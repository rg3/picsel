// QML image providers for thumbnails and full-view images.
//
// URLs look like image://thumb/<session>/<uid>/<version> (and image://full/…).
// Files are referenced by a numeric uid rather than by name, so no file name
// ever has to survive URL encoding. Providers run on QML loader threads, so
// they only touch the thread-safe caches and the mutex-protected uid map.
#pragma once

#include "ImageCache.h"

#include <QHash>
#include <QMutex>
#include <QQuickImageProvider>
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

class CacheImageProvider : public QQuickImageProvider {
public:
    enum class Kind { Thumb, Full };
    explicit CacheImageProvider(Kind kind);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    Kind m_kind;
};

} // namespace picsel
