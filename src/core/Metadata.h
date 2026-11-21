// Photo metadata (EXIF) used for sorting and for the info panel.
#pragma once

#include <QSize>
#include <QString>
#include <optional>

namespace picsel {

struct PhotoInfo {
    QString exifTime;  // "yyyy-MM-ddTHH:mm:ss[.zzz]" camera-local time, empty if unknown
    QString camera;    // "Make Model" without duplicated make, empty if unknown
    QString lens;
    QString exposure;  // e.g. "1/250 s · f/2.8 · ISO 400 · 50 mm"
    QSize size;        // pixel size as recorded in EXIF (may be empty)
};

// Must be called once before readPhotoInfo() is used from several threads.
void initMetadataLibrary();

// Reads EXIF from a picture. Never throws; returns nullopt if unreadable.
std::optional<PhotoInfo> readPhotoInfo(const QString &path);

// Helpers exposed for tests.
QString cameraName(const QString &make, const QString &model);
QString exifDateToIso(const QString &exifDate, const QString &subSec);
QString formatExposureTime(double seconds);

} // namespace picsel
