// SPDX-License-Identifier: GPL-3.0-or-later
#include "image/ImageCache.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace picsel;

class TestImageCache : public QObject {
    Q_OBJECT

    static void writeJpeg(const QString &path, QColor color, QSize size = {400, 300}) {
        QImage img(size, QImage::Format_RGB32);
        img.fill(color);
        QVERIFY(img.save(path, "JPEG"));
    }

private slots:
    void decodesNearestFirstAndAll() {
        QTemporaryDir dir;
        QStringList names;
        for (int i = 0; i < 20; ++i) {
            names << QStringLiteral("p%1.jpg").arg(i, 2, 10, QLatin1Char('0'));
            writeJpeg(dir.filePath(names.last()), QColor::fromHsv(i * 15, 200, 200));
        }
        ImageCache::Params p;
        p.threads = 1; // deterministic order
        ImageCache cache(p);
        QSignalSpy ready(&cache, &ImageCache::ready);
        cache.setDirectory(dir.path());
        cache.setFiles(names, QVector<bool>(names.size(), true));
        cache.setFocus(10, 11);
        cache.setTargetSize(QSize(100, 100));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 20, 10000);
        QCOMPARE(ready.at(0).at(0).toString(), QStringLiteral("p10.jpg"));
        QCOMPARE(ready.at(1).at(0).toString(), QStringLiteral("p11.jpg"));
        QCOMPARE(cache.image(QStringLiteral("p00.jpg")).size(), QSize(100, 75));
        QCOMPARE(ready.at(0).at(1).toSize(), QSize(400, 300));
    }

    void removeForgetsPixelsAndRedecodes() {
        QTemporaryDir dir;
        const QString name = QStringLiteral("a.jpg");
        writeJpeg(dir.filePath(name), Qt::red);
        ImageCache cache(ImageCache::Params{});
        QSignalSpy ready(&cache, &ImageCache::ready);
        cache.setDirectory(dir.path());
        cache.setFiles({name}, {true});
        cache.setFocus(0, 0);
        cache.setTargetSize(QSize(64, 64));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(qRed(cache.image(name).pixel(5, 5)) > 200);

        // Same name, different picture: must be decoded again, not reused.
        writeJpeg(dir.filePath(name), Qt::blue);
        cache.remove(name);
        QVERIFY(cache.image(name).isNull());
        QTest::qWait(200);
        QCOMPARE(ready.size(), 1); // not decoded again until it is listed again
        cache.setFiles({name}, {true});
        QTRY_COMPARE(ready.size(), 2);
        QVERIFY(qBlue(cache.image(name).pixel(5, 5)) > 200);
    }

    void settledImageBoostsWithoutBlocking() {
        QTemporaryDir dir;
        writeJpeg(dir.filePath("a.jpg"), Qt::red);
        writeJpeg(dir.filePath("b.jpg"), Qt::green);
        ImageCache::Params p;
        p.maxDistance = 0; // only the focus is decoded unless boosted
        ImageCache cache(p);
        QSignalSpy ready(&cache, &ImageCache::ready);
        cache.setDirectory(dir.path());
        cache.setFiles({"a.jpg", "b.jpg"}, {true, true});
        cache.setFocus(0, 0);
        cache.setTargetSize(QSize(64, 64));
        QTRY_COMPARE(ready.size(), 1);
        QTest::qWait(100);
        QCOMPARE(ready.size(), 1); // b.jpg is out of range

        QVERIFY(!cache.settledImage("b.jpg")); // not decoded yet: boosted instead
        QTRY_COMPARE(ready.size(), 2);
        QCOMPARE(ready.at(1).at(0).toString(), QStringLiteral("b.jpg"));
        const std::optional<QImage> b = cache.settledImage("b.jpg");
        QVERIFY(b && qGreen(b->pixel(5, 5)) > 100);

        const std::optional<QImage> gone = cache.settledImage("missing.jpg");
        QVERIFY(gone && gone->isNull()); // nothing will ever come: settled
    }

    void settledImageKeepsFocusFirst() {
        // A neighbour asking first must not overtake the current picture.
        QTemporaryDir dir;
        const QStringList names{"a.jpg", "b.jpg", "c.jpg"};
        for (const QString &n : names)
            writeJpeg(dir.filePath(n), Qt::blue);
        ImageCache::Params p;
        p.threads = 1; // decode order is the pick order
        p.maxDistance = 3;
        ImageCache cache(p);
        QSignalSpy ready(&cache, &ImageCache::ready);
        cache.setDirectory(dir.path());
        cache.setFiles(names, {true, true, true});
        cache.setFocus(0, 0);
        QVERIFY(!cache.settledImage("c.jpg")); // no target size yet: nothing runs
        cache.setTargetSize(QSize(64, 64));
        QTRY_COMPARE(ready.size(), 3);
        QCOMPARE(ready.at(0).at(0).toString(), QStringLiteral("a.jpg"));
        QCOMPARE(ready.at(2).at(0).toString(), QStringLiteral("c.jpg"));
    }

    void clearRedecodesEverything() {
        QTemporaryDir dir;
        writeJpeg(dir.filePath("a.jpg"), Qt::green);
        writeJpeg(dir.filePath("b.jpg"), Qt::green);
        ImageCache cache(ImageCache::Params{});
        QSignalSpy ready(&cache, &ImageCache::ready);
        cache.setDirectory(dir.path());
        cache.setFiles({"a.jpg", "b.jpg"}, {true, true});
        cache.setFocus(0, 1);
        cache.setTargetSize(QSize(64, 64));
        QTRY_COMPARE(ready.size(), 2);
        cache.clear();
        QTRY_COMPARE(ready.size(), 4);
    }

    void budgetNeverEvictsInjectedImages() {
        QTemporaryDir dir;
        QStringList names{"v.mp4"};
        for (int i = 0; i < 6; ++i) {
            names << QStringLiteral("p%1.jpg").arg(i);
            writeJpeg(dir.filePath(names.last()), Qt::gray);
        }
        ImageCache::Params p;
        p.budgetBytes = 3 * 64 * 48 * 4 + 64 * 48 * 4; // ~3 pictures + the video frame
        ImageCache cache(p);
        QSignalSpy ready(&cache, &ImageCache::ready);
        cache.setDirectory(dir.path());
        QVector<bool> decodable(names.size(), true);
        decodable[0] = false;
        cache.setFiles(names, decodable);
        cache.setTargetSize(QSize(64, 48));
        QImage frame(64, 48, QImage::Format_RGB32);
        frame.fill(Qt::black);
        cache.setImage("v.mp4", frame);
        cache.setFocus(6, 6); // far from the video at index 0
        QTRY_VERIFY(ready.size() >= 3);
        QTest::qWait(200);
        QVERIFY(!cache.image("v.mp4").isNull());
    }
};

QTEST_GUILESS_MAIN(TestImageCache)
#include "tst_imagecache.moc"
