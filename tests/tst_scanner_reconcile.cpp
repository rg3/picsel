// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DirScanner.h"
#include "core/SelectionDb.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <sqlite3.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace picsel;

namespace {

bool writeFile(const QString &path, const QByteArray &data) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

QStringList sortedNames(const QVector<DiskFile> &files) {
    QStringList out;
    for (const DiskFile &f : files)
        out << f.name;
    std::sort(out.begin(), out.end());
    return out;
}

std::optional<DbFile> find(const SelectionDb &db, const QString &name) {
    for (const DbFile &f : db.files())
        if (f.name == name)
            return f;
    return std::nullopt;
}

DbFile dbFile(const QString &name, qint64 size, qint64 mtime, quint64 inode, bool selected = false) {
    DbFile f;
    f.name = name;
    f.size = size;
    f.mtimeNs = mtime;
    f.inode = inode;
    f.selected = selected;
    return f;
}

DiskFile diskFile(const QString &name, qint64 size, qint64 mtime, quint64 inode) {
    return DiskFile{name, size, mtime, inode};
}

bool setMtime(const QString &path, qint64 sec) {
    struct timespec ts[2];
    ts[0].tv_sec = ts[1].tv_sec = sec;
    ts[0].tv_nsec = ts[1].tv_nsec = 0;
    return ::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), ts, 0) == 0;
}

} // namespace

class TestScannerReconcile : public QObject {
    Q_OBJECT
private slots:
    void mediaKind_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("kind");
        const int img = int(MediaKind::Image), vid = int(MediaKind::Video), none = int(MediaKind::None);
        QTest::newRow("jpg") << "a.jpg" << img;
        QTest::newRow("JPG") << "A.JPG" << img;
        QTest::newRow("jpeg") << "a.jpeg" << img;
        QTest::newRow("JpEg") << "a.JpEg" << img;
        QTest::newRow("jpe") << "a.jpe" << img;
        QTest::newRow("JPE") << "a.JPE" << img;
        QTest::newRow("mp4") << "v.mp4" << vid;
        QTest::newRow("MP4") << "v.MP4" << vid;
        QTest::newRow("m4v") << "v.m4v" << vid;
        QTest::newRow("mov") << "v.MOV" << vid;
        QTest::newRow("multi-dot") << "a.b.c.jpg" << img;
        QTest::newRow("png") << "a.png" << none;
        QTest::newRow("jpg.part") << "a.jpg.part" << none;
        QTest::newRow("trailing dot") << "x." << none;
        QTest::newRow("no stem") << ".jpg" << none;
        QTest::newRow("no extension") << "jpg" << none;
        QTest::newRow("empty") << "" << none;
    }
    void mediaKind() {
        QFETCH(QString, name);
        QFETCH(int, kind);
        QCOMPARE(int(mediaKindForName(name)), kind);
        QCOMPARE(isSupportedName(name), kind != int(MediaKind::None));
    }

    void databaseFile() {
        QVERIFY(isDatabaseFile("picsel.db"));
        QVERIFY(isDatabaseFile("picsel.db-wal"));
        QVERIFY(isDatabaseFile("picsel.db-shm"));
        QVERIFY(isDatabaseFile("picsel.db-journal"));
        QVERIFY(!isDatabaseFile("a.jpg"));
        QVERIFY(!isDatabaseFile("my-picsel.db"));
        QVERIFY(!isDatabaseFile("picsel.jpg"));
    }

    void scanDirFiltersEntries() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QDir d(tmp.path());
        QVERIFY(writeFile(d.filePath("a.jpg"), "aaa"));
        QVERIFY(writeFile(d.filePath("B.JPEG"), "bb"));
        QVERIFY(writeFile(d.filePath("clip.mov"), "c"));
        QVERIFY(writeFile(d.filePath("notes.txt"), "x"));
        QVERIFY(writeFile(d.filePath("img.png"), "x"));
        QVERIFY(d.mkdir("sub.jpg"));                                   // subdirectory
        QVERIFY(writeFile(d.filePath("sub.jpg/inner.jpg"), "x"));      // not recursed into
        QVERIFY(QFile::link(d.filePath("a.jpg"), d.filePath("link.jpg"))); // symlink to a jpg
        QVERIFY(QFile::link(d.filePath("nowhere"), d.filePath("dangling.jpg")));
        QCOMPARE(::mkfifo(QFile::encodeName(d.filePath("fifo.jpg")).constData(), 0600), 0);
        {
            SelectionDb db;
            QVERIFY(db.create(tmp.path(), tmp.path(), false)); // creates picsel.db (+ -wal/-shm)
            QVERIFY(writeFile(d.filePath("picsel.db-journal"), "x"));
            const QVector<DiskFile> files = scanDir(tmp.path());
            QCOMPARE(sortedNames(files), (QStringList{"B.JPEG", "a.jpg", "clip.mov"}));
        }

        const QVector<DiskFile> files = scanDir(tmp.path());
        QCOMPARE(sortedNames(files), (QStringList{"B.JPEG", "a.jpg", "clip.mov"}));
        for (const DiskFile &f : files) {
            struct stat st;
            QCOMPARE(::lstat(QFile::encodeName(d.filePath(f.name)).constData(), &st), 0);
            QCOMPARE(f.size, qint64(st.st_size));
            QCOMPARE(f.inode, quint64(st.st_ino));
            QCOMPARE(f.mtimeNs, qint64(st.st_mtim.tv_sec) * 1000000000LL + st.st_mtim.tv_nsec);
        }

        // statEntry agrees with scanDir.
        QVERIFY(statEntry(tmp.path(), "a.jpg"));
        QCOMPARE(statEntry(tmp.path(), "a.jpg")->size, 3);
        QVERIFY(!statEntry(tmp.path(), "link.jpg"));
        QVERIFY(!statEntry(tmp.path(), "sub.jpg"));
        QVERIFY(!statEntry(tmp.path(), "fifo.jpg"));
        QVERIFY(!statEntry(tmp.path(), "notes.txt"));
        QVERIFY(!statEntry(tmp.path(), "picsel.db"));
        QVERIFY(!statEntry(tmp.path(), "missing.jpg"));

        bool ok = false;
        QVERIFY(!scanDir(tmp.path(), &ok).isEmpty());
        QVERIFY(ok);
        QVERIFY(scanDir(d.filePath("does-not-exist"), &ok).isEmpty());
        QVERIFY(!ok);
    }

    void scanDirReportsIncompleteListing() {
        if (::geteuid() == 0)
            QSKIP("root ignores folder permissions");
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QVERIFY(writeFile(QDir(tmp.path()).filePath("a.jpg"), "a"));
        // Readable but not searchable: readdir() works, every fstatat() fails.
        const QByteArray path = QFile::encodeName(tmp.path());
        QCOMPARE(::chmod(path.constData(), 0600), 0);
        bool ok = true;
        const QVector<DiskFile> files = scanDir(tmp.path(), &ok);
        ::chmod(path.constData(), 0700);
        QVERIFY(!ok);
        QVERIFY(files.isEmpty());
    }

    void reconcileSynthetic() {
        const QVector<DbFile> db{
            dbFile("same.jpg", 10, 100, 1),
            dbFile("gone.jpg", 20, 200, 2),
            dbFile("old.jpg", 30, 300, 3, true),
            dbFile("sizechg.jpg", 40, 400, 4),
            dbFile("mtimechg.jpg", 50, 500, 5),
            dbFile("inodechg.jpg", 60, 600, 6),
            dbFile("notrename.jpg", 70, 700, 7),
        };
        const QVector<DiskFile> disk{
            diskFile("same.jpg", 10, 100, 1),
            diskFile("new.jpg", 30, 300, 3),        // same inode + size as old.jpg: rename
            diskFile("sizechg.jpg", 41, 400, 4),
            diskFile("mtimechg.jpg", 50, 501, 5),
            diskFile("inodechg.jpg", 60, 600, 66),
            diskFile("other.jpg", 71, 700, 7),      // inode of notrename.jpg, different size
            diskFile("fresh.jpg", 80, 800, 8),
        };
        const ReconcileResult r = reconcile(db, disk);
        QVERIFY(!r.isEmpty());

        QStringList removed = r.removed;
        std::sort(removed.begin(), removed.end());
        QCOMPARE(removed, (QStringList{"gone.jpg", "notrename.jpg"}));

        QCOMPARE(r.renamed.size(), 1);
        QCOMPARE(r.renamed[0], (QPair<QString, QString>("old.jpg", "new.jpg")));

        QCOMPARE(sortedNames(r.changed), (QStringList{"inodechg.jpg", "mtimechg.jpg", "sizechg.jpg"}));
        QCOMPARE(sortedNames(r.added), (QStringList{"fresh.jpg", "other.jpg"}));

        // Identical input: nothing to do.
        QVERIFY(reconcile({dbFile("a.jpg", 1, 2, 3)}, {diskFile("a.jpg", 1, 2, 3)}).isEmpty());
        // Inode 0 (unknown) never matches as a rename.
        const ReconcileResult r0 = reconcile({dbFile("a.jpg", 1, 2, 0)}, {diskFile("b.jpg", 1, 2, 0)});
        QCOMPARE(r0.removed, QStringList{"a.jpg"});
        QCOMPARE(r0.added.size(), 1);
        QVERIFY(r0.renamed.isEmpty());
    }

    void reconcileAgainstDisk() {
        QTemporaryDir tmp;
        const QDir d(tmp.path());
        QVERIFY(writeFile(d.filePath("keep.jpg"), "keep"));
        QVERIFY(writeFile(d.filePath("rename_me.jpg"), "rename"));
        QVERIFY(writeFile(d.filePath("delete_me.jpg"), "delete"));
        QVERIFY(writeFile(d.filePath("grow.jpg"), "grow"));
        QVERIFY(writeFile(d.filePath("touch.jpg"), "touch"));
        QVERIFY(setMtime(d.filePath("touch.jpg"), 1000000));

        SelectionDb db;
        QVERIFY(db.create(tmp.path(), tmp.path(), false));
        {
            const ReconcileResult first = reconcile(db.files(), scanDir(tmp.path()));
            QCOMPARE(first.added.size(), 5);
            QVERIFY(applyReconcile(db, first, false));
        }
        QCOMPARE(db.files().size(), 5);
        QVERIFY(db.setSelected(QStringList{"keep.jpg", "rename_me.jpg", "grow.jpg"}, true));
        QVERIFY(db.setMeta("grow.jpg", FileMeta{"t", "c", 1}));
        QVERIFY(db.setMeta("keep.jpg", FileMeta{"t", "c", 2}));

        // Changes while the folder was closed.
        QVERIFY(QFile::rename(d.filePath("rename_me.jpg"), d.filePath("renamed.jpg")));
        QVERIFY(QFile::remove(d.filePath("delete_me.jpg")));
        QVERIFY(writeFile(d.filePath("grow.jpg"), "grown bigger"));
        QVERIFY(setMtime(d.filePath("touch.jpg"), 2000000));
        QVERIFY(writeFile(d.filePath("added.mp4"), "new"));

        const ReconcileResult r = reconcile(db.files(), scanDir(tmp.path()));
        QCOMPARE(r.removed, QStringList{"delete_me.jpg"});
        QCOMPARE(r.renamed.size(), 1);
        QCOMPARE(r.renamed[0], (QPair<QString, QString>("rename_me.jpg", "renamed.jpg")));
        QCOMPARE(sortedNames(r.changed), (QStringList{"grow.jpg", "touch.jpg"}));
        QCOMPARE(sortedNames(r.added), QStringList{"added.mp4"});

        QVERIFY(applyReconcile(db, r, true));
        QStringList names;
        for (const DbFile &f : db.files())
            names << f.name;
        std::sort(names.begin(), names.end());
        QCOMPARE(names, (QStringList{"added.mp4", "grow.jpg", "keep.jpg", "renamed.jpg", "touch.jpg"}));
        QCOMPARE(find(db, "renamed.jpg")->selected, true);  // selection followed the rename
        QCOMPARE(find(db, "added.mp4")->selected, true);    // default selection
        QCOMPARE(find(db, "grow.jpg")->selected, true);
        QCOMPARE(find(db, "grow.jpg")->size, qint64(12));
        QVERIFY(!find(db, "grow.jpg")->meta.dhash);          // stale metadata dropped
        QCOMPARE(find(db, "keep.jpg")->meta.dhash, std::optional<quint64>(2));
        QCOMPARE(find(db, "touch.jpg")->mtimeNs, 2000000LL * 1000000000LL);
        QCOMPARE(find(db, "touch.jpg")->selected, false);

        // Everything is now in sync.
        QVERIFY(reconcile(db.files(), scanDir(tmp.path())).isEmpty());
    }

    void applyReconcileIsAtomic() {
        QTemporaryDir tmp;
        SelectionDb db;
        QVERIFY(db.create(tmp.path(), tmp.path(), false));
        DbFile a = dbFile("a.jpg", 1, 1, 1, true);
        QVERIFY(db.insertFile(a));

        // Make inserting one particular name fail half-way through the apply.
        sqlite3 *raw = nullptr;
        QCOMPARE(sqlite3_open(SelectionDb::dbPath(tmp.path()).toUtf8().constData(), &raw), SQLITE_OK);
        QCOMPARE(sqlite3_exec(raw,
                              "CREATE TRIGGER boom BEFORE INSERT ON files WHEN NEW.name = 'boom.jpg' "
                              "BEGIN SELECT RAISE(ABORT, 'boom'); END;",
                              nullptr, nullptr, nullptr),
                 SQLITE_OK);
        sqlite3_close(raw);

        ReconcileResult r;
        r.removed << "a.jpg";
        r.added << diskFile("ok.jpg", 2, 2, 2) << diskFile("boom.jpg", 3, 3, 3);
        QVERIFY(!applyReconcile(db, r, false));
        QCOMPARE(db.files().size(), 1);
        QCOMPARE(db.files()[0].name, QString("a.jpg"));
        QCOMPARE(db.files()[0].selected, true);
    }
};

QTEST_GUILESS_MAIN(TestScannerReconcile)
#include "tst_scanner_reconcile.moc"
