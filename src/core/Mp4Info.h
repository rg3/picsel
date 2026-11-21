// Minimal ISO-BMFF (MP4 / QuickTime MOV) metadata reader.
//
// Walks the top-level boxes (seeking over mdat, which is never read) and
// parses moov/mvhd and the first video track's tkhd. No ffmpeg involved, so it
// is cheap enough to run for every video while scanning a folder.
#pragma once

#include <QDateTime>
#include <QSize>
#include <QString>
#include <optional>

namespace picsel {

struct VideoInfo {
    qint64 durationMs = -1;
    QDateTime creationUtc;      // invalid if absent or zero
    QString creationLocal;      // creationUtc in the system local time zone as "yyyy-MM-ddTHH:mm:ss", empty if absent
    QSize size;                 // display size of the first video track, after applying the tkhd rotation matrix (90/270 swaps w/h)
    int rotation = 0;           // 0, 90, 180, 270 (clockwise, as the legacy "rotate" tag) from the video tkhd matrix
};

// Returns nullopt when the file can't be read or has no moov/mvhd.
std::optional<VideoInfo> readMp4Info(const QString &path);

// Clockwise rotation (0, 90, 180, 270) of a tkhd/mvhd 3x3 matrix given its
// a, b, c, d entries (16.16 fixed point). Exposed for tests.
int rotationFromMatrix(qint32 a, qint32 b, qint32 c, qint32 d);

} // namespace picsel
