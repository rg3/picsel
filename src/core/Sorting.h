// Display ordering of files.
#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include <limits>

namespace picsel {

enum class SortMode { Name = 0, Date = 1 };

SortMode sortModeFromString(const QString &s);
QString sortModeToString(SortMode m);

// Camera "Make Model" -> clock correction in seconds (added to EXIF time).
using CameraOffsets = QHash<QString, qint64>;

inline constexpr qint64 kNoTime = std::numeric_limits<qint64>::max();

// Parses an EXIF-style local time ("YYYY-MM-DDTHH:MM:SS[.fff]") into
// milliseconds on a naive (timezone-free) scale, applying the camera offset.
// Returns kNoTime when empty or unparsable.
qint64 correctedTimeMs(const QString &exifTime, const QString &camera, const CameraOffsets &offsets);

struct SortKey {
    QString name;
    qint64 timeMs = kNoTime;
};

// Natural, case-insensitive name comparison ("IMG_2" < "IMG_10").
int compareNames(const QString &a, const QString &b);

// Strict weak ordering for the mode. Date mode: dated files first by time
// (ties by name), then undated files by name.
bool sortLess(const SortKey &a, const SortKey &b, SortMode mode);

} // namespace picsel
