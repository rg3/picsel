#include "VideoThumbnailer.h"

#include <QMediaPlayer>
#include <QTimer>
#include <QTransform>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include <algorithm>
#include <cstring>
#include <functional>
#include <utility>

#include <dlfcn.h>
#include <link.h>

namespace picsel {

namespace {

constexpr int kTimeoutMs = 8000;
// If a seek while paused produces no acceptable frame within this time, play
// (silently: there is no audio output) until one shows up.
constexpr int kNudgeMs = 1500;
// Tolerance for frame timestamps vs the seek target (frame boundaries).
constexpr qint64 kSlackMs = 50;
// While playing, take whatever comes after this many frames, in case the
// stream's timestamps never reach the target (odd edit lists, start offsets).
constexpr int kMaxPlayedFrames = 45;

// Qt's ffmpeg backend leaves libav on its default stderr logger at info
// level, so every video prints hardware-decode fallback warnings ("No support
// for codec h264 profile 100", "Failed setup for format vaapi") that are
// harmless, and some of them are logged at error level. Keep only fatal
// messages: real failures still reach us through QMediaPlayer::errorOccurred,
// and QT_FFMPEG_DEBUG=1 brings the full log back. There are no libav headers
// to build against, so av_log_set_level is looked up in the loaded libavutil.
// Call once a QMediaPlayer exists, so the plugin (and libavutil) is loaded.
void quietenLibav() {
    static bool done = false;
    if (std::exchange(done, true) || qEnvironmentVariableIsSet("QT_FFMPEG_DEBUG"))
        return;
    void *avutil = nullptr;
    dl_iterate_phdr([](dl_phdr_info *info, size_t, void *out) {
        if (!strstr(info->dlpi_name, "/libavutil.so"))
            return 0;
        *static_cast<void **>(out) = dlopen(info->dlpi_name, RTLD_LAZY | RTLD_NOLOAD);
        return 1;
    }, &avutil);
    if (!avutil)
        return;
    if (auto setLevel = reinterpret_cast<void (*)(int)>(dlsym(avutil, "av_log_set_level")))
        setLevel(8); // AV_LOG_FATAL
    dlclose(avutil); // drops our extra reference only
}

} // namespace

VideoThumbnailer::VideoThumbnailer(QObject *parent) : QObject(parent) {}

VideoThumbnailer::~VideoThumbnailer() {
    m_queue.clear();
    for (auto &s : m_slots)
        destroySlot(s.get());
    m_slots.clear();
}

void VideoThumbnailer::setMaxConcurrent(int n) {
    m_maxConcurrent = std::max(1, n);
    pump();
}

void VideoThumbnailer::request(const QString &key, const QString &path, QSize box) {
    if (Slot *s = slotFor(key)) {
        if (s->req.path == path && s->req.box == box)
            return;
        stop(s);
    }
    Request r{key, path, box};
    auto it = std::find_if(m_queue.begin(), m_queue.end(), [&](const Request &q) { return q.key == key; });
    if (it != m_queue.end())
        *it = std::move(r);
    else
        m_queue.append(std::move(r));
    pump();
}

void VideoThumbnailer::prioritize(const QString &key) {
    auto it = std::find_if(m_queue.begin(), m_queue.end(), [&](const Request &q) { return q.key == key; });
    if (it == m_queue.end() || it == m_queue.begin())
        return;
    Request r = std::move(*it);
    m_queue.erase(it);
    m_queue.prepend(std::move(r));
}

void VideoThumbnailer::cancel(const QString &key) {
    m_queue.removeIf([&](const Request &q) { return q.key == key; });
    if (Slot *s = slotFor(key)) {
        stop(s);
        pump();
    }
}

void VideoThumbnailer::clear() {
    m_queue.clear();
    for (auto &s : m_slots)
        if (s->phase != Phase::Idle)
            stop(s.get());
}

QImage VideoThumbnailer::transformed(const QImage &img, int rotation, bool mirrored) {
    QImage out = mirrored ? img.flipped(Qt::Horizontal) : img;
    if (rotation % 360 != 0)
        out = out.transformed(QTransform().rotate(rotation));
    return out;
}

VideoThumbnailer::Slot *VideoThumbnailer::slotFor(const QString &key) const {
    for (const auto &s : m_slots)
        if (s->phase != Phase::Idle && s->req.key == key)
            return s.get();
    return nullptr;
}

VideoThumbnailer::Slot *VideoThumbnailer::createSlot() {
    auto slot = std::make_unique<Slot>();
    Slot *s = slot.get();
    s->timeout = new QTimer(this);
    s->timeout->setSingleShot(true);
    s->nudge = new QTimer(this);
    s->nudge->setSingleShot(true);
    connect(s->timeout, &QTimer::timeout, this, [this, s] {
        if (s->phase != Phase::Idle && s->phase != Phase::Finishing)
            finishError(s, QStringLiteral("timed out"));
    });
    connect(s->nudge, &QTimer::timeout, this, [this, s] {
        if (s->phase == Phase::Seeking) {
            s->phase = Phase::Playing;
            s->player->play();
        }
    });
    createPlayer(s);
    m_slots.push_back(std::move(slot));
    return s;
}

void VideoThumbnailer::createPlayer(Slot *s) {
    s->player = new QMediaPlayer(this);
    quietenLibav();
    connect(s->player, &QMediaPlayer::mediaStatusChanged, this, [this, s] { onStatus(s, s->generation); });
    connect(s->player, &QMediaPlayer::errorOccurred, this, [this, s](QMediaPlayer::Error, const QString &msg) {
        if (s->phase != Phase::Idle && s->phase != Phase::Finishing)
            finishError(s, msg.isEmpty() ? QStringLiteral("media player error") : msg);
    });
}

// Gets rid of a player whose state is uncertain (e.g. aborted while loading
// asynchronously, which can still report the aborted load later).
void VideoThumbnailer::dropPlayer(Slot *s) {
    s->player->disconnect(this);
    s->player->setVideoSink(nullptr);
    s->player->deleteLater();
    s->player = nullptr;
}

void VideoThumbnailer::destroySlot(Slot *s) {
    stop(s);
    s->timeout->disconnect(this);
    s->nudge->disconnect(this);
    if (s->player)
        dropPlayer(s);
    delete s->timeout;
    delete s->nudge;
}

void VideoThumbnailer::start(Slot *s, Request req) {
    if (!s->player)
        createPlayer(s);
    s->req = std::move(req);
    s->generation = ++m_generation;
    s->phase = Phase::Loading;
    s->seekTargetMs = 0;

    // A fresh sink per request: frames still queued for the previous one die
    // with it (the connection's context object), and the generation check
    // catches anything else.
    s->sink = new QVideoSink(this);
    const quint64 gen = s->generation;
    int played = 0;
    connect(s->sink, &QVideoSink::videoFrameChanged, s->sink, [this, s, gen, played](const QVideoFrame &f) mutable {
        if (s->generation != gen)
            return;
        if (s->phase == Phase::Playing && ++played >= kMaxPlayedFrames)
            s->seekTargetMs = -1; // accept anything from now on
        onFrame(s, gen, f);
    });
    s->player->setVideoSink(s->sink);
    s->timeout->start(kTimeoutMs);
    s->player->setSource(QUrl::fromLocalFile(s->req.path));
    // setSource() may have failed synchronously; onStatus() handles the rest.
    if (s->generation == gen && s->phase == Phase::Loading)
        onStatus(s, gen);
}

void VideoThumbnailer::stop(Slot *s) {
    if (s->phase == Phase::Idle && !s->sink)
        return;
    const bool clean = s->phase == Phase::Finishing && s->cleanFinish;
    s->phase = Phase::Idle;
    s->generation = 0;
    s->cleanFinish = false;
    s->timeout->stop();
    s->nudge->stop();
    if (s->sink) {
        s->sink->disconnect();
        s->sink->deleteLater();
        s->sink = nullptr;
    }
    s->req = Request();
    if (!s->player)
        return;
    if (clean) {
        // Loaded and paused/playing a frame: safe to reuse.
        s->player->stop();
        s->player->setVideoSink(nullptr);
        s->player->setSource(QUrl());
    } else {
        dropPlayer(s); // recreated on the next start()
    }
}

void VideoThumbnailer::onStatus(Slot *s, quint64 generation) {
    if (s->phase == Phase::Idle || s->phase == Phase::Finishing || s->generation != generation)
        return;
    switch (s->player->mediaStatus()) {
    case QMediaPlayer::LoadedMedia:
        if (s->phase != Phase::Loading)
            return;
        if (!s->player->hasVideo()) {
            finishError(s, QStringLiteral("no video stream"));
            return;
        }
        {
            const qint64 d = s->player->duration();
            s->seekTargetMs = d < 200 ? 0 : std::min<qint64>(1000, d / 2);
        }
        s->phase = Phase::Seeking;
        s->nudge->start(kNudgeMs);
        if (s->seekTargetMs > 0)
            s->player->setPosition(s->seekTargetMs);
        s->player->pause();
        break;
    case QMediaPlayer::InvalidMedia:
        finishError(s, s->player->errorString().isEmpty() ? QStringLiteral("invalid media")
                                                          : s->player->errorString());
        break;
    case QMediaPlayer::EndOfMedia:
        if (s->phase == Phase::Seeking || s->phase == Phase::Playing)
            finishError(s, QStringLiteral("no frame before end of media"));
        break;
    default:
        break;
    }
}

void VideoThumbnailer::onFrame(Slot *s, quint64 generation, const QVideoFrame &frame) {
    if (s->generation != generation || (s->phase != Phase::Seeking && s->phase != Phase::Playing))
        return;
    if (!frame.isValid())
        return;
    if (s->seekTargetMs > 0 && frame.startTime() >= 0
        && std::max(frame.startTime(), frame.endTime()) / 1000 + kSlackMs < s->seekTargetMs)
        return; // a frame from before the seek

    // The ffmpeg backend puts the stream's display matrix into
    // surfaceFormat().rotation() (frame.rotation() stays 0), and in Qt 6.11
    // toImage() applies both, plus mirroring, so the image is upright. Should
    // a backend/version not do so, the size gives it away for 90/270.
    QImage img = frame.toImage();
    if (img.isNull()) {
        finishError(s, QStringLiteral("could not convert video frame"));
        return;
    }
    const int rotation = (int(frame.rotation()) + int(frame.surfaceFormat().rotation())) % 360;
    if ((rotation == 90 || rotation == 270) && img.size() == frame.size() && img.width() != img.height())
        img = transformed(img, rotation, false);
    const QSize fullSize = img.size();
    const QSize box = s->req.box;
    if (box.isValid() && !box.isEmpty() && (img.width() > box.width() || img.height() > box.height()))
        img = img.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    img = img.convertToFormat(img.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied
                                                    : QImage::Format_RGB32);
    finishOk(s, img, s->player->duration(), fullSize);
}

// Finishing is deferred to the event loop: the player must not be torn down or
// handed a new source from within one of its own signal emissions (it would
// go on to emit stale status for the old media), and user slots connected to
// ready()/failed() may call back into us.
void VideoThumbnailer::finishOk(Slot *s, const QImage &img, qint64 durationMs, QSize fullSize) {
    finish(s, true, [img, durationMs, fullSize](VideoThumbnailer *t, const QString &key) {
        emit t->ready(key, img, durationMs, fullSize);
    });
}

void VideoThumbnailer::finishError(Slot *s, const QString &reason) {
    finish(s, false, [reason](VideoThumbnailer *t, const QString &key) { emit t->failed(key, reason); });
}

void VideoThumbnailer::finish(Slot *s, bool clean, std::function<void(VideoThumbnailer *, const QString &)> notify) {
    s->phase = Phase::Finishing;
    s->cleanFinish = clean;
    s->timeout->stop();
    s->nudge->stop();
    const quint64 gen = s->generation;
    QMetaObject::invokeMethod(this, [this, s, gen, notify = std::move(notify)] {
        // Cancelled, cleared or destroyed meanwhile?
        if (std::none_of(m_slots.begin(), m_slots.end(), [s](const auto &p) { return p.get() == s; })
            || s->generation != gen || s->phase != Phase::Finishing)
            return;
        const QString key = s->req.key;
        stop(s);
        notify(this, key);
        pump();
    }, Qt::QueuedConnection);
}

void VideoThumbnailer::pump() {
    if (m_pumping)
        return;
    m_pumping = true;
    for (;;) {
        // Drop idle players beyond the limit (after setMaxConcurrent() lowered it).
        int busy = 0;
        for (const auto &s : m_slots)
            busy += s->phase != Phase::Idle;
        for (size_t i = m_slots.size(); i-- > 0 && int(m_slots.size()) > m_maxConcurrent;) {
            if (m_slots[i]->phase == Phase::Idle) {
                destroySlot(m_slots[i].get());
                m_slots.erase(m_slots.begin() + qsizetype(i));
            }
        }
        if (m_queue.isEmpty() || busy >= m_maxConcurrent)
            break;
        Slot *free = nullptr;
        for (const auto &s : m_slots)
            if (s->phase == Phase::Idle) {
                free = s.get();
                break;
            }
        if (!free)
            free = createSlot();
        // start() may finish synchronously (and emit); the loop re-evaluates.
        start(free, m_queue.takeFirst());
    }
    m_pumping = false;
}

} // namespace picsel
