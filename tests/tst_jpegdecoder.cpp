#include "image/JpegDecoder.h"

#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QTest>

using namespace picsel;

namespace {

// A 64x40 image with distinct quadrant colours, so orientation errors show.
QImage pattern() {
    QImage img(64, 40, QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixel(x, y, x < 32 ? (y < 20 ? 0xffff0000 : 0xff00ff00)
                                      : (y < 20 ? 0xff0000ff : 0xffffffff));
    return img;
}

// Inserts an APP1 Exif segment with only the orientation tag after SOI.
QByteArray withOrientation(const QByteArray &jpeg, int orientation) {
    QByteArray tiff("MM\0\x2a\0\0\0\x08", 8);
    tiff += QByteArray("\0\x01", 2);                                  // 1 entry
    tiff += QByteArray("\x01\x12\0\x03\0\0\0\x01", 8);               // tag, SHORT, count 1
    tiff += char(0); tiff += char(orientation); tiff += QByteArray(2, 0);
    tiff += QByteArray(4, 0);                                         // next IFD
    QByteArray payload = QByteArray("Exif\0\0", 6) + tiff;
    QByteArray seg("\xff\xe1", 2);
    const int len = int(payload.size()) + 2;
    seg += char(len >> 8); seg += char(len & 0xff);
    seg += payload;
    return jpeg.left(2) + seg + jpeg.mid(2);
}

QByteArray encode(const QImage &img) {
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    QImageWriter w(&buf, "jpeg");
    w.setQuality(100);
    w.write(img);
    return out;
}

bool roughlyEqual(const QImage &a, const QImage &b) {
    if (a.size() != b.size())
        return false;
    for (int y = 2; y < a.height(); y += 7)
        for (int x = 2; x < a.width(); x += 7) {
            const QColor ca = a.pixelColor(x, y), cb = b.pixelColor(x, y);
            if (std::abs(ca.red() - cb.red()) > 40 || std::abs(ca.green() - cb.green()) > 40
                || std::abs(ca.blue() - cb.blue()) > 40)
                return false;
        }
    return true;
}

} // namespace

class TestJpegDecoder : public QObject {
    Q_OBJECT
private slots:
    void orientationMatchesQt_data() {
        QTest::addColumn<int>("orientation");
        for (int o = 1; o <= 8; ++o)
            QTest::addRow("o%d", o) << o;
    }

    void orientationMatchesQt() {
        QFETCH(int, orientation);
        const QByteArray jpeg = withOrientation(encode(pattern()), orientation);

        QBuffer buf;
        buf.setData(jpeg);
        QImageReader reader(&buf, "jpeg");
        reader.setAutoTransform(true);
        const QImage expected = reader.read().convertToFormat(QImage::Format_RGB32);
        QVERIFY(!expected.isNull());

        auto got = decodeJpegData(reinterpret_cast<const uchar *>(jpeg.constData()), size_t(jpeg.size()),
                                  QSize(), DecodeQuality::High);
        QVERIFY(got.has_value());
        QCOMPARE(got->orientation, orientation);
        QCOMPARE(got->fullSize, expected.size());
        QVERIFY(roughlyEqual(got->image, expected));
    }

    void scalesToFitBox() {
        QImage big(1200, 800, QImage::Format_RGB32);
        big.fill(Qt::darkCyan);
        const QByteArray jpeg = encode(big);
        auto got = decodeJpegData(reinterpret_cast<const uchar *>(jpeg.constData()), size_t(jpeg.size()),
                                  QSize(300, 300), DecodeQuality::Fast);
        QVERIFY(got.has_value());
        QCOMPARE(got->image.size(), QSize(300, 200));
        QCOMPARE(got->fullSize, QSize(1200, 800));
    }

    void neverUpscales() {
        QImage small(100, 50, QImage::Format_RGB32);
        small.fill(Qt::red);
        const QByteArray jpeg = encode(small);
        auto got = decodeJpegData(reinterpret_cast<const uchar *>(jpeg.constData()), size_t(jpeg.size()),
                                  QSize(1000, 1000), DecodeQuality::Fast);
        QVERIFY(got.has_value());
        QCOMPARE(got->image.size(), QSize(100, 50));
    }

    void chooseScale() {
        QCOMPARE(chooseScaleDenom(QSize(6000, 4000), QSize(750, 500)), 8);
        QCOMPARE(chooseScaleDenom(QSize(6000, 4000), QSize(760, 510)), 4);
        QCOMPARE(chooseScaleDenom(QSize(6000, 4000), QSize(3840, 2160)), 1);
        QCOMPARE(chooseScaleDenom(QSize(6000, 4000), QSize(2000, 2000)), 2);
        QCOMPARE(chooseScaleDenom(QSize(6000, 4000), QSize()), 1);
    }

    void rejectsGarbage() {
        const QByteArray junk("\xff\xd8 not really a jpeg", 21);
        QVERIFY(!decodeJpegData(reinterpret_cast<const uchar *>(junk.constData()), size_t(junk.size()),
                                QSize(), DecodeQuality::Fast).has_value());
    }

    void fileFallbackForNonJpeg() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("x.png"));
        pattern().save(path);
        auto got = decodeImage(path, QSize(32, 32), DecodeQuality::Fast);
        QVERIFY(got.has_value());
        QCOMPARE(got->image.size(), QSize(32, 20));
    }
};

QTEST_GUILESS_MAIN(TestJpegDecoder)
#include "tst_jpegdecoder.moc"
