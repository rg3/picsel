// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Similarity.h"
#include "core/Sorting.h"

#include <QPainter>
#include <QTest>

using namespace picsel;

namespace {
QImage scene(int shift, QColor sky = QColor(80, 140, 220)) {
    QImage img(600, 400, QImage::Format_RGB32);
    img.fill(sky);
    QPainter p(&img);
    p.fillRect(0, 260, 600, 140, QColor(60, 120, 40));
    p.setBrush(QColor(240, 210, 60));
    p.drawEllipse(QPoint(150 + shift, 100), 50, 50);
    p.fillRect(380 + shift, 150, 120, 150, QColor(120, 60, 40));
    return img;
}
} // namespace

class TestSimilarity : public QObject {
    Q_OBJECT
private slots:
    void nearIdenticalImagesHashClose() {
        const quint64 a = dHash(scene(0));
        const quint64 b = dHash(scene(6));
        const quint64 c = dHash(scene(0).scaled(300, 200));
        QVERIFY(hammingDistance(a, b) <= 10);
        QVERIFY(hammingDistance(a, c) <= 4);
    }

    void differentImagesHashFar() {
        // Coarse structure: dHash averages fine textures away to flat grey.
        QImage other(600, 400, QImage::Format_RGB32);
        for (int y = 0; y < 400; ++y)
            for (int x = 0; x < 600; ++x) {
                const int v = ((x / 50) * 37 + (y / 40) * 91) % 256;
                other.setPixel(x, y, qRgb(v, 255 - v, (v * 3) % 256));
            }
        QVERIFY(hammingDistance(dHash(scene(0)), dHash(other)) > 20);
        QVERIFY(hammingDistance(dHash(scene(0)), dHash(scene(0).flipped(Qt::Horizontal))) > 15);
    }

    void groupsConsecutiveRuns() {
        const quint64 h = dHash(scene(0));
        const quint64 far = ~h;
        QVector<SimilarityInput> in{
            {h, 0}, {h, 2000}, {h, 3000},  // group 0
            {far, 4000},                   // alone
            {h, 5000},                     // differs from previous
            {h, 60000},                    // same look but 55 s later
            {h ^ 0x3, 61000},              // close: group 1 with previous
            {std::nullopt, 62000},         // no hash yet
        };
        const QVector<int> g = similarityGroups(in, 8, 10);
        QCOMPARE(g, (QVector<int>{0, 0, 0, -1, -1, 1, 1, -1}));
    }

    void unknownTimesUseHashOnly() {
        const quint64 h = 0x1234;
        QVector<SimilarityInput> in{{h, kNoTime}, {h, 5000}};
        QCOMPARE(similarityGroups(in, 2, 1), (QVector<int>{0, 0}));
    }
};

QTEST_GUILESS_MAIN(TestSimilarity)
#include "tst_similarity.moc"
