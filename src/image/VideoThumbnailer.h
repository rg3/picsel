// VideoThumbnailer: grabs a representative frame of videos with QtMultimedia.
//
// Each in-flight request uses one QMediaPlayer (reused across requests, up to
// maxConcurrent of them) with a QVideoSink and no audio output. Once the media
// is loaded the player seeks to min(1 s, duration / 2), and the first frame at
// or after that position becomes the thumbnail.
#pragma once

#include <QImage>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

class QMediaPlayer;
class QTimer;
class QVideoFrame;
class QVideoSink;

namespace picsel {

class VideoThumbnailer : public QObject {   // lives in and is used from the GUI thread
    Q_OBJECT
public:
    explicit VideoThumbnailer(QObject *parent = nullptr);
    ~VideoThumbnailer() override;

    void setMaxConcurrent(int n);              // default 2
    // Queue a request. |key| is echoed back. |box| = bounding box in device
    // pixels (fit, keep aspect, never upscale). Re-requesting a queued or
    // in-flight key replaces the old request.
    void request(const QString &key, const QString &path, QSize box);
    void prioritize(const QString &key);       // move to the front of the queue
    void cancel(const QString &key);
    void clear();                              // drop everything queued/in progress

    // Applies a QtVideo::Rotation (degrees clockwise) and mirroring to an
    // image. Exposed for tests.
    static QImage transformed(const QImage &img, int rotation, bool mirrored);

signals:
    void ready(const QString &key, const QImage &image, qint64 durationMs, QSize fullSize);
    void failed(const QString &key, const QString &reason);

private:
    struct Request {
        QString key;
        QString path;
        QSize box;
    };
    enum class Phase { Idle, Loading, Seeking, Playing, Finishing };
    struct Slot {
        QMediaPlayer *player = nullptr;
        QVideoSink *sink = nullptr;
        QTimer *timeout = nullptr;
        QTimer *nudge = nullptr; // falls back to play() if a paused seek shows nothing
        Phase phase = Phase::Idle;
        Request req;
        quint64 generation = 0;
        qint64 seekTargetMs = 0;
        bool cleanFinish = false; // finished with a frame: the player can be reused
    };

    Slot *createSlot();
    void createPlayer(Slot *s);
    void dropPlayer(Slot *s);
    void destroySlot(Slot *s);
    void start(Slot *s, Request req);
    void stop(Slot *s);
    void onStatus(Slot *s, quint64 generation);
    void onFrame(Slot *s, quint64 generation, const QVideoFrame &frame);
    void finishOk(Slot *s, const QImage &img, qint64 durationMs, QSize fullSize);
    void finishError(Slot *s, const QString &reason);
    void finish(Slot *s, bool clean, std::function<void(VideoThumbnailer *, const QString &)> notify);
    void pump();
    Slot *slotFor(const QString &key) const;

    int m_maxConcurrent = 2;
    QList<Request> m_queue;
    std::vector<std::unique_ptr<Slot>> m_slots;
    quint64 m_generation = 0;
    bool m_pumping = false;
};

} // namespace picsel
