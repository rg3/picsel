// SPDX-License-Identifier: GPL-3.0-or-later
#include "Metadata.h"

#include <QFile>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <exiv2/exiv2.hpp>

namespace picsel {

void initMetadataLibrary() {
    static const bool once = [] {
        Exiv2::XmpParser::initialize();
        Exiv2::LogMsg::setLevel(Exiv2::LogMsg::mute);
        return true;
    }();
    Q_UNUSED(once);
}

QString cameraName(const QString &make, const QString &model) {
    const QString mk = make.trimmed();
    const QString md = model.trimmed();
    if (md.isEmpty())
        return mk;
    if (mk.isEmpty())
        return md;
    // "NIKON CORPORATION" + "NIKON Z 6" -> "NIKON Z 6"; "Canon" + "Canon EOS R6" -> "Canon EOS R6".
    const QString firstWord = mk.section(QLatin1Char(' '), 0, 0);
    if (md.startsWith(firstWord, Qt::CaseInsensitive))
        return md;
    return mk + QLatin1Char(' ') + md;
}

QString exifDateToIso(const QString &exifDate, const QString &subSec) {
    // EXIF: "YYYY:MM:DD HH:MM:SS"
    const QString d = exifDate.trimmed();
    if (d.size() < 19 || d[4] != QLatin1Char(':') || d[7] != QLatin1Char(':') || d.startsWith(QLatin1String("0000")))
        return {};
    QString iso = d.left(4) + QLatin1Char('-') + d.mid(5, 2) + QLatin1Char('-') + d.mid(8, 2)
        + QLatin1Char('T') + d.mid(11, 8);
    QString ss = subSec.trimmed();
    if (!ss.isEmpty() && std::all_of(ss.cbegin(), ss.cend(), [](QChar c) { return c.isDigit(); }))
        iso += QLatin1Char('.') + (ss + QStringLiteral("00")).left(3);
    return iso;
}

QString formatExposureTime(double s) {
    if (s <= 0)
        return {};
    if (s >= 1.0)
        return QString::number(s, 'g', 3) + QStringLiteral(" s");
    return QStringLiteral("1/%1 s").arg(std::lround(1.0 / s));
}

namespace {

QString str(const Exiv2::ExifData &exif, const char *key) {
    auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it == exif.end() || it->count() == 0)
        return {};
    return QString::fromStdString(it->toString()).trimmed();
}

std::optional<double> num(const Exiv2::ExifData &exif, const char *key) {
    auto it = exif.findKey(Exiv2::ExifKey(key));
    if (it == exif.end() || it->count() == 0)
        return std::nullopt;
    const Exiv2::Rational r = it->toRational(0);
    if (r.second == 0)
        return std::nullopt;
    return double(r.first) / double(r.second);
}

} // namespace

std::optional<PhotoInfo> readPhotoInfo(const QString &path) {
    try {
        auto image = Exiv2::ImageFactory::open(QFile::encodeName(path).toStdString());
        if (!image)
            return std::nullopt;
        image->readMetadata();
        const Exiv2::ExifData &exif = image->exifData();
        PhotoInfo info;
        if (exif.empty())
            return info;

        QString date = str(exif, "Exif.Photo.DateTimeOriginal");
        QString sub = str(exif, "Exif.Photo.SubSecTimeOriginal");
        if (date.isEmpty()) {
            date = str(exif, "Exif.Image.DateTime");
            sub = str(exif, "Exif.Photo.SubSecTime");
        }
        info.exifTime = exifDateToIso(date, sub);
        info.camera = cameraName(str(exif, "Exif.Image.Make"), str(exif, "Exif.Image.Model"));
        info.lens = str(exif, "Exif.Photo.LensModel");

        QStringList parts;
        if (auto t = num(exif, "Exif.Photo.ExposureTime"); t && *t > 0)
            parts << formatExposureTime(*t);
        if (auto f = num(exif, "Exif.Photo.FNumber"); f && *f > 0)
            parts << QStringLiteral("f/%1").arg(QString::number(*f, 'g', 2));
        const QString iso = str(exif, "Exif.Photo.ISOSpeedRatings");
        if (!iso.isEmpty())
            parts << QStringLiteral("ISO %1").arg(iso.section(QLatin1Char(' '), 0, 0));
        if (auto fl = num(exif, "Exif.Photo.FocalLength"); fl && *fl > 0)
            parts << QStringLiteral("%1 mm").arg(QString::number(*fl, 'g', 3));
        info.exposure = parts.join(QStringLiteral(" · "));

        const auto w = num(exif, "Exif.Photo.PixelXDimension");
        const auto h = num(exif, "Exif.Photo.PixelYDimension");
        if (w && h)
            info.size = QSize(int(*w), int(*h));
        return info;
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

} // namespace picsel
