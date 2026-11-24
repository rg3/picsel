// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Sorting.h"

#include <QTest>
#include <algorithm>

using namespace picsel;

class TestSorting : public QObject {
    Q_OBJECT
private slots:
    void naturalNames() {
        QStringList names{"IMG_10.jpg", "img_2.jpg", "IMG_1.jpg", "DSC_0100.JPG", "DSC_0099.JPG"};
        std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
            return compareNames(a, b) < 0;
        });
        QCOMPARE(names, (QStringList{"DSC_0099.JPG", "DSC_0100.JPG", "IMG_1.jpg", "img_2.jpg", "IMG_10.jpg"}));
    }

    void leadingZerosAndMixed() {
        QVERIFY(compareNames("a01", "a1") != 0);
        QVERIFY(compareNames("a2b", "a10b") < 0);
        QVERIFY(compareNames("a", "a1") < 0);
        QVERIFY(compareNames("x9.jpg", "x9.jpg") == 0);
    }

    void caseOnlyDifferenceIsStillOrdered() {
        QVERIFY(compareNames("a.jpg", "A.jpg") != 0);
        QCOMPARE(compareNames("a.jpg", "A.jpg"), -compareNames("A.jpg", "a.jpg"));
    }

    void sortModeDefaultsToDate() {
        QCOMPARE(sortModeFromString(QString()), SortMode::Date); // never chosen
        QCOMPARE(sortModeFromString("bogus"), SortMode::Date);
        QCOMPARE(sortModeFromString("date"), SortMode::Date);
        QCOMPARE(sortModeFromString("name"), SortMode::Name);
    }

    void sortModeParsingIsStrict() {
        QCOMPARE(parseSortMode("name"), std::optional(SortMode::Name));
        QCOMPARE(parseSortMode("date"), std::optional(SortMode::Date));
        QVERIFY(!parseSortMode("Date"));
        QVERIFY(!parseSortMode(QString()));
        // Every mode survives a round trip through its string.
        for (SortMode m : {SortMode::Name, SortMode::Date})
            QCOMPARE(parseSortMode(sortModeToString(m)), std::optional(m));
    }

    void correctedTime() {
        CameraOffsets off{{"NIKON Z 6", 90}};
        const qint64 t0 = correctedTimeMs("2026-11-21T18:00:00", "Google Pixel 9", off);
        const qint64 t1 = correctedTimeMs("2026-11-21T18:00:00", "NIKON Z 6", off);
        QCOMPARE(t1 - t0, 90000);
        QCOMPARE(correctedTimeMs("", "x", off), kNoTime);
        QCOMPARE(correctedTimeMs("garbage", "x", off), kNoTime);
        QCOMPARE(correctedTimeMs("2026-11-21T18:00:00.250", "x", off) - correctedTimeMs("2026-11-21T18:00:00", "x", off), 250);
    }

    void dateModeUndatedLast() {
        QVector<SortKey> keys{{"c.jpg", kNoTime}, {"b.jpg", 2000}, {"a.jpg", kNoTime}, {"z.jpg", 1000}};
        std::sort(keys.begin(), keys.end(), [](const SortKey &a, const SortKey &b) {
            return sortLess(a, b, SortMode::Date);
        });
        QStringList order;
        for (const auto &k : keys)
            order << k.name;
        QCOMPARE(order, (QStringList{"z.jpg", "b.jpg", "a.jpg", "c.jpg"}));
    }
};

QTEST_GUILESS_MAIN(TestSorting)
#include "tst_sorting.moc"
