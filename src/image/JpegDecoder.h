// SPDX-License-Identifier: GPL-3.0-or-later
// Fast, scaled picture decoding.
//
// JPEGs go through libjpeg-turbo with DCT-domain downscaling (1/2, 1/4, 1/8),
// which is what makes 24 MP thumbnails cost ~10-20 ms. Anything libjpeg
// can't handle (CMYK, broken files, other formats) falls back to QImageReader.
#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <optional>

namespace picsel {

enum class DecodeQuality {
    Fast, // thumbnails: fast integer IDCT, plain upsampling
    High, // full view: accurate IDCT, fancy upsampling
};

struct DecodedImage {
    QImage image;     // oriented, scaled to fit the requested box (never upscaled)
    QSize fullSize;   // oriented size of the original picture
    int orientation = 1; // EXIF orientation that was applied (1..8)
};

// Decodes |path| so that the upright image fits inside |box| (device pixels).
// An empty |box| means full size.
std::optional<DecodedImage> decodeImage(const QString &path, QSize box, DecodeQuality q);

// Same, from memory (used by tests).
std::optional<DecodedImage> decodeJpegData(const uchar *data, size_t len, QSize box, DecodeQuality q);

// EXIF orientation from a raw APP1 payload ("Exif\0\0" + TIFF). Returns 1 when
// absent or malformed.
int parseExifOrientation(const uchar *app1, size_t len);

// Applies an EXIF orientation (1..8) to an image.
QImage applyOrientation(const QImage &img, int orientation);

// Largest libjpeg scale denominator (1, 2, 4, 8) that still yields an image at
// least as large as needed to fill |box| at the fit scale. |src| and |box|
// must be in the same (unoriented) frame.
int chooseScaleDenom(QSize src, QSize box);

} // namespace picsel
