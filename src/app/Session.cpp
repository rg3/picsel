// SPDX-License-Identifier: GPL-3.0-or-later
#include "Session.h"

#include "core/DirScanner.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QLocale>
#include <QMap>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <cerrno>

namespace picsel {

namespace {
int g_nextSessionId = 1;
constexpr int kUndoLimit = 1000;
} // namespace

Session::Session(const QString &dir, QObject *parent)
    : QObject(parent),
      m_id(g_nextSessionId++),
      m_dir(QDir(dir).absolutePath()),
      m_uids(std::make_shared<UidMap>()) {
    ImageCache::Params tp;
    tp.quality = DecodeQuality::Fast;
    tp.budgetBytes = 4LL << 30;
    tp.computeHash = true;
    m_thumbs = ImageCache::createShared(tp);

    ImageCache::Params fp;
    fp.quality = DecodeQuality::High;
    fp.threads = 4;
    fp.maxDistance = 3;
    fp.budgetBytes = 512LL << 20;
    m_full = ImageCache::createShared(fp);
    m_full->setPaused(true);

    m_thumbs->setDirectory(m_dir);
    m_full->setDirectory(m_dir);
    connect(m_thumbs.get(), &ImageCache::ready, this, &Session::onThumbReady);
    connect(m_thumbs.get(), &ImageCache::failed, this, &Session::onThumbFailed);
    connect(m_thumbs.get(), &ImageCache::hashed, this, [this](const QString &name, quint64 h) {
        m_hashes.insert(name, h);
        if (!m_groupTimer.isActive())
            m_groupTimer.start();
    });
    connect(&m_video, &VideoThumbnailer::ready, this,
            [this](const QString &name, const QImage &img, qint64 durationMs, QSize fullSize) {
                const int row = m_model.rowOf(name);
                if (Item *it = m_model.mutableAt(row)) {
                    it->dims = fullSize; // the video's size, not the thumbnail's
                    if (it->durationMs != durationMs) {
                        it->durationMs = durationMs;
                        m_model.notifyChanged(row, {FileListModel::DurationRole});
                    }
                }
                m_thumbs->setImage(name, img); // -> ready -> onThumbReady
            });
    connect(&m_video, &VideoThumbnailer::failed, this,
            [this](const QString &name, const QString &) { onThumbFailed(name); });
    m_showSimilar = settings::showSimilar();
    m_similarSeconds = settings::similarSeconds();
    m_similarBits = settings::similarBits();
    m_groupTimer.setSingleShot(true);
    m_groupTimer.setInterval(300);
    connect(&m_groupTimer, &QTimer::timeout, this, &Session::recomputeGroups);

    m_columns = settings::columns();
    m_offsets = settings::cameraOffsets();
    m_metaPool.setMaxThreadCount(4);
    m_metaFlushTimer.setSingleShot(true);
    m_metaFlushTimer.setInterval(400);
    connect(&m_metaFlushTimer, &QTimer::timeout, this, &Session::flushMetaWrites);
    m_resortTimer.setSingleShot(true);
    m_resortTimer.setInterval(250);
    connect(&m_resortTimer, &QTimer::timeout, this, &Session::resortAll);
    connect(&m_watcher, &DirWatcher::fileAdded, this, &Session::onFileAdded);
    connect(&m_watcher, &DirWatcher::fileRemoved, this, &Session::onFileRemoved);
    connect(&m_watcher, &DirWatcher::fileRenamed, this, &Session::onFileRenamed);
    connect(&m_watcher, &DirWatcher::fileChanged, this, &Session::onFileChanged);
    connect(&m_watcher, &DirWatcher::rescanNeeded, this, &Session::onRescanNeeded);
    connect(&m_watcher, &DirWatcher::directoryGone, this, [this] {
        emit errorOccurred(tr("The folder %1 was deleted or moved. Changes can no longer be saved.").arg(m_dir));
    });
    m_removalTimer.setSingleShot(true);
    m_removalTimer.setInterval(60);
    connect(&m_removalTimer, &QTimer::timeout, this, &Session::flushRemovals);
    m_cacheOrderTimer.setSingleShot(true);
    m_cacheOrderTimer.setInterval(0);
    connect(&m_cacheOrderTimer, &QTimer::timeout, this, &Session::rebuildCacheOrder);

    registerProviderSession(m_id, ProviderEntry{m_thumbs, m_full, m_uids});
}

Session::~Session() {
    m_closing = true;
    delete m_moveJob; // cancels and joins the worker
    flushRemovals();
    m_metaPool.clear();
    m_metaPool.waitForDone();
    flushMetaWrites();
    unregisterProviderSession(m_id);
}

void Session::reportError(const QString &message) {
    // The window is closing or gone: nobody would see a dialog.
    if (m_closing)
        qWarning().noquote() << "picsel:" << message;
    else
        emit errorOccurred(message);
}

void Session::fail(const QString &message) {
    m_error = message;
}

bool Session::open() {
    if (!m_db.open(m_dir)) {
        fail(tr("Cannot open %1: %2").arg(SelectionDb::dbPath(m_dir), m_db.lastError()));
        return false;
    }
    // Older databases may lack it; it is what reveals a moved or copied folder.
    if (m_db.config(QString::fromLatin1(cfg::SourceDir)).isEmpty()
        && !m_db.setConfig(QString::fromLatin1(cfg::SourceDir), m_dir))
        qWarning().noquote() << "picsel: cannot record the folder in" << SelectionDb::dbPath(m_dir)
                             << m_db.lastError(); // only needed to detect a later move
    m_targetDir = m_db.config(QString::fromLatin1(cfg::TargetDir));
    m_defaultSelected = m_db.config(QString::fromLatin1(cfg::DefaultSelected)) == QLatin1String("1");
    m_sortMode = sortModeFromString(m_db.config(QString::fromLatin1(cfg::SortMode)));
    return loadFiles();
}

bool Session::create(const QString &targetDir, bool defaultSelected) {
    if (!m_db.create(m_dir, targetDir, defaultSelected)) {
        fail(tr("Cannot create %1: %2").arg(SelectionDb::dbPath(m_dir), m_db.lastError()));
        return false;
    }
    m_db.close();
    return open();
}

Item Session::makeItem(const DbFile &f) {
    Item it;
    it.uid = m_nextUid++;
    it.name = f.name;
    it.selected = f.selected;
    it.kind = mediaKindForName(f.name);
    it.size = f.size;
    it.mtimeNs = f.mtimeNs;
    it.inode = f.inode;
    it.meta = f.meta;
    it.sortTimeMs = correctedTimeMs(f.meta.exifTime, f.meta.camera, m_offsets);
    m_uids->set(it.uid, it.name);
    return it;
}

bool Session::loadFiles() {
    // Watch first so nothing created during the scan is missed (the watcher
    // and the reconcile below agree on the result either way).
    if (!m_watcher.start(m_dir))
        qWarning().noquote() << "picsel: cannot watch" << m_dir << m_watcher.errorString();
    bool readOk = false;
    const QVector<DiskFile> disk = scanDir(m_dir, &readOk);
    const int scanErr = errno; // before anything else can change it
    if (!readOk) {
        // Never reconcile against a partial listing: that would drop the
        // choices of every file it missed.
        fail(tr("Cannot list the files in %1: %2").arg(m_dir, qt_error_string(scanErr)));
        return false;
    }
    const QVector<DbFile> before = m_db.files(&readOk);
    if (!readOk) {
        // Never reconcile against a partial list: that would reset choices.
        fail(tr("Cannot read %1: %2").arg(SelectionDb::dbPath(m_dir), m_db.lastError()));
        return false;
    }
    const ReconcileResult r = reconcile(before, disk);
    if (!r.isEmpty() && !applyReconcile(m_db, r, m_defaultSelected)) {
        fail(tr("Cannot update %1: %2").arg(SelectionDb::dbPath(m_dir), m_db.lastError()));
        return false;
    }
    QVector<Item> items;
    const QVector<DbFile> rows = m_db.files(&readOk);
    if (!readOk) {
        fail(tr("Cannot read %1: %2").arg(SelectionDb::dbPath(m_dir), m_db.lastError()));
        return false;
    }
    items.reserve(rows.size());
    m_uids->clear();
    for (const DbFile &f : rows)
        items.push_back(makeItem(f));
    const SortMode mode = m_sortMode;
    std::sort(items.begin(), items.end(), [mode](const Item &a, const Item &b) {
        return sortLess(sortKeyOf(a), sortKeyOf(b), mode);
    });
    m_model.reset(std::move(items));
    recountSelected();
    rebuildCacheOrder();
    startMetadataScan();
    requestVideoThumbs();
    return true;
}

void Session::startMetadataScan() {
    m_metaPool.clear();
    QStringList names;
    for (const Item &it : m_model.items())
        names << it.name;
    scheduleMetadata(names);
}

void Session::scheduleMetadata(const QStringList &names) {
    for (const QString &name : names) {
        const Item *it = m_model.at(m_model.rowOf(name));
        if (!it)
            continue;
        const QString path = QDir(m_dir).filePath(name);
        const qint64 size = it->size, mtime = it->mtimeNs;
        const bool video = it->kind == MediaKind::Video;
        m_metaPool.start([this, name, path, size, mtime, video] {
            PhotoInfo info;
            if (!video) {
                info = readPhotoInfo(path).value_or(PhotoInfo{});
            } else if (auto v = readMp4Info(path)) {
                // Videos have no camera model; group them per container so
                // their clock can be corrected like a camera's.
                info.exifTime = v->creationLocal;
                info.camera = QStringLiteral("Videos (%1)").arg(QFileInfo(name).suffix().toUpper());
                info.size = v->size;
                if (v->durationMs >= 0) {
                    const qint64 s = (v->durationMs + 500) / 1000;
                    info.exposure = QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
                }
            }
            QMetaObject::invokeMethod(this, [this, name, size, mtime, info] {
                onMetadataRead(name, size, mtime, info);
            }, Qt::QueuedConnection);
        });
    }
}

void Session::onMetadataRead(const QString &name, qint64 size, qint64 mtimeNs, const PhotoInfo &info) {
    const int row = m_model.rowOf(name);
    Item *it = m_model.mutableAt(row);
    if (!it || it->size != size || it->mtimeNs != mtimeNs)
        return; // file changed or vanished meanwhile
    m_details.insert(name, info);
    if (!it->meta.scanned || it->meta.exifTime != info.exifTime || it->meta.camera != info.camera) {
        it->meta.scanned = true;
        it->meta.exifTime = info.exifTime;
        it->meta.camera = info.camera;
        m_pendingMeta.insert(name, it->meta);
        m_metaFlushTimer.start();
        const qint64 t = correctedTimeMs(info.exifTime, info.camera, m_offsets);
        if (t != it->sortTimeMs) {
            it->sortTimeMs = t;
            if (m_sortMode == SortMode::Date)
                m_resortTimer.start();
            // Groups also check times; don't restart a running timer, so a
            // stream of results can't keep postponing it.
            if (!m_groupTimer.isActive())
                m_groupTimer.start();
        }
    }
    emit itemInfoChanged(row);
}

void Session::flushMetaWrites() {
    if (m_pendingMeta.isEmpty() || !m_db.isOpen())
        return;
    // On failure the batch is dropped: the files are read again next time.
    SelectionDb::Transaction tx(m_db);
    bool ok = true;
    for (auto i = m_pendingMeta.cbegin(); ok && i != m_pendingMeta.cend(); ++i)
        ok = m_db.setMeta(i.key(), i.value());
    if (!ok || !tx.commit())
        reportError(tr("Could not save picture details: %1").arg(m_db.lastError()));
    m_pendingMeta.clear();
}

void Session::setCurrentRow(int row) {
    if (const Item *it = m_model.at(row))
        m_currentName = it->name;
}

void Session::resortAll() {
    QVector<Item> items = m_model.items();
    const SortMode m = m_sortMode;
    std::stable_sort(items.begin(), items.end(), [m](const Item &a, const Item &b) {
        return sortLess(sortKeyOf(a), sortKeyOf(b), m);
    });
    bool same = true;
    for (int i = 0; i < items.size() && same; ++i)
        same = items[i].uid == m_model.items()[i].uid;
    if (same)
        return;
    m_model.reset(std::move(items));
    rebuildCacheOrder();
    const int row = m_model.rowOf(m_currentName);
    emit highlightRequested(row >= 0 ? row : 0);
}

void Session::scheduleCacheOrder() {
    m_cacheOrderTimer.start();
}

void Session::rebuildCacheOrder() {
    QStringList names;
    QVector<bool> decodable;
    names.reserve(m_model.count());
    decodable.reserve(m_model.count());
    for (const Item &it : m_model.items()) {
        names.push_back(it.name);
        decodable.push_back(it.kind == MediaKind::Image);
    }
    m_thumbs->setFiles(names, decodable);
    m_full->setFiles(names, decodable);
    m_groupTimer.start(); // order or membership changed
}

void Session::recountSelected() {
    int n = 0;
    for (const Item &it : m_model.items())
        n += it.selected ? 1 : 0;
    m_selectedCount = n;
    emit countsChanged();
}

void Session::setDefaultSelected(bool v) {
    if (v == m_defaultSelected)
        return;
    if (!m_db.setConfig(QString::fromLatin1(cfg::DefaultSelected), v ? QStringLiteral("1") : QStringLiteral("0"))) {
        emit errorOccurred(m_db.lastError());
        return;
    }
    m_defaultSelected = v;
    emit defaultSelectedChanged();
}

void Session::setSortMode(const QString &mode) {
    const SortMode m = sortModeFromString(mode);
    if (m == m_sortMode)
        return;
    m_sortMode = m;
    if (!m_db.setConfig(QString::fromLatin1(cfg::SortMode), sortModeToString(m)))
        emit errorOccurred(tr("Could not save the sort order: %1").arg(m_db.lastError()));
    resortAll();
    emit sortModeChanged();
}

void Session::setColumns(int n) {
    n = std::clamp(n, 2, 6);
    if (n == m_columns)
        return;
    m_columns = n;
    settings::setColumns(n);
    emit columnsChanged();
}

bool Session::applySelected(int row, bool selected, bool recordUndo) {
    Item *it = m_model.mutableAt(row);
    if (!it)
        return false;
    if (it->selected == selected)
        return true;
    if (!m_db.setSelected(it->name, selected)) {
        emit errorOccurred(tr("Could not save the selection: %1").arg(m_db.lastError()));
        return false;
    }
    if (recordUndo) {
        m_undo.push_back({it->name, it->selected});
        if (m_undo.size() > kUndoLimit)
            m_undo.removeFirst();
        if (m_undo.size() == 1)
            emit canUndoChanged();
    }
    it->selected = selected;
    m_selectedCount += selected ? 1 : -1;
    m_model.notifyChanged(row, {FileListModel::SelectedRole});
    emit countsChanged();
    return true;
}

void Session::setSelected(int row, bool selected) {
    applySelected(row, selected, true);
}

void Session::toggleSelected(int row) {
    if (const Item *it = m_model.at(row))
        applySelected(row, !it->selected, true);
}

int Session::undo() {
    while (!m_undo.isEmpty()) {
        const UndoEntry e = m_undo.takeLast();
        if (m_undo.isEmpty())
            emit canUndoChanged();
        const int row = m_model.rowOf(e.name);
        if (row < 0)
            continue; // file vanished meanwhile
        applySelected(row, e.previous, false);
        return row;
    }
    return -1;
}

bool Session::setTargetDir(const QString &pathOrUrl) {
    QString path = pathOrUrl;
    const QUrl url(pathOrUrl);
    if (url.isLocalFile())
        path = url.toLocalFile();
    path = QDir(path).absolutePath();
    const QFileInfo fi(path);
    if (!fi.isDir()) {
        emit errorOccurred(tr("%1 is not a folder.").arg(path));
        return false;
    }
    if (!m_db.setConfig(QString::fromLatin1(cfg::TargetDir), path)) {
        emit errorOccurred(m_db.lastError());
        return false;
    }
    m_targetDir = path;
    emit targetDirChanged();
    return true;
}

QVariantMap Session::info(int row) const {
    QVariantMap m;
    const Item *it = m_model.at(row);
    if (!it)
        return m;
    m[QStringLiteral("name")] = it->name;
    m[QStringLiteral("size")] = QLocale().formattedDataSize(it->size, 1, QLocale::DataSizeTraditionalFormat);
    m[QStringLiteral("width")] = it->dims.width();
    m[QStringLiteral("height")] = it->dims.height();
    m[QStringLiteral("isVideo")] = it->kind == MediaKind::Video;
    m[QStringLiteral("selected")] = it->selected;
    m[QStringLiteral("camera")] = it->meta.camera;
    QString date = it->meta.exifTime;
    date.replace(QLatin1Char('T'), QLatin1Char(' '));
    const qint64 offset = m_offsets.value(it->meta.camera, 0);
    if (offset != 0 && it->sortTimeMs != kNoTime) {
        const QDateTime corrected = QDateTime::fromMSecsSinceEpoch(it->sortTimeMs, QTimeZone::UTC);
        date = corrected.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QLatin1Char(' ') + tr("(clock corrected)");
    }
    m[QStringLiteral("date")] = date;
    const PhotoInfo details = m_details.value(it->name);
    m[QStringLiteral("lens")] = details.lens;
    m[QStringLiteral("exposure")] = it->kind == MediaKind::Video ? QString() : details.exposure;
    m[QStringLiteral("duration")] = it->kind == MediaKind::Video ? details.exposure : QString();
    if (it->kind == MediaKind::Video && it->dims.isEmpty() && details.size.isValid()) {
        m[QStringLiteral("width")] = details.size.width();
        m[QStringLiteral("height")] = details.size.height();
    }
    int groupSize = 0;
    if (it->groupId >= 0)
        for (const Item &other : m_model.items())
            groupSize += other.groupId == it->groupId ? 1 : 0;
    m[QStringLiteral("groupSize")] = groupSize;
    return m;
}

void Session::setThumbBox(int w, int h) {
    const QSize box(w, h);
    const bool bigger = !m_thumbBox.isValid() || w > m_thumbBox.width() * 1.1 || h > m_thumbBox.height() * 1.1;
    m_thumbBox = box;
    m_thumbs->setTargetSize(box);
    if (bigger)
        requestVideoThumbs();
}

void Session::requestVideoThumbs() {
    if (!m_thumbBox.isValid())
        return;
    for (const Item &it : m_model.items())
        if (it.kind == MediaKind::Video)
            m_video.request(it.name, QDir(m_dir).filePath(it.name), m_thumbBox);
}

void Session::setVisibleRange(int first, int last) {
    m_thumbs->setFocus(first, last);
    for (int row = std::min(last, m_model.count() - 1); row >= std::max(0, first); --row)
        if (const Item *it = m_model.at(row); it->kind == MediaKind::Video && it->thumbVersion == 0)
            m_video.prioritize(it->name);
}

QUrl Session::fileUrl(int row) const {
    const Item *it = m_model.at(row);
    return it ? QUrl::fromLocalFile(QDir(m_dir).filePath(it->name)) : QUrl();
}

void Session::setFullBox(int w, int h) {
    m_full->setTargetSize(QSize(w, h));
}

void Session::setFullFocus(int row) {
    m_full->setPaused(row < 0); // closed: stop prefetching
    if (row >= 0)
        m_full->setFocus(row, row);
}

void Session::requestHighlight(const QString &name) {
    const int row = m_model.rowOf(name);
    if (row >= 0)
        emit highlightRequested(row);
}

void Session::onThumbReady(const QString &name, QSize fullSize) {
    const int row = m_model.rowOf(name);
    Item *it = m_model.mutableAt(row);
    if (!it)
        return;
    it->thumbVersion++;
    it->thumbFailed = false;
    // Videos get their real size from the thumbnailer, not from this image.
    const bool dimsChanged = it->kind == MediaKind::Image && fullSize.isValid() && it->dims != fullSize;
    if (dimsChanged)
        it->dims = fullSize;
    m_model.notifyChanged(row, {FileListModel::ThumbVersionRole, FileListModel::ThumbFailedRole});
    if (dimsChanged)
        emit itemInfoChanged(row);
}

void Session::onThumbFailed(const QString &name) {
    const int row = m_model.rowOf(name);
    Item *it = m_model.mutableAt(row);
    if (!it)
        return;
    it->thumbFailed = true;
    m_model.notifyChanged(row, {FileListModel::ThumbFailedRole});
}

QVariantList Session::cameras() const {
    QMap<QString, int> counts;
    for (const Item &it : m_model.items())
        if (!it.meta.camera.isEmpty())
            counts[it.meta.camera]++;
    const CameraOffsets &offsets = m_offsets;
    QVariantList out;
    for (auto i = counts.cbegin(); i != counts.cend(); ++i)
        out << QVariantMap{{QStringLiteral("camera"), i.key()},
                           {QStringLiteral("count"), i.value()},
                           {QStringLiteral("offset"), offsets.value(i.key(), 0)}};
    return out;
}

void Session::setCameraOffset(const QString &camera, qint64 seconds) {
    settings::setCameraOffset(camera, seconds);
    reloadCameraOffsets();
    emit cameraOffsetsEdited();
}

void Session::reloadCameraOffsets() {
    m_offsets = settings::cameraOffsets();
    const CameraOffsets &offsets = m_offsets;
    for (int row = 0; row < m_model.count(); ++row) {
        Item *it = m_model.mutableAt(row);
        it->sortTimeMs = correctedTimeMs(it->meta.exifTime, it->meta.camera, offsets);
    }
    if (m_sortMode == SortMode::Date)
        resortAll();
    m_groupTimer.start(); // groups check the corrected times
    emit itemInfoChanged(m_model.rowOf(m_currentName));
}

QVariantMap Session::prepareMove() const {
    QStringList names;
    for (const Item &it : m_model.items())
        if (it.selected)
            names << it.name;
    const PreflightResult r = preflightMove(m_dir, m_targetDir, names);
    return {
        {QStringLiteral("count"), int(names.size())},
        {QStringLiteral("total"), m_model.count()},
        {QStringLiteral("target"), m_targetDir},
        {QStringLiteral("fatal"), r.fatal},
        {QStringLiteral("blocked"), r.blocked},
        {QStringLiteral("conflicts"), r.conflicts},
    };
}

void Session::startMove() {
    if (m_moveJob)
        return;
    QStringList names;
    for (const Item &it : m_model.items())
        if (it.selected)
            names << it.name;
    if (names.isEmpty())
        return;
    // Selection changes made before the move can't be undone afterwards.
    if (!m_undo.isEmpty()) {
        m_undo.clear();
        emit canUndoChanged();
    }
    m_moveJob = new MoveJob(m_dir, m_targetDir, names, this);
    connect(m_moveJob, &MoveJob::progress, this, &Session::moveProgress);
    connect(m_moveJob, &MoveJob::conflictFound, this, &Session::moveConflict);
    connect(m_moveJob, &MoveJob::fileMoved, this, [this](const QString &name, const QString &) {
        onFileMoved(name);
    });
    connect(m_moveJob, &MoveJob::finished, this, &Session::onMoveFinished);
    emit movingChanged();
    m_moveJob->start();
}

void Session::resolveConflict(int action, bool applyToAll) {
    if (!m_moveJob)
        return;
    const ConflictAction a = action == 0 ? ConflictAction::Overwrite
        : action == 1 ? ConflictAction::Rename : ConflictAction::Skip;
    m_moveJob->resolveConflict(a, applyToAll);
}

void Session::cancelMove() {
    if (m_moveJob)
        m_moveJob->cancel();
}

void Session::onFileMoved(const QString &name) {
    m_pendingRemovals << name;
    if (!m_removalTimer.isActive())
        m_removalTimer.start();
}

void Session::flushRemovals() {
    if (m_pendingRemovals.isEmpty())
        return;
    const QStringList names = std::exchange(m_pendingRemovals, {});
    if (m_db.isOpen()) {
        // On failure the rows stay; the next open drops them again.
        SelectionDb::Transaction tx(m_db);
        bool ok = true;
        for (int i = 0; ok && i < names.size(); ++i)
            ok = m_db.removeFile(names[i]);
        if (!ok || !tx.commit())
            reportError(tr("Could not record removed files: %1").arg(m_db.lastError()));
    }
    for (const QString &n : names)
        removeItem(n);
    rebuildCacheOrder();
    recountSelected();
}

void Session::removeItem(const QString &name) {
    const int row = m_model.rowOf(name);
    if (row < 0)
        return;
    const quint32 uid = m_model.at(row)->uid;
    const bool wasCurrent = name == m_currentName;
    m_model.removeRow(row);
    m_uids->remove(uid);
    // A later file with the same name must not inherit these pixels.
    m_thumbs->remove(name);
    m_full->remove(name);
    m_video.cancel(name);
    m_details.remove(name);
    m_hashes.remove(name);
    m_pendingMeta.remove(name);
    if (wasCurrent) {
        const Item *next = m_model.at(std::min(row, m_model.count() - 1));
        m_currentName = next ? next->name : QString();
    }
}

void Session::onMoveFinished(const QVector<MoveOutcome> &outcomes, bool cancelled) {
    flushRemovals();
    int moved = 0, skipped = 0;
    QStringList failures;
    for (const MoveOutcome &o : outcomes) {
        switch (o.status) {
        case MoveOutcome::Moved: ++moved; break;
        case MoveOutcome::Skipped: ++skipped; break;
        case MoveOutcome::Failed: failures << QStringLiteral("%1: %2").arg(o.name, o.error); break;
        }
    }
    m_moveJob->deleteLater();
    m_moveJob = nullptr;
    emit movingChanged();
    const int row = m_model.rowOf(m_currentName);
    emit highlightRequested(row >= 0 ? row : 0);
    emit moveFinished(moved, skipped, failures, cancelled);
}

// --- Live folder changes -------------------------------------------------

void Session::onFileAdded(const DiskFile &f) {
    if (m_model.rowOf(f.name) >= 0) {
        onFileChanged(f);
        return;
    }
    DbFile d;
    d.name = f.name;
    d.selected = m_defaultSelected;
    d.size = f.size;
    d.mtimeNs = f.mtimeNs;
    d.inode = f.inode;
    if (!m_db.insertFile(d)) {
        emit errorOccurred(tr("Could not record %1: %2").arg(f.name, m_db.lastError()));
        return;
    }
    m_model.insertSorted(makeItem(d), m_sortMode);
    scheduleCacheOrder();
    if (mediaKindForName(f.name) == MediaKind::Video && m_thumbBox.isValid())
        m_video.request(f.name, QDir(m_dir).filePath(f.name), m_thumbBox);
    recountSelected();
    scheduleMetadata({f.name});
    // Keep the highlight on the same picture if rows shifted under it.
    const int row = m_model.rowOf(m_currentName);
    if (row >= 0)
        emit highlightRequested(row);
}

void Session::onFileRemoved(const QString &name) {
    if (m_model.rowOf(name) < 0)
        return; // already gone (e.g. moved by us)
    if (!m_db.removeFile(name))
        emit errorOccurred(tr("Could not record the removal of %1: %2").arg(name, m_db.lastError()));
    const QString current = m_currentName;
    removeItem(name);
    scheduleCacheOrder();
    recountSelected();
    const int row = m_model.rowOf(current == name ? m_currentName : current);
    if (row >= 0)
        emit highlightRequested(row);
}

void Session::onFileRenamed(const QString &from, const DiskFile &to) {
    const int fromRow = m_model.rowOf(from);
    if (fromRow < 0) {
        onFileAdded(to);
        return;
    }
    if (from != to.name && m_model.rowOf(to.name) >= 0)
        removeItem(to.name); // renamed over an existing picture
    {
        SelectionDb::Transaction tx(m_db);
        if (!m_db.renameFile(from, to.name) || !m_db.updateStat(to.name, to.size, to.mtimeNs, to.inode)
            || !tx.commit()) {
            emit errorOccurred(tr("Could not record the rename of %1: %2").arg(from, m_db.lastError()));
            return;
        }
    }
    const int row = m_model.rowOf(from);
    m_model.setName(row, to.name);
    Item *it = m_model.mutableAt(row);
    it->kind = mediaKindForName(to.name);
    it->size = to.size;
    it->mtimeNs = to.mtimeNs;
    it->inode = to.inode;
    m_uids->set(it->uid, to.name);
    if (m_details.contains(from))
        m_details.insert(to.name, m_details.take(from));
    if (m_hashes.contains(from))
        m_hashes.insert(to.name, m_hashes.take(from));
    if (m_pendingMeta.contains(from))
        m_pendingMeta.insert(to.name, m_pendingMeta.take(from)); // its old row is gone
    if (m_currentName == from)
        m_currentName = to.name;
    const int newRow = m_model.resort(row, m_sortMode); // also notifies the row
    // The caches are keyed by name: the picture is decoded again under its new name.
    scheduleCacheOrder();
    if (it->kind == MediaKind::Video && m_thumbBox.isValid()) {
        m_video.cancel(from);
        m_video.request(to.name, QDir(m_dir).filePath(to.name), m_thumbBox);
    }
    recountSelected();
    emit itemInfoChanged(newRow);
    const int cur = m_model.rowOf(m_currentName);
    if (cur >= 0)
        emit highlightRequested(cur);
}

void Session::onFileChanged(const DiskFile &f) {
    const int row = m_model.rowOf(f.name);
    Item *it = m_model.mutableAt(row);
    if (!it) {
        onFileAdded(f);
        return;
    }
    if (!m_db.updateStat(f.name, f.size, f.mtimeNs, f.inode))
        emit errorOccurred(tr("Could not record the change of %1: %2").arg(f.name, m_db.lastError()));
    it->size = f.size;
    it->mtimeNs = f.mtimeNs;
    it->inode = f.inode;
    it->meta.scanned = false;
    m_thumbs->invalidate(f.name);
    m_full->invalidate(f.name);
    if (it->kind == MediaKind::Video && m_thumbBox.isValid())
        m_video.request(f.name, QDir(m_dir).filePath(f.name), m_thumbBox);
    scheduleMetadata({f.name});
    emit itemInfoChanged(row);
}

void Session::onRescanNeeded() {
    // Rare (inotify queue overflow): reconcile everything from scratch.
    flushRemovals();
    flushMetaWrites();
    m_details.clear();
    m_hashes.clear();
    m_thumbs->clear(); // items are rebuilt with no thumbnail yet: decode again
    m_full->clear();
    m_video.clear();
    if (!loadFiles())
        emit errorOccurred(m_error);
    const int row = m_model.rowOf(m_currentName);
    emit highlightRequested(row >= 0 ? row : 0);
}

// --- Similar pictures ------------------------------------------------------

void Session::setShowSimilar(bool on) {
    if (on == m_showSimilar)
        return;
    m_showSimilar = on;
    settings::setShowSimilar(on);
    emit similarChanged();
    recomputeGroups();
}

void Session::setSimilarSeconds(int s) {
    s = std::clamp(s, 0, 3600);
    if (s == m_similarSeconds)
        return;
    m_similarSeconds = s;
    settings::setSimilarSeconds(s);
    emit similarChanged();
    m_groupTimer.start();
}

void Session::setSimilarBits(int b) {
    b = std::clamp(b, 0, 32);
    if (b == m_similarBits)
        return;
    m_similarBits = b;
    settings::setSimilarBits(b);
    emit similarChanged();
    m_groupTimer.start();
}

void Session::recomputeGroups() {
    const QVector<Item> &items = m_model.items();
    QVector<int> groups(items.size(), -1);
    if (m_showSimilar) {
        QVector<SimilarityInput> in;
        in.reserve(items.size());
        for (const Item &it : items) {
            auto h = m_hashes.constFind(it.name);
            in.push_back({h == m_hashes.constEnd() ? std::nullopt : std::optional<quint64>(*h), it.sortTimeMs});
        }
        groups = similarityGroups(in, m_similarBits, m_similarSeconds);
    }
    bool changed = false;
    int count = 0;
    for (int row = 0; row < items.size(); ++row) {
        Item *it = m_model.mutableAt(row);
        if (it->groupId != groups[row]) {
            it->groupId = groups[row];
            changed = true;
        }
        count = std::max(count, groups[row] + 1);
    }
    if (changed) {
        m_model.notifyAllChanged({FileListModel::GroupIdRole, FileListModel::GroupStartRole,
                                  FileListModel::GroupEndRole});
        emit itemInfoChanged(m_model.rowOf(m_currentName));
    }
    if (count != m_groupCount) {
        m_groupCount = count;
        emit groupsChanged();
    }
}

} // namespace picsel
