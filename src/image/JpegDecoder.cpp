// SPDX-License-Identifier: GPL-3.0-or-later
#include "JpegDecoder.h"

#include <QFile>
#include <QImageReader>
#include <QTransform>

#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <jpeglib.h>

namespace picsel {

namespace {

struct ErrorMgr {
    jpeg_error_mgr pub;
    std::jmp_buf jump;
};

[[noreturn]] void onError(j_common_ptr cinfo) {
    auto *err = reinterpret_cast<ErrorMgr *>(cinfo->err);
    std::longjmp(err->jump, 1);
}

void onMessage(j_common_ptr) {}
void onEmit(j_common_ptr, int) {}

quint16 rd16(const uchar *p, bool le) {
    return le ? quint16(p[0] | (p[1] << 8)) : quint16((p[0] << 8) | p[1]);
}
quint32 rd32(const uchar *p, bool le) {
    return le ? quint32(p[0] | (p[1] << 8) | (p[2] << 16) | (quint32(p[3]) << 24))
              : quint32((quint32(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

QSize scaledToFit(QSize src, QSize box) {
    if (box.isEmpty() || (src.width() <= box.width() && src.height() <= box.height()))
        return src;
    return src.scaled(box, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
}

std::optional<DecodedImage> decodeWithQt(const QString &path, QSize box) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize full = reader.size();
    if (full.isValid()) {
        // reader.size() is unoriented; swap for 90-degree transforms.
        if (reader.transformation() & QImageIOHandler::TransformationRotate90)
            full.transpose();
        const QSize fit = scaledToFit(full, box);
        if (fit != full) {
            QSize readSize = fit;
            if (reader.transformation() & QImageIOHandler::TransformationRotate90)
                readSize.transpose();
            reader.setScaledSize(readSize);
        }
    }
    QImage img = reader.read();
    if (img.isNull())
        return std::nullopt;
    if (!full.isValid())
        full = img.size();
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(img.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied
                                                        : QImage::Format_RGB32);
    const QSize fit = scaledToFit(img.size(), box);
    if (fit != img.size())
        img = img.scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return DecodedImage{std::move(img), full, 1};
}

} // namespace

int parseExifOrientation(const uchar *p, size_t len) {
    if (len < 14 || memcmp(p, "Exif\0\0", 6) != 0)
        return 1;
    const uchar *tiff = p + 6;
    const size_t tlen = len - 6;
    bool le;
    if (tiff[0] == 'I' && tiff[1] == 'I')
        le = true;
    else if (tiff[0] == 'M' && tiff[1] == 'M')
        le = false;
    else
        return 1;
    if (rd16(tiff + 2, le) != 42)
        return 1;
    const size_t ifd = rd32(tiff + 4, le);
    if (ifd + 2 > tlen) // size_t: no wrap-around for offsets near 4 GB
        return 1;
    const quint16 count = rd16(tiff + ifd, le);
    for (quint32 i = 0; i < count; ++i) {
        const size_t e = ifd + 2 + size_t(i) * 12;
        if (e + 12 > tlen)
            return 1;
        if (rd16(tiff + e, le) == 0x0112) {
            const quint16 type = rd16(tiff + e + 2, le);
            const int v = type == 3 ? rd16(tiff + e + 8, le) : 1; // SHORT
            return (v >= 1 && v <= 8) ? v : 1;
        }
    }
    return 1;
}

QImage applyOrientation(const QImage &img, int o) {
    // Same mapping as Qt's own JPEG plugin (exif -> QImageIOHandler transform).
    switch (o) {
    case 2: return img.flipped(Qt::Horizontal);
    case 3: return img.flipped(Qt::Horizontal | Qt::Vertical);
    case 4: return img.flipped(Qt::Vertical);
    case 5: return img.flipped(Qt::Vertical).transformed(QTransform().rotate(90));
    case 6: return img.transformed(QTransform().rotate(90));
    case 7: return img.flipped(Qt::Horizontal).transformed(QTransform().rotate(90));
    case 8: return img.transformed(QTransform().rotate(270));
    default: return img;
    }
}

int chooseScaleDenom(QSize src, QSize box) {
    if (box.isEmpty() || src.isEmpty())
        return 1;
    const double s = std::min(double(box.width()) / src.width(), double(box.height()) / src.height());
    if (s >= 1.0)
        return 1;
    for (int d : {8, 4, 2}) {
        // libjpeg rounds scaled dimensions up.
        const int w = (src.width() + d - 1) / d;
        const int h = (src.height() + d - 1) / d;
        if (w >= int(src.width() * s) && h >= int(src.height() * s))
            return d;
    }
    return 1;
}

std::optional<DecodedImage> decodeJpegData(const uchar *data, size_t len, QSize box, DecodeQuality q) {
    jpeg_decompress_struct cinfo;
    ErrorMgr err;
    cinfo.err = jpeg_std_error(&err.pub);
    err.pub.error_exit = onError;
    err.pub.output_message = onMessage;
    err.pub.emit_message = onEmit;

    // Declared before setjmp so a longjmp doesn't skip their lifetimes.
    QImage img;
    int orientation = 1;

    if (setjmp(err.jump)) {
        jpeg_destroy_decompress(&cinfo);
        return std::nullopt;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data, static_cast<unsigned long>(len));
    jpeg_save_markers(&cinfo, JPEG_APP0 + 1, 0xFFFF);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return std::nullopt;
    }
    if (cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK) {
        jpeg_destroy_decompress(&cinfo);
        return std::nullopt; // let Qt handle CMYK
    }
    for (jpeg_saved_marker_ptr m = cinfo.marker_list; m; m = m->next) {
        if (m->marker == JPEG_APP0 + 1) {
            orientation = parseExifOrientation(m->data, m->data_length);
            if (orientation != 1)
                break;
        }
    }
    const bool swap = orientation >= 5;
    QSize src(int(cinfo.image_width), int(cinfo.image_height));
    QSize unorientedBox = box;
    if (swap)
        unorientedBox.transpose();

    cinfo.scale_num = 1;
    cinfo.scale_denom = unsigned(chooseScaleDenom(src, unorientedBox));
    cinfo.out_color_space = JCS_EXT_BGRX; // == QImage::Format_RGB32 on little endian
    if (q == DecodeQuality::Fast) {
        cinfo.dct_method = JDCT_IFAST;
        cinfo.do_fancy_upsampling = FALSE;
    } else {
        cinfo.dct_method = JDCT_ISLOW;
        cinfo.do_fancy_upsampling = TRUE;
    }
    jpeg_start_decompress(&cinfo);

    img = QImage(int(cinfo.output_width), int(cinfo.output_height), QImage::Format_RGB32);
    if (img.isNull()) {
        jpeg_destroy_decompress(&cinfo);
        return std::nullopt;
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = img.scanLine(int(cinfo.output_scanline));
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    QSize fit = scaledToFit(src, unorientedBox);
    if (fit != img.size()) {
        img = img.scaled(fit, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    img = applyOrientation(img, orientation);
    if (swap)
        src.transpose();
    return DecodedImage{std::move(img), src, orientation};
}

std::optional<DecodedImage> decodeImage(const QString &path, QSize box, DecodeQuality q) {
    // Read into memory rather than mmap: if another program truncates the
    // file mid-decode, a mapping would kill the process with SIGBUS.
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        char magic[2];
        if (f.peek(magic, 2) == 2 && uchar(magic[0]) == 0xFF && uchar(magic[1]) == 0xD8) {
            const QByteArray data = f.readAll();
            if (auto r = decodeJpegData(reinterpret_cast<const uchar *>(data.constData()),
                                        size_t(data.size()), box, q))
                return r;
        }
    }
    return decodeWithQt(path, box);
}

} // namespace picsel
