// SPDX-License-Identifier: GPL-3.0-or-later
// QML image providers for thumbnails and full-view images.
//
// URLs look like image://thumb/<session>/<uid>/<version> (and image://full/…).
// Files are referenced by a numeric uid rather than by name, so no file name
// ever has to survive URL encoding. Providers run on the engine's single
// pixmap reader thread, so they only touch the thread-safe caches and the
// mutex-protected uid map, and the full-size one never blocks it.
#pragma once

#include "ImageCache.h"

#include <QHash>
#include <QMutex>
#include <QQuickAsyncImageProvider>
#include <QQuickImageProvider>
#include <QTimer>
#include <memory>

namespace picsel {

class UidMap {
public:
    void set(quint32 uid, const QString &name) {
        QMutexLocker l(&m_mutex);
        m_map.insert(uid, name);
    }
    void remove(quint32 uid) {
        QMutexLocker l(&m_mutex);
        m_map.remove(uid);
    }
    void clear() {
        QMutexLocker l(&m_mutex);
        m_map.clear();
    }
    QString name(quint32 uid) const {
        QMutexLocker l(&m_mutex);
        return m_map.value(uid);
    }

private:
    mutable QMutex m_mutex;
    QHash<quint32, QString> m_map;
};

struct ProviderEntry {
    std::shared_ptr<ImageCache> thumbs;
    std::shared_ptr<ImageCache> full;
    std::shared_ptr<UidMap> uids;
};

void registerProviderSession(int sessionId, ProviderEntry entry);
void unregisterProviderSession(int sessionId);

// Thumbnails: usually cached already, so this returns at once. It only
// blocks (briefly) for one evicted from a very large folder.
class ThumbImageProvider : public QQuickImageProvider {
public:
    ThumbImageProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// Full-size images can take long to read (slow storage) and would hold up
// every other image load on the reader thread, so they are answered
// asynchronously.
class FullImageProvider : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};

// Lives on the reader thread, which runs an event loop; cache signals reach
// it queued, so it is never touched by two threads at once.
class FullImageResponse : public QQuickImageResponse {
    Q_OBJECT
public:
    FullImageResponse(std::weak_ptr<ImageCache> cache, const QString &name);
    QQuickTextureFactory *textureFactory() const override;
    void cancel() override;

private:
    void check();
    void finish(QImage img);

    std::weak_ptr<ImageCache> m_cache;
    QString m_name;
    QImage m_image;
    QTimer m_timeout{this}; // parented: follows the response if it changes thread
    bool m_done = false;
};

} // namespace picsel
