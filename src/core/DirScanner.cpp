#include "DirScanner.h"

#include <QFile>
#include <QHash>
#include <QSet>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace picsel {

MediaKind mediaKindForName(const QString &name) {
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == name.size() - 1)
        return MediaKind::None;
    const QString ext = name.mid(dot + 1).toLower();
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg") || ext == QLatin1String("jpe"))
        return MediaKind::Image;
    if (ext == QLatin1String("mp4") || ext == QLatin1String("m4v") || ext == QLatin1String("mov"))
        return MediaKind::Video;
    return MediaKind::None;
}

bool isDatabaseFile(const QString &name) {
    return name.startsWith(QLatin1String(kDbFileName));
}

static std::optional<DiskFile> fromStat(const QString &name, const struct stat &st) {
    if (!S_ISREG(st.st_mode) || !isSupportedName(name) || isDatabaseFile(name))
        return std::nullopt;
    DiskFile f;
    f.name = name;
    f.size = st.st_size;
    f.mtimeNs = qint64(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec;
    f.inode = st.st_ino;
    return f;
}

std::optional<DiskFile> statEntry(const QString &dir, const QString &name) {
    struct stat st;
    const QByteArray path = QFile::encodeName(dir + QLatin1Char('/') + name);
    if (::lstat(path.constData(), &st) != 0)
        return std::nullopt;
    return fromStat(name, st);
}

QVector<DiskFile> scanDir(const QString &dir) {
    QVector<DiskFile> out;
    const QByteArray d = QFile::encodeName(dir);
    const int dfd = ::open(d.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0)
        return out;
    DIR *dp = ::fdopendir(dfd);
    if (!dp) {
        ::close(dfd);
        return out;
    }
    while (struct dirent *e = ::readdir(dp)) {
        if (e->d_type != DT_REG && e->d_type != DT_UNKNOWN)
            continue;
        const QString name = QFile::decodeName(e->d_name);
        if (!isSupportedName(name) || isDatabaseFile(name))
            continue;
        struct stat st;
        if (::fstatat(dfd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0)
            continue;
        if (auto f = fromStat(name, st))
            out.push_back(std::move(*f));
    }
    ::closedir(dp); // also closes dfd
    return out;
}

ReconcileResult reconcile(const QVector<DbFile> &db, const QVector<DiskFile> &disk) {
    ReconcileResult r;
    QHash<QString, const DbFile *> dbByName;
    dbByName.reserve(db.size());
    for (const DbFile &f : db)
        dbByName.insert(f.name, &f);

    QSet<QString> onDisk;
    QVector<const DiskFile *> unknown;
    for (const DiskFile &f : disk) {
        onDisk.insert(f.name);
        auto it = dbByName.constFind(f.name);
        if (it == dbByName.constEnd()) {
            unknown.push_back(&f);
        } else {
            const DbFile *d = it.value();
            if (d->size != f.size || d->mtimeNs != f.mtimeNs || d->inode != f.inode)
                r.changed.push_back(f);
        }
    }

    // DB rows that vanished, indexed by (inode, size) for rename detection.
    QHash<QPair<quint64, qint64>, QString> vanished;
    QStringList vanishedOrder;
    for (const DbFile &f : db) {
        if (onDisk.contains(f.name))
            continue;
        vanishedOrder.push_back(f.name);
        if (f.inode != 0)
            vanished.insert({f.inode, f.size}, f.name);
    }

    QSet<QString> renamedFrom;
    for (const DiskFile *f : unknown) {
        auto it = vanished.find({f->inode, f->size});
        if (it != vanished.end()) {
            r.renamed.push_back({it.value(), f->name});
            renamedFrom.insert(it.value());
            vanished.erase(it);
        } else {
            r.added.push_back(*f);
        }
    }
    for (const QString &n : vanishedOrder)
        if (!renamedFrom.contains(n))
            r.removed.push_back(n);
    return r;
}

bool applyReconcile(SelectionDb &db, const ReconcileResult &r, bool defaultSelected) {
    SelectionDb::Transaction tx(db);
    for (const QString &n : r.removed)
        if (!db.removeFile(n))
            return false;
    for (const auto &[from, to] : r.renamed)
        if (!db.renameFile(from, to))
            return false;
    for (const DiskFile &f : r.changed)
        if (!db.updateStat(f.name, f.size, f.mtimeNs, f.inode))
            return false;
    for (const DiskFile &f : r.added) {
        DbFile d;
        d.name = f.name;
        d.selected = defaultSelected;
        d.size = f.size;
        d.mtimeNs = f.mtimeNs;
        d.inode = f.inode;
        if (!db.insertFile(d))
            return false;
    }
    return tx.commit();
}

} // namespace picsel
