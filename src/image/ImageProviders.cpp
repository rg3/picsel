// SPDX-License-Identifier: GPL-3.0-or-later
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

QString providerUrl(const char *provider, int sessionId, quint32 uid, int version) {
    // The version only makes QML reload; resolve() ignores it.
    return QStringLiteral("image://%1/%2/%3/%4")
        .arg(QLatin1String(provider)).arg(sessionId).arg(uid).arg(version);
}

namespace {
// Resolves "<session>/<uid>/<version>" (see providerUrl) to a session and a file name.
std::optional<std::pair<ProviderEntry, QString>> resolve(const QString &id) {
    const QStringList parts = id.split(QLatin1Char('/'));
    if (parts.size() < 2)
        return std::nullopt;
    auto entry = lookup(parts[0].toInt());
    if (!entry)
        return std::nullopt;
    QString name = entry->uids->name(parts[1].toUInt());
    if (name.isEmpty())
        return std::nullopt;
    return std::make_pair(std::move(*entry), std::move(name));
}

// Shown when there is nothing to show, so QML never gets an error status.
QImage placeholder() {
    QImage img(1, 1, QImage::Format_RGB32);
    img.fill(Qt::black);
    return img;
}
} // namespace

ThumbImageProvider::ThumbImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage ThumbImageProvider::requestImage(const QString &id, QSize *size, const QSize &) {
    QImage img;
    if (auto r = resolve(id)) {
        img = r->first.thumbs->image(r->second);
        if (img.isNull())
            img = r->first.thumbs->waitFor(r->second, 3000);
    }
    if (img.isNull())
        img = placeholder();
    if (size)
        *size = img.size();
    return img;
}

QQuickImageResponse *FullImageProvider::requestImageResponse(const QString &id, const QSize &) {
    auto r = resolve(id);
    return new FullImageResponse(r ? r->first.full : nullptr, r ? r->second : QString());
}

FullImageResponse::FullImageResponse(std::weak_ptr<ImageCache> cache, const QString &name)
    : m_cache(std::move(cache)), m_name(name) {
    if (auto c = m_cache.lock()) {
        // Connected before the first check, so no result can slip between.
        connect(c.get(), &ImageCache::ready, this, [this](const QString &n) { if (n == m_name) check(); });
        connect(c.get(), &ImageCache::failed, this, [this](const QString &n) { if (n == m_name) check(); });
    }
    // Same limit as the old blocking wait; also covers the rare changes that
    // emit nothing (file removed, cache cleared).
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(10000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        auto c = m_cache.lock();
        finish(c ? c->image(m_name) : QImage());
    });
    m_timeout.start();
    // The engine connects to finished() only after we return.
    QMetaObject::invokeMethod(this, &FullImageResponse::check, Qt::QueuedConnection);
}

void FullImageResponse::check() {
    auto c = m_cache.lock();
    if (!c || m_name.isEmpty()) {
        finish({});
        return;
    }
    if (std::optional<QImage> img = c->settledImage(m_name))
        finish(std::move(*img));
}

void FullImageResponse::cancel() {
    // The page scrolled away: finish() stops prioritising it, and the engine
    // still needs finished() to clean up.
    finish({});
}

void FullImageResponse::finish(QImage img) {
    if (m_done)
        return;
    m_done = true;
    m_timeout.stop();
    m_image = img.isNull() ? placeholder() : std::move(img);
    if (auto c = m_cache.lock(); c && !m_name.isEmpty())
        c->unboost(m_name);
    emit finished();
}

QQuickTextureFactory *FullImageResponse::textureFactory() const {
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

} // namespace picsel
