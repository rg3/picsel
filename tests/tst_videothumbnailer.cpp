#include "image/VideoThumbnailer.h"
#include "videofixtures.h"

#include <QFile>
#include <QLoggingCategory>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace picsel;

namespace {

constexpr int kWaitMs = 30000;

// Blue frame with a red top-left quadrant that turns all green from t = 0.9 s,
// so both orientation and the seek position show in the thumbnail.
fixtures::Spec marked(const QString &size, int ccw = 0) {
    fixtures::Spec s;
    s.source = QStringLiteral("color=c=blue");
    s.size = size;
    s.filter = QStringLiteral("drawbox=x=0:y=0:w=iw/2:h=ih/2:color=red:t=fill,"
                              "drawbox=x=0:y=0:w=iw:h=ih:color=lime:t=fill:enable='gte(t,0.9)'");
    s.displayRotationCcw = ccw;
    return s;
}

bool near(QColor c, QColor want) {
    return std::abs(c.red() - want.red()) < 60 && std::abs(c.green() - want.green()) < 60
           && std::abs(c.blue() - want.blue()) < 60;
}

} // namespace

class TestVideoThumbnailer : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_land, m_portrait, m_marked, m_markedRot, m_short, m_phone;

    QString make(const QString &name, const fixtures::Spec &spec) {
        const QString path = m_dir.filePath(name);
        QString err;
        if (!fixtures::makeVideo(path, spec, &err)) {
            qWarning("ffmpeg failed: %s", qPrintable(err));
            return {};
        }
        return path;
    }

    // Waits until |n| ready/failed signals arrived in total.
    static bool waitFor(QSignalSpy &ready, QSignalSpy &failed, int n) {
        return QTest::qWaitFor([&] { return ready.size() + failed.size() >= n; }, kWaitMs);
    }

private slots:
    void initTestCase() {
        // Qt's ffmpeg backend dumps every opened file's streams at info level.
        QLoggingCategory::setFilterRules(QStringLiteral("qt.multimedia.ffmpeg.mediadataholder.info=false"));
        QVERIFY(m_dir.isValid());
        if (fixtures::ffmpeg().isEmpty())
            QSKIP("ffmpeg not found");
        fixtures::Spec land;
        land.size = QStringLiteral("640x360");
        m_land = make(QStringLiteral("land.mp4"), land);
        fixtures::Spec portrait;
        portrait.size = QStringLiteral("640x360");
        portrait.displayRotationCcw = -90; // phone held upright: rotate 90° clockwise to display
        m_portrait = make(QStringLiteral("portrait.mp4"), portrait);
        m_marked = make(QStringLiteral("marked.mov"), marked(QStringLiteral("320x240")));
        m_markedRot = make(QStringLiteral("marked-rot.mp4"), marked(QStringLiteral("320x240"), -90));
        fixtures::Spec shortClip;
        shortClip.seconds = 0.12;
        m_short = make(QStringLiteral("short.mp4"), shortClip);
        fixtures::Spec phone; // full HD portrait, like a phone held upright
        phone.size = QStringLiteral("1920x1080");
        phone.source = QStringLiteral("testsrc2");
        phone.seconds = 3;
        phone.displayRotationCcw = -90;
        m_phone = make(QStringLiteral("PXL_VID.mp4"), phone);
        QVERIFY(!m_land.isEmpty() && !m_portrait.isEmpty() && !m_marked.isEmpty() && !m_markedRot.isEmpty()
                && !m_short.isEmpty() && !m_phone.isEmpty());
    }

    void landscape() {
        VideoThumbnailer t;
        QSignalSpy ready(&t, &VideoThumbnailer::ready), failed(&t, &VideoThumbnailer::failed);
        t.request(QStringLiteral("a"), m_land, QSize(200, 200));
        QVERIFY(waitFor(ready, failed, 1));
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().at(1).toString()));
        const auto args = ready.first();
        QCOMPARE(args.at(0).toString(), QStringLiteral("a"));
        const QImage img = args.at(1).value<QImage>();
        QCOMPARE(img.size(), QSize(200, 112)); // QImage::scaled() rounds down
        QVERIFY(img.format() == QImage::Format_RGB32 || img.format() == QImage::Format_ARGB32_Premultiplied);
        QVERIFY(std::abs(args.at(2).toLongLong() - 2500) <= 100);
        QCOMPARE(args.at(3).toSize(), QSize(640, 360));
    }

    void neverUpscales() {
        VideoThumbnailer t;
        QSignalSpy ready(&t, &VideoThumbnailer::ready), failed(&t, &VideoThumbnailer::failed);
        t.request(QStringLiteral("a"), m_land, QSize(4000, 4000));
        QVERIFY(waitFor(ready, failed, 1));
        QCOMPARE(failed.size(), 0);
        QCOMPARE(ready.first().at(1).value<QImage>().size(), QSize(640, 360));
    }

    void portrait() {
        VideoThumbnailer t;
        QSignalSpy ready(&t, &VideoThumbnailer::ready), failed(&t, &VideoThumbnailer::failed);
        t.request(QStringLiteral("p"), m_portrait, QSize(256, 256));
        QVERIFY(waitFor(ready, failed, 1));
        QCOMPARE(failed.size(), 0);
        const QImage img = ready.first().at(1).value<QImage>();
        QCOMPARE(img.size(), QSize(144, 256));
        QCOMPARE(ready.first().at(3).toSize(), QSize(360, 640));
    }

    void fullHdPortrait() {
        VideoThumbnailer t;
        QSignalSpy ready(&t, &VideoThumbnailer::ready), failed(&t, &VideoThumbnailer::failed);
        t.request(QStringLiteral("pxl"), m_phone, QSize(400, 400));
        QVERIFY(waitFor(ready, failed, 1));
        QCOMPARE(failed.size(), 0);
        QCOMPARE(ready.first().at(1).value<QImage>().size(), QSize(225, 400));
        QCOMPARE(ready.first().at(3).toSize(), QSize(1080, 1920));
        QVERIFY(std::abs(ready.first().at(2).toLongLong() - 3000) <= 100);
    }

    // Orientation of the content and seek position, not just the size.
    void content_data() {
        QTest::addColumn<bool>("rotated");
        QTest::addRow("plain") << false;
        QTest::addRow("rotated") << true;
    }

    void content() {
        QFETCH(bool, rotated);
        VideoThumbnailer t;
        QSignalSpy ready(&t, &VideoThumbnailer::ready), failed(&t, &VideoThumbnailer::failed);
        t.request(QStringLiteral("m"), rotated ? m_markedRot : m_marked, QSize());
        QVERIFY(waitFor(ready, failed, 1));
        QCOMPARE(failed.size(), 0);
        const QImage img = ready.first().at(1).value<QImage>();
        QCOMPARE(img.size(), rotated ? QSize(240, 320) : QSize(320, 240));
        // Seeked to 1 s: the frame is all green.
        QVERIFY2(near(img.pixelColor(img.width() / 4, img.height() / 4), Qt::green),
                 qPrintable(img.pixelColor(img.width() / 4, img.height() / 4).name()));

        // Same video at t=0 via a clip too short to seek in is covered by
        // shortClip(); here check orientation with the pre-seek frame layout
        // by asking for the first frame through a 0.5 s cut of the same file.
        const QString cut = m_dir.filePath(rotated ? QStringLiteral("cut-rot.mp4") : QStringLiteral("cut.mp4"));
        QString err;
        QVERIFY2(fixtures::run({QStringLiteral("-i"), rotated ? m_markedRot : m_marked, QStringLiteral("-t"),
                                QStringLiteral("0.15"), QStringLiteral("-c"), QStringLiteral("copy"), cut},
                               &err),
                 qPrintable(err));
        t.request(QStringLiteral("c"), cut, QSize());
        QVERIFY(waitFor(ready, failed, 2));
        QCOMPARE(failed.size(), 0);
        const QImage first = ready.at(1).at(1).value<QImage>();
        QCOMPARE(first.size(), img.size());
        // Stored frame: red top-left quadrant. Displayed rotated 90° clockwise,
        // the red quadrant is at the top right.
        const QPoint red = rotated ? QPoint(first.width() * 3 / 4, first.height() / 4)
                                   : QPoint(first.width() / 4, first.height() / 4);
        const QPoint blue = rotated ? QPoint(first.width() / 4, first.height() / 4)
                                    : QPoint(first.width() * 3 / 4, first.height() / 4);
        QVERIFY2(near(first.pixelColor(red), Qt::red), qPrintable(first.pixelColor(red).name()));
        QVERIFY2(near(first.pixelColor(blue), Qt::blue), qPrintable(first.pixelColor(blue).name()));
    }

    void shortClip() {
        VideoThumbnailer t;
        QSignalSpy ready(&t, &VideoThumbnailer::ready), failed(&t, &VideoThumbnailer::failed);
        t.request(QStringLiteral("s"), m_short, QSize(100, 100));
        QVERIFY(waitFor(ready, failed, 1));
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().at(1).toString()));
        QCOMPARE(ready.first().at(1).value<QImage>().size(), QSize(100, 75));
    }

    void concurrentAndQueued() {
        VideoThumbnailer t;
        t.setMaxConcurrent(2);
        QSignalSpy ready(&t, &VideoThumbnailer::ready), failed(&t, &VideoThumbnailer::failed);
        const QStringList paths{m_land, m_portrait, m_marked, m_markedRot, m_land, m_portrait, m_short};
        for (int i = 0; i < paths.size(); ++i)
            t.request(QStringLiteral("k%1").arg(i), paths[i], QSize(160, 160));
        t.prioritize(QStringLiteral("k6"));
        QVERIFY(waitFor(ready, failed, int(paths.size())));
        QTest::qWait(200);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(ready.size(), paths.size());
        QSet<QString> keys;
        for (const auto &args : ready) {
            keys.insert(args.at(0).toString());
            const QImage img = args.at(1).value<QImage>();
            QVERIFY(!img.isNull());
            QVERIFY(img.width() <= 160 && img.height() <= 160);
            QVERIFY(img.width() == 160 || img.height() == 160);
        }
        QCOMPARE(keys.size(), paths.size());
        // k6 was moved ahead of k2..k5.
        int pos6 = -1, pos5 = -1;
        for (int i = 0; i < ready.size(); ++i) {
            if (ready.at(i).at(0).toString() == QStringLiteral("k6"))
                pos6 = i;
            if (ready.at(i).at(0).toString() == QStringLiteral("k5"))
                pos5 = i;
        }
        QVERIFY(pos6 < pos5);
    }

    void reuseManyTimes() {
        VideoThumbnailer t;
        t.setMaxConcurrent(1);
        QSignalSpy ready(&t, &VideoThumbnailer::ready), failed(&t, &VideoThumbnailer::failed);
        for (int i = 0; i < 6; ++i)
            t.request(QStringLiteral("r%1").arg(i), i % 2 ? m_portrait : m_land, QSize(64, 64));
        QVERIFY(waitFor(ready, failed, 6));
        QCOMPARE(failed.size(), 0);
        for (int i = 0; i < 6; ++i) {
            QCOMPARE(ready.at(i).at(0).toString(), QStringLiteral("r%1").arg(i));
            QCOMPARE(ready.at(i).at(1).value<QImage>().size(), i % 2 ? QSize(36, 64) : QSize(64, 36));
        }
    }

    void cancelAndClear() {
        VideoThumbnailer t;
        t.setMaxConcurrent(2);
        QSignalSpy ready(&t, &VideoThumbnailer::ready), failed(&t, &VideoThumbnailer::failed);
        t.request(QStringLiteral("a"), m_land, QSize(64, 64));     // in flight
        t.request(QStringLiteral("b"), m_portrait, QSize(64, 64)); // in flight
        t.request(QStringLiteral("c"), m_marked, QSize(64, 64));   // queued
        t.request(QStringLiteral("d"), m_land, QSize(64, 64));     // queued
        t.cancel(QStringLiteral("a"));
        t.cancel(QStringLiteral("c"));
        QVERIFY(waitFor(ready, failed, 2));
        QTest::qWait(1500);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(ready.size(), 2);
        QSet<QString> got{ready.at(0).at(0).toString(), ready.at(1).at(0).toString()};
        QCOMPARE(got, (QSet<QString>{QStringLiteral("b"), QStringLiteral("d")}));

        ready.clear();
        for (int i = 0; i < 5; ++i)
            t.request(QStringLiteral("x%1").arg(i), m_land, QSize(64, 64));
        // All but the queued ones are in flight (loading) now. Waiting here
        // would let small clips finish before clear().
        t.clear();
        QTest::qWait(3000);
        QCOMPARE(ready.size(), 0);
        QCOMPARE(failed.size(), 0);

        // Still usable afterwards.
        t.request(QStringLiteral("after"), m_portrait, QSize(64, 64));
        QVERIFY(waitFor(ready, failed, 1));
        QCOMPARE(failed.size(), 0);
        QCOMPARE(ready.first().at(0).toString(), QStringLiteral("after"));
    }

    void clearFromSlot() {
        VideoThumbnailer t;
        QSignalSpy ready(&t, &VideoThumbnailer::ready), failed(&t, &VideoThumbnailer::failed);
        connect(&t, &VideoThumbnailer::ready, &t, [&t] { t.clear(); });
        for (int i = 0; i < 4; ++i)
            t.request(QStringLiteral("y%1").arg(i), m_land, QSize(64, 64));
        QVERIFY(waitFor(ready, failed, 1));
        QTest::qWait(2000);
        QCOMPARE(ready.size(), 1);
        QCOMPARE(failed.size(), 0);
    }

    void notAVideo() {
        const QString path = m_dir.filePath(QStringLiteral("fake.mp4"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'z'));
        f.close();
        VideoThumbnailer t;
        QSignalSpy ready(&t, &VideoThumbnailer::ready), failed(&t, &VideoThumbnailer::failed);
        t.request(QStringLiteral("bad"), path, QSize(64, 64));
        t.request(QStringLiteral("missing"), m_dir.filePath(QStringLiteral("nope.mp4")), QSize(64, 64));
        t.request(QStringLiteral("good"), m_land, QSize(64, 64));
        QVERIFY(waitFor(ready, failed, 3));
        QCOMPARE(failed.size(), 2);
        QCOMPARE(ready.size(), 1);
        QCOMPARE(ready.first().at(0).toString(), QStringLiteral("good"));
        QSet<QString> bad{failed.at(0).at(0).toString(), failed.at(1).at(0).toString()};
        QCOMPARE(bad, (QSet<QString>{QStringLiteral("bad"), QStringLiteral("missing")}));
    }

    void destroyWhileBusy() {
        auto *t = new VideoThumbnailer;
        for (int i = 0; i < 3; ++i)
            t->request(QStringLiteral("z%1").arg(i), m_land, QSize(64, 64));
        QTest::qWait(50);
        delete t;
        QTest::qWait(500); // nothing must crash on late backend callbacks
    }

    void transformHelper() {
        QImage img(4, 2, QImage::Format_RGB32);
        img.fill(Qt::blue);
        img.setPixel(0, 0, 0xffff0000);
        const QImage r = VideoThumbnailer::transformed(img, 90, false);
        QCOMPARE(r.size(), QSize(2, 4));
        QCOMPARE(r.pixel(1, 0), 0xffff0000u);
        const QImage m = VideoThumbnailer::transformed(img, 0, true);
        QCOMPARE(m.pixel(3, 0), 0xffff0000u);
    }
};

QTEST_MAIN(TestVideoThumbnailer)
#include "tst_videothumbnailer.moc"
