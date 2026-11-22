// SPDX-License-Identifier: GPL-3.0-or-later
#include "Mp4Info.h"

#include <QFile>
#include <QTimeZone>

#include <cmath>
#include <numbers>

namespace picsel {

namespace {

constexpr qint64 kMaxMoovBytes = 64LL << 20; // moov of a multi-hour video is a few MB
constexpr int kMaxTopLevelBoxes = 100000;

quint32 fourcc(const char (&s)[5]) {
    return (quint32(uchar(s[0])) << 24) | (quint32(uchar(s[1])) << 16) | (quint32(uchar(s[2])) << 8)
           | quint32(uchar(s[3]));
}

quint32 be32(const uchar *p) {
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

quint64 be64(const uchar *p) { return (quint64(be32(p)) << 32) | be32(p + 4); }

struct Box {
    quint32 type = 0;
    qint64 payload = 0; // offset of the payload (after the header)
    qint64 end = 0;     // offset one past the box
};

// Parses a box header at |pos| inside a buffer of |len| bytes. Boxes that
// overrun |len| are rejected.
std::optional<Box> boxAt(const uchar *data, qint64 len, qint64 pos) {
    if (pos < 0 || len - pos < 8)
        return std::nullopt;
    Box b;
    quint64 size = be32(data + pos);
    b.type = be32(data + pos + 4);
    b.payload = pos + 8;
    if (size == 1) {
        if (len - pos < 16)
            return std::nullopt;
        size = be64(data + pos + 8);
        b.payload = pos + 16;
    } else if (size == 0) {
        size = quint64(len - pos);
    }
    if (size < quint64(b.payload - pos) || size > quint64(len - pos))
        return std::nullopt;
    b.end = pos + qint64(size);
    return b;
}

// Finds the first child box of |type| in [begin, end).
std::optional<Box> findChild(const uchar *data, qint64 begin, qint64 end, quint32 type) {
    qint64 pos = begin;
    while (pos < end) {
        auto b = boxAt(data, end, pos);
        if (!b)
            return std::nullopt;
        if (b->type == type)
            return b;
        pos = b->end;
    }
    return std::nullopt;
}

struct TrackInfo {
    bool video = false;
    qint64 width = 0;  // pixels (integer part of 16.16)
    qint64 height = 0;
    int rotation = 0;
};

TrackInfo parseTrak(const uchar *data, const Box &trak) {
    TrackInfo t;
    if (auto tkhd = findChild(data, trak.payload, trak.end, fourcc("tkhd"))) {
        const uchar *p = data + tkhd->payload;
        const qint64 len = tkhd->end - tkhd->payload;
        if (len >= 4) {
            const int version = p[0];
            // version/flags, creation, modification, track_ID, reserved, duration
            const qint64 skip = 4 + (version == 1 ? 8 + 8 + 4 + 4 + 8 : 4 + 4 + 4 + 4 + 4);
            // reserved[2], layer, alternate_group, volume, reserved, matrix[9], width, height
            const qint64 need = skip + 8 + 2 + 2 + 2 + 2 + 36 + 4 + 4;
            if (version <= 1 && len >= need) {
                const uchar *m = p + skip + 16;
                const qint32 a = qint32(be32(m)), b = qint32(be32(m + 4));
                const qint32 c = qint32(be32(m + 12)), d = qint32(be32(m + 16));
                t.rotation = rotationFromMatrix(a, b, c, d);
                t.width = be32(m + 36) >> 16;
                t.height = be32(m + 40) >> 16;
            }
        }
    }
    if (auto mdia = findChild(data, trak.payload, trak.end, fourcc("mdia"))) {
        if (auto hdlr = findChild(data, mdia->payload, mdia->end, fourcc("hdlr"))) {
            // version/flags, pre_defined (QuickTime: component type), handler_type
            if (hdlr->end - hdlr->payload >= 12)
                t.video = be32(data + hdlr->payload + 8) == fourcc("vide");
        }
    }
    return t;
}

// Picks the first 'vide' track with a tkhd size, else the first track that is
// either. The structure is fixed (moov/trak/{tkhd,mdia/hdlr}), so there is no
// recursion and no depth to limit.
std::optional<TrackInfo> pickVideoTrack(const uchar *data, const Box &moov) {
    std::optional<TrackInfo> sized;
    qint64 pos = moov.payload;
    while (pos < moov.end) {
        auto b = boxAt(data, moov.end, pos);
        if (!b)
            break;
        if (b->type == fourcc("trak")) {
            const TrackInfo t = parseTrak(data, *b);
            const bool hasSize = t.width > 0 && t.height > 0;
            if (t.video && hasSize)
                return t;
            if ((t.video || hasSize) && !sized)
                sized = t; // keep looking for a 'vide' track with a size
        }
        pos = b->end;
    }
    return sized;
}

std::optional<VideoInfo> parseMoov(const QByteArray &moovBytes) {
    const auto *data = reinterpret_cast<const uchar *>(moovBytes.constData());
    const qint64 len = moovBytes.size();
    auto moov = boxAt(data, len, 0);
    if (!moov)
        return std::nullopt;
    auto mvhd = findChild(data, moov->payload, moov->end, fourcc("mvhd"));
    if (!mvhd)
        return std::nullopt;

    const uchar *p = data + mvhd->payload;
    const qint64 mlen = mvhd->end - mvhd->payload;
    if (mlen < 4)
        return std::nullopt;
    const int version = p[0];
    quint64 created = 0, duration = 0;
    quint32 timescale = 0;
    bool unknownDuration = false;
    if (version == 1) {
        if (mlen < 4 + 8 + 8 + 4 + 8)
            return std::nullopt;
        created = be64(p + 4);
        timescale = be32(p + 20);
        duration = be64(p + 24);
        unknownDuration = duration == ~quint64(0);
    } else if (version == 0) {
        if (mlen < 4 + 4 + 4 + 4 + 4)
            return std::nullopt;
        created = be32(p + 4);
        timescale = be32(p + 12);
        duration = be32(p + 16);
        unknownDuration = duration == 0xffffffffu;
    } else {
        return std::nullopt;
    }

    VideoInfo info;
    if (timescale > 0 && !unknownDuration && duration < (quint64(1) << 52))
        info.durationMs = qint64((duration * 1000 + timescale / 2) / timescale);

    // Seconds since 1904-01-01 UTC. Reject absurd values (> year ~2200).
    constexpr qint64 k1904To1970 = 2082844800;
    if (created > 0 && created < quint64(k1904To1970) + 7258118400ULL) {
        info.creationUtc = QDateTime::fromSecsSinceEpoch(qint64(created) - k1904To1970, QTimeZone::UTC);
        info.creationLocal = info.creationUtc.toLocalTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    }

    if (auto t = pickVideoTrack(data, *moov)) {
        if (t->width > 0 && t->height > 0 && t->width < 65536 && t->height < 65536) {
            QSize s(int(t->width), int(t->height));
            if (t->rotation == 90 || t->rotation == 270)
                s.transpose();
            info.size = s;
        }
        info.rotation = t->rotation;
    }
    return info;
}

} // namespace

int rotationFromMatrix(qint32 a, qint32 b, qint32 c, qint32 d) {
    if (a == 0 && b == 0 && c == 0 && d == 0)
        return 0;
    // (a, b) is the image of the x axis; for a clockwise rotation by θ in the
    // screen's y-down frame, a = cos θ and b = sin θ.
    Q_UNUSED(c);
    Q_UNUSED(d);
    const double deg = std::atan2(double(b), double(a)) * 180.0 / std::numbers::pi;
    int r = int(std::lround(deg / 90.0)) * 90;
    r = ((r % 360) + 360) % 360;
    return r;
}

std::optional<VideoInfo> readMp4Info(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const qint64 fileSize = f.size();
    qint64 pos = 0;
    for (int n = 0; n < kMaxTopLevelBoxes && fileSize - pos >= 8; ++n) {
        if (!f.seek(pos))
            return std::nullopt;
        uchar hdr[16];
        if (f.read(reinterpret_cast<char *>(hdr), 8) != 8)
            return std::nullopt;
        quint64 size = be32(hdr);
        const quint32 type = be32(hdr + 4);
        qint64 headerLen = 8;
        if (size == 1) {
            if (f.read(reinterpret_cast<char *>(hdr + 8), 8) != 8)
                return std::nullopt;
            size = be64(hdr + 8);
            headerLen = 16;
        } else if (size == 0) {
            size = quint64(fileSize - pos);
        }
        if (size < quint64(headerLen))
            return std::nullopt;
        // Truncated files: a box may claim to extend past EOF. Clamp, so a
        // moov cut short still fails cleanly in the bounds-checked parser.
        const qint64 avail = fileSize - pos;
        const qint64 boxLen = size > quint64(avail) ? avail : qint64(size);

        if (type == fourcc("moov")) {
            if (boxLen > kMaxMoovBytes)
                return std::nullopt;
            if (!f.seek(pos))
                return std::nullopt;
            QByteArray bytes = f.read(boxLen);
            if (bytes.size() != boxLen)
                return std::nullopt;
            // Normalise the header to a 32-bit/largesize-free form the
            // in-memory parser can bound-check against the bytes we have.
            if (size != quint64(boxLen) || headerLen == 16) {
                QByteArray fixed;
                const qint64 payloadLen = boxLen - headerLen;
                if (payloadLen + 8 > 0xffffffffLL)
                    return std::nullopt;
                const quint32 sz = quint32(payloadLen + 8);
                fixed.append(char(sz >> 24)).append(char(sz >> 16)).append(char(sz >> 8)).append(char(sz));
                fixed.append("moov", 4);
                fixed.append(bytes.constData() + headerLen, payloadLen);
                bytes = fixed;
            }
            return parseMoov(bytes);
        }
        if (size > quint64(avail))
            return std::nullopt; // box runs past EOF and wasn't moov
        pos += qint64(size);
    }
    return std::nullopt;
}

} // namespace picsel
