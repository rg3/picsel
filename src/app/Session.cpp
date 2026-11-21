#include "Session.h"

#include "core/DirScanner.h"
#include "core/Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QUrl>

#include <algorithm>

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
    m_thumbs = std::make_shared<ImageCache>(tp);

    ImageCache::Params fp;
    fp.quality = DecodeQuality::High;
    fp.threads = 4;
    fp.maxDistance = 3;
    fp.budgetBytes = 512LL << 20;
    m_full = std::make_shared<ImageCache>(fp);
    m_full->setPaused(true);

    m_thumbs->setDirectory(m_dir);
    m_full->setDirectory(m_dir);
    connect(m_thumbs.get(), &ImageCache::ready, this, &Session::onThumbReady);
    connect(m_thumbs.get(), &ImageCache::failed, this, &Session::onThumbFailed);

    m_columns = settings::columns();
    m_metaPool.setMaxThreadCount(4);
    m_metaFlushTimer.setSingleShot(true);
    m_metaFlushTimer.setInterval(400);
    connect(&m_metaFlushTimer, &QTimer::timeout, this, &Session::flushMetaWrites);
    m_resortTimer.setSingleShot(true);
    m_resortTimer.setInterval(250);
    connect(&m_resortTimer, &QTimer::timeout, this, &Session::resortAll);
    m_removalTimer.setSingleShot(true);
    m_removalTimer.setInterval(60);
    connect(&m_removalTimer, &QTimer::timeout, this, &Session::flushRemovals);
    m_cacheOrderTimer.setSingleShot(true);
    m_cacheOrderTimer.setInterval(0);
    connect(&m_cacheOrderTimer, &QTimer::timeout, this, &Session::rebuildCacheOrder);

    registerProviderSession(m_id, ProviderEntry{m_thumbs, m_full, m_uids});
}

Session::~Session() {
    delete m_moveJob; // cancels and joins the worker
    flushRemovals();
    m_metaPool.clear();
    m_metaPool.waitForDone();
    flushMetaWrites();
    unregisterProviderSession(m_id);
}

void Session::fail(const QString &message) {
    m_error = message;
}

bool Session::open() {
    if (!m_db.open(m_dir)) {
        fail(tr("Cannot open %1: %2").arg(SelectionDb::dbPath(m_dir), m_db.lastError()));
        return false;
    }
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
    it.sortTimeMs = correctedTimeMs(f.meta.exifTime, f.meta.camera, settings::cameraOffsets());
    m_uids->set(it.uid, it.name);
    return it;
}

bool Session::loadFiles() {
    const QVector<DiskFile> disk = scanDir(m_dir);
    const ReconcileResult r = reconcile(m_db.files(), disk);
    if (!r.isEmpty() && !applyReconcile(m_db, r, m_defaultSelected)) {
        fail(tr("Cannot update %1: %2").arg(SelectionDb::dbPath(m_dir), m_db.lastError()));
        return false;
    }
    QVector<Item> items;
    const QVector<DbFile> rows = m_db.files();
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
    return true;
}

void Session::startMetadataScan() {
    m_metaPool.clear();
    for (const Item &it : m_model.items()) {
        if (it.kind != MediaKind::Image)
            continue;
        const QString name = it.name;
        const QString path = QDir(m_dir).filePath(name);
        const qint64 size = it.size, mtime = it.mtimeNs;
        m_metaPool.start([this, name, path, size, mtime] {
            const PhotoInfo info = readPhotoInfo(path).value_or(PhotoInfo{});
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
        const qint64 t = correctedTimeMs(info.exifTime, info.camera, settings::cameraOffsets());
        if (t != it->sortTimeMs) {
            it->sortTimeMs = t;
            if (m_sortMode == SortMode::Date)
                m_resortTimer.start();
        }
    }
    emit itemInfoChanged(row);
}

void Session::flushMetaWrites() {
    if (m_pendingMeta.isEmpty() || !m_db.isOpen())
        return;
    SelectionDb::Transaction tx(m_db);
    for (auto i = m_pendingMeta.cbegin(); i != m_pendingMeta.cend(); ++i)
        m_db.setMeta(i.key(), i.value());
    tx.commit();
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
    m_db.setConfig(QString::fromLatin1(cfg::SortMode), sortModeToString(m));
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
    m[QStringLiteral("date")] = date;
    const PhotoInfo details = m_details.value(it->name);
    m[QStringLiteral("lens")] = details.lens;
    m[QStringLiteral("exposure")] = details.exposure;
    return m;
}

void Session::setThumbBox(int w, int h) {
    m_thumbs->setTargetSize(QSize(w, h));
}

void Session::setVisibleRange(int first, int last) {
    m_thumbs->setFocus(first, last);
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
    const bool dimsChanged = fullSize.isValid() && it->dims != fullSize;
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
        SelectionDb::Transaction tx(m_db);
        for (const QString &n : names)
            m_db.removeFile(n);
        tx.commit();
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
    m_details.remove(name);
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

} // namespace picsel
