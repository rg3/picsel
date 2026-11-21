#include "ImageProviders.h"

namespace picsel {

namespace {
QMutex g_mutex;
QHash<int, ProviderEntry> g_sessions;

std::optional<ProviderEntry> lookup(int id) {
    QMutexLocker l(&g_mutex);
    auto it = g_sessions.constFind(id);
    if (it == g_sessions.constEnd())
        return std::nullopt;
    return *it;
}
} // namespace

void registerProviderSession(int sessionId, ProviderEntry entry) {
    QMutexLocker l(&g_mutex);
    g_sessions.insert(sessionId, std::move(entry));
}

void unregisterProviderSession(int sessionId) {
    QMutexLocker l(&g_mutex);
    g_sessions.remove(sessionId);
}

CacheImageProvider::CacheImageProvider(Kind kind)
    : QQuickImageProvider(QQuickImageProvider::Image), m_kind(kind) {}

QImage CacheImageProvider::requestImage(const QString &id, QSize *size, const QSize &) {
    // id = "<session>/<uid>/<version>"
    const QStringList parts = id.split(QLatin1Char('/'));
    QImage img;
    if (parts.size() >= 2) {
        if (auto entry = lookup(parts[0].toInt())) {
            const QString name = entry->uids->name(parts[1].toUInt());
            if (!name.isEmpty()) {
                ImageCache *cache = m_kind == Kind::Thumb ? entry->thumbs.get() : entry->full.get();
                img = cache->image(name);
                if (img.isNull() || m_kind == Kind::Full)
                    img = cache->waitFor(name, m_kind == Kind::Thumb ? 3000 : 10000);
            }
        }
    }
    if (img.isNull()) {
        img = QImage(1, 1, QImage::Format_RGB32);
        img.fill(Qt::black);
    }
    if (size)
        *size = img.size();
    return img;
}

} // namespace picsel
