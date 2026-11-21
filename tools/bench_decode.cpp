// Times thumbnail decoding of a folder through ImageCache, the same path the
// mosaic uses.  usage: bench_decode DIR [WxH] [visibleCount]
#include "core/DirScanner.h"
#include "image/ImageCache.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <cstdio>

using namespace picsel;

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        std::fprintf(stderr, "usage: bench_decode DIR [WxH] [visibleCount]\n");
        return 2;
    }
    const QString dir = args[1];
    QSize box(750, 500);
    if (args.size() > 2) {
        const QStringList wh = args[2].split(QLatin1Char('x'));
        box = QSize(wh.value(0).toInt(), wh.value(1).toInt());
    }
    const int visible = args.size() > 3 ? args[3].toInt() : 16;

    QStringList names;
    QVector<bool> decodable;
    for (const DiskFile &f : scanDir(dir)) {
        names << f.name;
        decodable << (mediaKindForName(f.name) == MediaKind::Image);
    }
    std::sort(names.begin(), names.end());
    const int total = int(std::count(decodable.cbegin(), decodable.cend(), true));

    ImageCache::Params p;
    p.budgetBytes = 8LL << 30;
    ImageCache cache(p);
    int done = 0, failed = 0, firstScreen = 0;
    qint64 firstScreenMs = -1;
    QElapsedTimer timer;
    QObject::connect(&cache, &ImageCache::ready, &app, [&](const QString &name, QSize) {
        ++done;
        if (names.indexOf(name) < visible && ++firstScreen == std::min(visible, total))
            firstScreenMs = timer.elapsed();
        if (done + failed == total) {
            const qint64 ms = timer.elapsed();
            std::printf("%d pictures, box %dx%d, %d threads\n", total, box.width(), box.height(),
                        QThreadPool::globalInstance()->maxThreadCount());
            std::printf("first %d visible: %lld ms\n", visible, (long long)firstScreenMs);
            std::printf("all: %lld ms (%.1f ms/picture wall, %d failed), RAM %.0f MB\n", (long long)ms,
                        double(ms) / total, failed, cache.bytesUsed() / 1048576.0);
            app.quit();
        }
    });
    QObject::connect(&cache, &ImageCache::failed, &app, [&](const QString &) { ++failed; });

    timer.start();
    cache.setDirectory(dir);
    cache.setFiles(names, decodable);
    cache.setFocus(0, visible - 1);
    cache.setTargetSize(box);
    if (total == 0)
        return 0;
    return app.exec();
}
