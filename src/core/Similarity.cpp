#include "Similarity.h"

#include "Sorting.h"

namespace picsel {

quint64 dHash(const QImage &image) {
    if (image.isNull())
        return 0;
    // Shrink in two steps: a cheap pass to ~64 px keeps the smooth pass tiny.
    QImage small = image;
    if (small.width() > 128 || small.height() > 128)
        small = small.scaled(72, 64, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    small = small.scaled(9, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_Grayscale8);
    quint64 h = 0;
    for (int y = 0; y < 8; ++y) {
        const uchar *row = small.constScanLine(y);
        for (int x = 0; x < 8; ++x)
            h = (h << 1) | (row[x] > row[x + 1] ? 1u : 0u);
    }
    return h;
}

QVector<int> similarityGroups(const QVector<SimilarityInput> &items, int maxBits, int maxSeconds) {
    const int n = int(items.size());
    QVector<int> groups(n, -1);
    auto linked = [&](int i) { // items i and i+1
        const auto &a = items[i];
        const auto &b = items[i + 1];
        if (!a.hash || !b.hash || hammingDistance(*a.hash, *b.hash) > maxBits)
            return false;
        if (a.timeMs != kNoTime && b.timeMs != kNoTime
            && std::abs(a.timeMs - b.timeMs) > qint64(maxSeconds) * 1000)
            return false;
        return true;
    };
    int next = 0;
    for (int i = 0; i + 1 < n; ++i) {
        if (!linked(i))
            continue;
        if (groups[i] < 0)
            groups[i] = next++;
        groups[i + 1] = groups[i];
    }
    return groups;
}

} // namespace picsel
