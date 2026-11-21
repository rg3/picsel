// ImageCache: decodes pictures of a folder in the background, nearest to the
// user's focus first, and keeps them in RAM within a byte budget.
//
// The same class backs both the mosaic thumbnails (all threads, budget of a
// few GB, focus = visible rows) and the full view (high quality, a handful of
// screen-sized images around the current one).
#pragma once

#include "JpegDecoder.h"

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSize>
#include <QStringList>
#include <QThreadPool>
#include <QWaitCondition>

namespace picsel {

class ImageCache : public QObject {
    Q_OBJECT
public:
    struct Params {
        DecodeQuality quality = DecodeQuality::Fast;
        int threads = 0;           // 0 = QThread::idealThreadCount()
        qint64 budgetBytes = 3LL << 30;
        int maxDistance = -1;      // don't decode farther than this from focus (-1 = no limit)
        bool computeHash = false;  // emit hashed() with a dHash of each decoded image
    };

    explicit ImageCache(Params p, QObject *parent = nullptr);
    ~ImageCache() override;

    void setDirectory(const QString &dir);
    // Display order. Names not listed are dropped; cached images are kept.
    // |decodable| marks entries this cache should decode itself (pictures);
    // others (videos) only get images through setImage().
    void setFiles(const QStringList &names, const QVector<bool> &decodable);
    // Bounding box in device pixels. Changing it schedules re-decodes; the old
    // images stay available meanwhile.
    void setTargetSize(QSize box);
    QSize targetSize() const;
    // Visible index range (inclusive) and the current index.
    void setFocus(int first, int last);
    void setBudget(qint64 bytes);
    // While paused only explicit waitFor() requests are decoded.
    void setPaused(bool paused);

    // Current image (possibly a stale size) or a null image.
    QImage image(const QString &name) const;
    // Blocks until the image for |name| at the current target size is ready
    // (or failed / timed out), boosting it to the front of the queue.
    QImage waitFor(const QString &name, int timeoutMs);

    void invalidate(const QString &name);
    // Forgets |name| (file deleted, or replaced by another file of that name).
    void remove(const QString &name);
    // Forgets every image (keeps the file list).
    void clear();
    void setImage(const QString &name, const QImage &img);
    qint64 bytesUsed() const;
    int pendingCount() const;

signals:
    // Emitted from worker threads; connect with the default (auto) connection.
    void ready(const QString &name, QSize fullSize);
    void failed(const QString &name);
    void hashed(const QString &name, quint64 hash);

private:
    struct Entry {
        QImage img;
        QSize decodedFor;  // target box the image was decoded for
        bool decodable = true;
        bool inFlight = false;
        bool failed = false;
        quint32 gen = 0;   // bumped when the entry is reset; stale decodes are dropped
    };

    bool needsWork(const Entry &e) const;
    int distance(int index) const;
    void pumpLocked();
    QString pickNextLocked();
    void runJob(const QString &name, QSize box, quint64 epoch, quint32 gen);
    void resetEntryLocked(Entry &e);
    static qint64 bytesOf(const QImage &img) { return img.sizeInBytes(); }

    Params m_params;
    QThreadPool m_pool;
    mutable QMutex m_mutex;
    QWaitCondition m_cond;
    QString m_dir;
    QStringList m_order;
    QHash<QString, int> m_index;
    QHash<QString, Entry> m_entries;
    QSize m_box;
    int m_first = 0;
    int m_last = -1;
    QString m_boost;
    int m_active = 0;
    qint64 m_bytes = 0;
    quint64 m_epoch = 0; // bumped on directory change
    bool m_stopping = false;
    bool m_paused = false;
};

} // namespace picsel
