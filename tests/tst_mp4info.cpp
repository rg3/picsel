#include "core/Mp4Info.h"
#include "videofixtures.h"

#include <QFile>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace picsel;

namespace {

// Offset of the first top-level box of |type|, or -1.
qint64 topLevelOffset(const QString &path, const char *type) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    const QByteArray d = f.readAll();
    qint64 pos = 0;
    while (pos + 8 <= d.size()) {
        const auto *p = reinterpret_cast<const uchar *>(d.constData() + pos);
        quint64 size = (quint64(p[0]) << 24) | (quint64(p[1]) << 16) | (quint64(p[2]) << 8) | p[3];
        if (d.mid(pos + 4, 4) == QByteArray(type))
            return pos;
        if (size == 1) {
            size = 0;
            for (int i = 0; i < 8; ++i)
                size = (size << 8) | p[8 + i];
        }
        if (size < 8)
            return -1;
        pos += qint64(size);
    }
    return -1;
}

QByteArray box(const char *type, const QByteArray &payload) {
    QByteArray b;
    const quint32 sz = quint32(payload.size() + 8);
    b.append(char(sz >> 24)).append(char(sz >> 16)).append(char(sz >> 8)).append(char(sz));
    return b + QByteArray(type, 4) + payload;
}

QByteArray be32(quint32 v) {
    QByteArray b;
    return b.append(char(v >> 24)).append(char(v >> 16)).append(char(v >> 8)).append(char(v));
}

} // namespace

class TestMp4Info : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    bool m_haveFfmpeg = false;

    QString make(const QString &name, const fixtures::Spec &spec) {
        const QString path = m_dir.filePath(name);
        if (QFile::exists(path))
            return path;
        QString err;
        if (!fixtures::makeVideo(path, spec, &err)) {
            qWarning("ffmpeg failed: %s", qPrintable(err));
            return {};
        }
        return path;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        m_haveFfmpeg = !fixtures::ffmpeg().isEmpty();
    }

    void basic_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("faststart");
        QTest::addRow("moov-at-end") << "end.mp4" << false;
        QTest::addRow("moov-first") << "first.mp4" << true;
        QTest::addRow("mov") << "camera.mov" << false;
    }

    void basic() {
        if (!m_haveFfmpeg)
            QSKIP("ffmpeg not found");
        QFETCH(QString, name);
        QFETCH(bool, faststart);
        fixtures::Spec spec;
        spec.faststart = faststart;
        const QString path = make(name, spec);
        QVERIFY(!path.isEmpty());

        const qint64 moov = topLevelOffset(path, "moov"), mdat = topLevelOffset(path, "mdat");
        QVERIFY(moov >= 0 && mdat >= 0);
        QCOMPARE(moov < mdat, faststart);

        const auto info = readMp4Info(path);
        QVERIFY(info.has_value());
        QVERIFY2(std::abs(info->durationMs - 2500) <= 100, qPrintable(QString::number(info->durationMs)));
        QCOMPARE(info->size, QSize(320, 240));
        QCOMPARE(info->rotation, 0);
        const QDateTime expected(QDate(2026, 11, 21), QTime(17, 30), QTimeZone::UTC);
        QCOMPARE(info->creationUtc, expected);
        QCOMPARE(info->creationUtc.timeSpec(), Qt::UTC);
        QCOMPARE(info->creationLocal, expected.toLocalTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")));
    }

    void rotated_data() {
        QTest::addColumn<int>("ccw");     // ffmpeg -display_rotation
        QTest::addColumn<int>("rotation"); // expected clockwise rotation
        QTest::addRow("phone-portrait") << -90 << 90;
        QTest::addRow("upside-down") << 180 << 180;
        QTest::addRow("ccw90") << 90 << 270;
    }

    void rotated() {
        if (!m_haveFfmpeg)
            QSKIP("ffmpeg not found");
        QFETCH(int, ccw);
        QFETCH(int, rotation);
        fixtures::Spec spec;
        spec.displayRotationCcw = ccw;
        const QString path = make(QStringLiteral("rot%1.mp4").arg(rotation), spec);
        QVERIFY(!path.isEmpty());
        const auto info = readMp4Info(path);
        QVERIFY(info.has_value());
        QCOMPARE(info->rotation, rotation);
        QCOMPARE(info->size, rotation == 180 ? QSize(320, 240) : QSize(240, 320));
    }

    void noCreationTime() {
        if (!m_haveFfmpeg)
            QSKIP("ffmpeg not found");
        fixtures::Spec spec;
        spec.creation.clear();
        const QString path = make(QStringLiteral("nodate.mp4"), spec);
        QVERIFY(!path.isEmpty());
        const auto info = readMp4Info(path);
        QVERIFY(info.has_value());
        QVERIFY(!info->creationUtc.isValid());
        QVERIFY(info->creationLocal.isEmpty());
        QVERIFY(info->durationMs > 0);
    }

    void videoOnlyAndLargerFrame() {
        if (!m_haveFfmpeg)
            QSKIP("ffmpeg not found");
        fixtures::Spec spec;
        spec.audio = false;
        spec.size = QStringLiteral("1280x720");
        spec.seconds = 1;
        const QString path = make(QStringLiteral("hd.mp4"), spec);
        QVERIFY(!path.isEmpty());
        const auto info = readMp4Info(path);
        QVERIFY(info.has_value());
        QCOMPARE(info->size, QSize(1280, 720));
        QVERIFY(std::abs(info->durationMs - 1000) <= 100);
    }

    void truncated() {
        if (!m_haveFfmpeg)
            QSKIP("ffmpeg not found");
        for (bool faststart : {false, true}) {
            fixtures::Spec spec;
            spec.faststart = faststart;
            const QString path = make(faststart ? QStringLiteral("first.mp4") : QStringLiteral("end.mp4"), spec);
            QVERIFY(!path.isEmpty());
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray all = f.readAll();
            const QString cut = m_dir.filePath(QStringLiteral("cut.mp4"));
            // Every prefix length in coarse steps, plus every byte near the start.
            for (qint64 len = 0; len < all.size(); len += (len < 2048 ? 1 : 997)) {
                QFile o(cut);
                QVERIFY(o.open(QIODevice::WriteOnly | QIODevice::Truncate));
                o.write(all.left(len));
                o.close();
                const auto info = readMp4Info(cut);
                if (info) { // whatever it returns must be sane
                    QVERIFY(info->durationMs >= -1);
                    QVERIFY(info->rotation % 90 == 0 && info->rotation >= 0 && info->rotation < 360);
                    QVERIFY(info->size.isEmpty() || info->size.isValid());
                }
            }
        }
    }

    void garbage() {
        const QString path = m_dir.filePath(QStringLiteral("garbage.mp4"));
        QRandomGenerator rng(1234);
        for (int round = 0; round < 300; ++round) {
            QByteArray d(int(rng.bounded(1, 4096)), Qt::Uninitialized);
            for (auto &c : d)
                c = char(rng.bounded(256));
            if (round % 3 == 1) // plausible top-level structure with a random moov
                d = box("ftyp", QByteArray("isom\0\0\0\0", 8)) + box("moov", d);
            else if (round % 3 == 2) // moov/mvhd that lies about sizes
                d = box("moov", box("mvhd", d.left(10)) + be32(0xfffffff0) + "trak" + d);
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(d);
            f.close();
            const auto info = readMp4Info(path);
            if (info)
                QVERIFY(info->rotation % 90 == 0 && info->rotation >= 0);
        }
        QVERIFY(!readMp4Info(m_dir.filePath(QStringLiteral("does-not-exist.mp4"))).has_value());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("this is plain text, not a video");
        f.close();
        QVERIFY(!readMp4Info(path).has_value());
    }

    void handcrafted() {
        // mvhd v1 with a 64-bit largesize moov, size==0 mdat at the end.
        QByteArray mvhd = QByteArray(1, '\x01') + QByteArray(3, 0);
        const quint64 created = 2082844800ULL + 1700000000ULL; // 2023-11-14T22:13:20Z
        for (int i = 7; i >= 0; --i)
            mvhd.append(char(created >> (8 * i)));
        mvhd += QByteArray(8, 0);           // modification
        mvhd += be32(600);                  // timescale
        mvhd += QByteArray(4, 0) + be32(3000); // duration = 5 s
        mvhd += QByteArray(80, 0);
        const QByteArray payload = box("mvhd", mvhd);
        QByteArray moov;
        const quint64 sz = quint64(payload.size()) + 16;
        moov += be32(1) + "moov";
        for (int i = 7; i >= 0; --i)
            moov.append(char(sz >> (8 * i)));
        moov += payload;
        const QByteArray file = box("ftyp", QByteArray("qt  \0\0\0\0", 8)) + moov + be32(0) + "mdat" + QByteArray(100, 'x');
        const QString path = m_dir.filePath(QStringLiteral("crafted.mov"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(file);
        f.close();
        const auto info = readMp4Info(path);
        QVERIFY(info.has_value());
        QCOMPARE(info->durationMs, 5000);
        QCOMPARE(info->creationUtc, QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC));
        QVERIFY(!info->size.isValid());
    }

    void matrix() {
        constexpr qint32 one = 0x10000;
        QCOMPARE(rotationFromMatrix(one, 0, 0, one), 0);
        QCOMPARE(rotationFromMatrix(0, one, -one, 0), 90);
        QCOMPARE(rotationFromMatrix(-one, 0, 0, -one), 180);
        QCOMPARE(rotationFromMatrix(0, -one, one, 0), 270);
        QCOMPARE(rotationFromMatrix(0, 0, 0, 0), 0);
    }
};

QTEST_GUILESS_MAIN(TestMp4Info)
#include "tst_mp4info.moc"
