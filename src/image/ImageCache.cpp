// SPDX-License-Identifier: GPL-3.0-or-later
#include "ImageCache.h"

#include "core/Similarity.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QThread>

namespace picsel {

namespace {
// Re-decode only when the target changed noticeably; tiny resizes just scale.
bool closeEnough(QSize a, QSize b) {
    if (a.isEmpty() || b.isEmpty())
        return a == b;
    const double rw = double(a.width()) / b.width();
    const double rh = double(a.height()) / b.height();
    return rw > 0.9 && rw < 1.1 && rh > 0.9 && rh < 1.1;
}
} // namespace

ImageCache::ImageCache(Params p, QObject *parent) : QObject(parent), m_params(p) {
    m_pool.setMaxThreadCount(p.threads > 0 ? p.threads : QThread::idealThreadCount());
}

ImageCache::~ImageCache() {
    {
        QMutexLocker lock(&m_mutex);
        m_stopping = true;
        m_cond.wakeAll();
    }
    m_pool.clear();
    m_pool.waitForDone();
}

void ImageCache::setDirectory(const QString &dir) {
    QMutexLocker lock(&m_mutex);
    if (dir == m_dir)
        return;
    m_dir = dir;
    ++m_epoch;
    m_entries.clear();
    m_order.clear();
    m_index.clear();
    m_bytes = 0;
}

void ImageCache::setFiles(const QStringList &names, const QVector<bool> &decodable) {
    QMutexLocker lock(&m_mutex);
    QHash<QString, Entry> entries;
    entries.reserve(names.size());
    m_index.clear();
    m_bytes = 0;
    for (int i = 0; i < names.size(); ++i) {
        const QString &n = names[i];
        Entry e = m_entries.take(n);
        e.decodable = i < decodable.size() ? decodable[i] : true;
        m_bytes += bytesOf(e.img);
        entries.insert(n, std::move(e));
        m_index.insert(n, i);
    }
    m_entries = std::move(entries);
    m_order = names;
    pumpLocked();
}

void ImageCache::setTargetSize(QSize box) {
    QMutexLocker lock(&m_mutex);
    if (box == m_box)
        return;
    m_box = box;
    pumpLocked();
}

QSize ImageCache::targetSize() const {
    QMutexLocker lock(&m_mutex);
    return m_box;
}

void ImageCache::setFocus(int first, int last) {
    QMutexLocker lock(&m_mutex);
    m_first = std::max(0, first);
    m_last = std::max(m_first, last);
    pumpLocked();
}

void ImageCache::setBudget(qint64 bytes) {
    QMutexLocker lock(&m_mutex);
    m_params.budgetBytes = bytes;
    pumpLocked();
}

void ImageCache::setPaused(bool paused) {
    QMutexLocker lock(&m_mutex);
    m_paused = paused;
    pumpLocked();
}

QImage ImageCache::image(const QString &name) const {
    QMutexLocker lock(&m_mutex);
    auto it = m_entries.constFind(name);
    return it == m_entries.constEnd() ? QImage() : it->img;
}

QImage ImageCache::waitFor(const QString &name, int timeoutMs) {
    QMutexLocker lock(&m_mutex);
    QDeadlineTimer deadline(timeoutMs);
    m_boost = name;
    pumpLocked();
    while (!m_stopping) {
        auto it = m_entries.constFind(name);
        if (it == m_entries.constEnd())
            break;
        if (it->failed || !it->decodable || (!it->img.isNull() && closeEnough(it->decodedFor, m_box)))
            break;
        if (!m_cond.wait(&m_mutex, deadline))
            break;
    }
    if (m_boost == name)
        m_boost.clear();
    auto it = m_entries.constFind(name);
    return it == m_entries.constEnd() ? QImage() : it->img;
}

void ImageCache::invalidate(const QString &name) {
    QMutexLocker lock(&m_mutex);
    auto it = m_entries.find(name);
    if (it == m_entries.end())
        return;
    it->failed = false;
    it->decodedFor = QSize(); // keep the old pixels until the new ones arrive
    pumpLocked();
}

void ImageCache::remove(const QString &name) {
    QMutexLocker lock(&m_mutex);
    auto it = m_entries.find(name);
    if (it == m_entries.end())
        return;
    resetEntryLocked(*it);
    // Keep the slot (m_order and m_index refer to it) but skip it until the
    // next setFiles(), which decides again for a new file of that name.
    it->decodable = false;
    pumpLocked(); // the freed bytes may let other work start
}

void ImageCache::clear() {
    QMutexLocker lock(&m_mutex);
    for (Entry &e : m_entries)
        resetEntryLocked(e);
    pumpLocked();
}

void ImageCache::resetEntryLocked(Entry &e) {
    // A decode still in flight for the old contents is dropped when it lands
    // (generation mismatch); the entry can be scheduled again right away.
    m_bytes -= bytesOf(e.img);
    e.img = QImage();
    e.decodedFor = QSize();
    e.inFlight = false;
    e.failed = false;
    ++e.gen;
}

void ImageCache::setImage(const QString &name, const QImage &img) {
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_entries.find(name);
        if (it == m_entries.end())
            return;
        m_bytes += bytesOf(img) - bytesOf(it->img);
        it->img = img;
        it->decodedFor = m_box;
        it->failed = img.isNull();
        m_cond.wakeAll();
    }
    emit ready(name, img.size());
}

qint64 ImageCache::bytesUsed() const {
    QMutexLocker lock(&m_mutex);
    return m_bytes;
}

int ImageCache::pendingCount() const {
    QMutexLocker lock(&m_mutex);
    int n = m_active;
    for (const Entry &e : m_entries)
        if (needsWork(e))
            ++n;
    return n;
}

bool ImageCache::needsWork(const Entry &e) const {
    if (!e.decodable || e.inFlight || e.failed || m_box.isEmpty())
        return false;
    return e.img.isNull() || !closeEnough(e.decodedFor, m_box);
}

int ImageCache::distance(int i) const {
    if (i < m_first)
        return m_first - i;
    if (i > m_last)
        return i - m_last;
    return 0;
}

QString ImageCache::pickNextLocked() {
    if (!m_boost.isEmpty()) {
        auto it = m_entries.find(m_boost);
        if (it != m_entries.end() && needsWork(*it))
            return m_boost;
    }
    const int n = int(m_order.size());
    if (n == 0 || m_paused)
        return {};
    const int first = std::min(m_first, n - 1);
    const int last = std::min(m_last, n - 1);

    int found = -1;
    int foundDist = 0;
    for (int i = first; i <= last && found < 0; ++i)
        if (needsWork(m_entries[m_order[i]]))
            found = i;
    for (int d = 1; found < 0; ++d) {
        if (m_params.maxDistance >= 0 && d > m_params.maxDistance)
            return {};
        const int below = last + d, above = first - d;
        if (below >= n && above < 0)
            return {};
        if (below < n && needsWork(m_entries[m_order[below]]))
            found = below;
        else if (above >= 0 && needsWork(m_entries[m_order[above]]))
            found = above;
        foundDist = d;
    }

    // Respect the budget: evict the farthest cached image only if it's
    // farther from focus than the candidate; otherwise stop here.
    const qint64 estimate = qint64(m_box.width()) * m_box.height() * 4;
    while (m_bytes + estimate > m_params.budgetBytes) {
        int far = -1, farDist = -1;
        for (int i = 0; i < n; ++i) {
            const Entry &e = m_entries[m_order[i]];
            // Non-decodable entries (video frames) can't be recreated here.
            if (!e.img.isNull() && !e.inFlight && e.decodable && distance(i) > farDist) {
                far = i;
                farDist = distance(i);
            }
        }
        if (far < 0 || farDist <= foundDist)
            return {};
        Entry &e = m_entries[m_order[far]];
        m_bytes -= bytesOf(e.img);
        e.img = QImage();
        e.decodedFor = QSize();
    }
    return m_order[found];
}

void ImageCache::pumpLocked() {
    if (m_stopping || m_dir.isEmpty())
        return;
    while (m_active < m_pool.maxThreadCount()) {
        const QString name = pickNextLocked();
        if (name.isEmpty())
            return;
        Entry &e = m_entries[name];
        e.inFlight = true;
        ++m_active;
        const QSize box = m_box;
        const quint64 epoch = m_epoch;
        const quint32 gen = e.gen;
        m_pool.start([this, name, box, epoch, gen] { runJob(name, box, epoch, gen); });
    }
}

void ImageCache::runJob(const QString &name, QSize box, quint64 epoch, quint32 gen) {
    QString path;
    {
        QMutexLocker lock(&m_mutex);
        path = QDir(m_dir).filePath(name);
    }
    std::optional<DecodedImage> result = decodeImage(path, box, m_params.quality);
    const quint64 hash = (result && m_params.computeHash) ? dHash(result->image) : 0;

    QSize fullSize;
    bool ok = false;
    {
        QMutexLocker lock(&m_mutex);
        --m_active;
        if (m_stopping)
            return;
        auto it = m_entries.find(name);
        const bool valid = epoch == m_epoch && it != m_entries.end() && it->gen == gen;
        if (valid) {
            it->inFlight = false;
            if (result) {
                m_bytes += bytesOf(result->image) - bytesOf(it->img);
                it->img = std::move(result->image);
                it->decodedFor = box;
                fullSize = result->fullSize;
                ok = true;
            } else {
                it->failed = true;
            }
        }
        m_cond.wakeAll();
        pumpLocked();
        if (!valid)
            return;
    }
    if (ok && m_params.computeHash)
        emit hashed(name, hash);
    if (ok)
        emit ready(name, fullSize);
    else
        emit failed(name);
}

} // namespace picsel
