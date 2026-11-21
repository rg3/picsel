// Near-duplicate detection for bursts of similar pictures.
#pragma once

#include <QImage>
#include <QVector>
#include <optional>

namespace picsel {

// 64-bit difference hash: the image reduced to 9x8 grey pixels, one bit per
// horizontally adjacent pair (left brighter than right).
quint64 dHash(const QImage &image);

inline int hammingDistance(quint64 a, quint64 b) { return __builtin_popcountll(a ^ b); }

struct SimilarityInput {
    std::optional<quint64> hash;
    qint64 timeMs; // kNoTime when unknown
};

// Groups consecutive entries (in display order) that look alike: neighbours
// whose hashes differ by at most |maxBits| and, when both times are known,
// were taken at most |maxSeconds| apart. Returns a group id per entry, -1 for
// pictures not in a group of two or more.
QVector<int> similarityGroups(const QVector<SimilarityInput> &items, int maxBits, int maxSeconds);

} // namespace picsel
