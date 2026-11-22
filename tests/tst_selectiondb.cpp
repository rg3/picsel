// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SelectionDb.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <sqlite3.h>

using namespace picsel;

namespace {

DbFile makeFile(const QString &name, bool selected = false, qint64 size = 100, qint64 mtime = 1000,
                quint64 inode = 1) {
    DbFile f;
    f.name = name;
    f.selected = selected;
    f.size = size;
    f.mtimeNs = mtime;
    f.inode = inode;
    return f;
}

std::optional<DbFile> find(const SelectionDb &db, const QString &name) {
    for (const DbFile &f : db.files())
        if (f.name == name)
            return f;
    return std::nullopt;
}

QStringList names(const SelectionDb &db) {
    QStringList out;
    for (const DbFile &f : db.files())
        out << f.name;
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

class TestSelectionDb : public QObject {
    Q_OBJECT
private slots:
    void createOpenConfigRoundTrip() {
        QTemporaryDir src, dst;
        QVERIFY(src.isValid() && dst.isValid());
        QVERIFY(!SelectionDb::existsIn(src.path()));
        {
            SelectionDb db;
            QVERIFY2(db.create(src.path(), dst.path(), true), qPrintable(db.lastError()));
            QVERIFY(db.isOpen());
            QVERIFY(db.setConfig(cfg::SortMode, "date"));
        }
        QVERIFY(SelectionDb::existsIn(src.path()));
        QCOMPARE(SelectionDb::dbPath(src.path()), QDir(src.path()).filePath("picsel.db"));

        SelectionDb db;
        QVERIFY2(db.open(src.path()), qPrintable(db.lastError()));
        QCOMPARE(db.config(cfg::AppId), QString(kAppId));
        QCOMPARE(db.config(cfg::SchemaVersion), QString::number(kSchemaVersion));
        QCOMPARE(db.config(cfg::SourceDir), QDir(src.path()).absolutePath());
        QCOMPARE(db.config(cfg::TargetDir), QDir(dst.path()).absolutePath());
        QCOMPARE(db.config(cfg::DefaultSelected), QString("1"));
        QCOMPARE(db.config(cfg::SortMode), QString("date"));
        QCOMPARE(db.config("missing", "fb"), QString("fb"));
        QVERIFY(db.setConfig(cfg::SortMode, "name")); // overwrite
        QCOMPARE(db.config(cfg::SortMode), QString("name"));
        db.close();
        QVERIFY(!db.isOpen());
        QCOMPARE(db.config(cfg::SortMode, "closed"), QString("closed"));
    }

    void schemaCreated() {
        QTemporaryDir dir;
        {
            SelectionDb db;
            QVERIFY(db.create(dir.path(), dir.path(), false));
        }
        sqlite3 *raw = nullptr;
        QCOMPARE(sqlite3_open_v2(SelectionDb::dbPath(dir.path()).toUtf8().constData(), &raw,
                                 SQLITE_OPEN_READONLY, nullptr),
                 SQLITE_OK);
        QStringList tables;
        sqlite3_stmt *st = nullptr;
        QCOMPARE(sqlite3_prepare_v2(raw, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name",
                                    -1, &st, nullptr),
                 SQLITE_OK);
        while (sqlite3_step(st) == SQLITE_ROW)
            tables << QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(st, 0)));
        sqlite3_finalize(st);

        QStringList cols;
        QCOMPARE(sqlite3_prepare_v2(raw, "PRAGMA table_info(files)", -1, &st, nullptr), SQLITE_OK);
        while (sqlite3_step(st) == SQLITE_ROW)
            cols << QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(st, 1)));
        sqlite3_finalize(st);
        sqlite3_close(raw);

        QCOMPARE(tables, (QStringList{"config", "files"}));
        QCOMPARE(cols, (QStringList{"name", "selected", "size", "mtime_ns", "inode", "exif_time",
                                    "camera", "dhash"}));
    }

    void insertSelectRemove() {
        QTemporaryDir dir;
        SelectionDb db;
        QVERIFY(db.create(dir.path(), dir.path(), false));
        QVERIFY(db.files().isEmpty());
        QVERIFY(db.insertFile(makeFile("a.jpg", true, 10, 20, 30)));
        QVERIFY(db.insertFile(makeFile("b.jpg", false, 11, 21, 31)));
        QCOMPARE(names(db), (QStringList{"a.jpg", "b.jpg"}));
        const auto a = find(db, "a.jpg");
        QVERIFY(a);
        QCOMPARE(a->selected, true);
        QCOMPARE(a->size, 10);
        QCOMPARE(a->mtimeNs, 20);
        QCOMPARE(a->inode, quint64(30));
        QVERIFY(a->meta.exifTime.isEmpty());
        QVERIFY(!a->meta.dhash);

        // Re-inserting an existing name replaces it and clears metadata.
        QVERIFY(db.setMeta("a.jpg", FileMeta{"2026-01-01T00:00:00", "Cam", 5}));
        QVERIFY(db.insertFile(makeFile("a.jpg", false, 99, 98, 97)));
        const auto a2 = find(db, "a.jpg");
        QCOMPARE(a2->selected, false);
        QCOMPARE(a2->size, 99);
        QVERIFY(a2->meta.camera.isEmpty());
        QVERIFY(!a2->meta.dhash);
        QCOMPARE(db.files().size(), 2);

        QVERIFY(db.removeFile("a.jpg"));
        QCOMPARE(names(db), (QStringList{"b.jpg"}));
        QVERIFY(db.removeFile("nonexistent.jpg"));
        QCOMPARE(names(db), (QStringList{"b.jpg"}));
    }

    void renameFile() {
        QTemporaryDir dir;
        SelectionDb db;
        QVERIFY(db.create(dir.path(), dir.path(), false));
        QVERIFY(db.insertFile(makeFile("a.jpg", true, 10, 20, 30)));
        QVERIFY(db.insertFile(makeFile("b.jpg", false, 11, 21, 31)));
        QVERIFY(db.setMeta("a.jpg", FileMeta{"t", "Cam", 7}));

        QVERIFY(db.renameFile("a.jpg", "c.jpg"));
        QCOMPARE(names(db), (QStringList{"b.jpg", "c.jpg"}));
        auto c = find(db, "c.jpg");
        QCOMPARE(c->selected, true);
        QCOMPARE(c->size, 10);
        QCOMPARE(c->inode, quint64(30));
        QCOMPARE(c->meta.camera, QString("Cam"));
        QCOMPARE(c->meta.dhash, std::optional<quint64>(7));

        // Onto an existing name: replaces that entry, like rename(2).
        QVERIFY2(db.renameFile("c.jpg", "b.jpg"), qPrintable(db.lastError()));
        QCOMPARE(names(db), (QStringList{"b.jpg"}));
        const auto b = find(db, "b.jpg");
        QCOMPARE(b->selected, true);
        QCOMPARE(b->size, 10);

        // Rename to itself is a no-op.
        QVERIFY(db.renameFile("b.jpg", "b.jpg"));
        QCOMPARE(names(db), (QStringList{"b.jpg"}));
    }

    void setSelectedPersists() {
        QTemporaryDir dir;
        {
            SelectionDb db;
            QVERIFY(db.create(dir.path(), dir.path(), false));
            for (const char *n : {"a.jpg", "b.jpg", "c.jpg", "d.jpg"})
                QVERIFY(db.insertFile(makeFile(n)));
            QVERIFY(db.setSelected("a.jpg", true));
            QVERIFY(db.setSelected(QStringList{"b.jpg", "c.jpg"}, true));
            QVERIFY(db.setSelected(QStringList{"c.jpg"}, false));
        }
        SelectionDb db;
        QVERIFY(db.open(dir.path()));
        QCOMPARE(find(db, "a.jpg")->selected, true);
        QCOMPARE(find(db, "b.jpg")->selected, true);
        QCOMPARE(find(db, "c.jpg")->selected, false);
        QCOMPARE(find(db, "d.jpg")->selected, false);
    }

    void appIdMismatchRefused() {
        QTemporaryDir dir;
        {
            SelectionDb db;
            QVERIFY(db.create(dir.path(), dir.path(), false));
            QVERIFY(db.setConfig(cfg::AppId, "somethingelse"));
        }
        SelectionDb db;
        QVERIFY(!db.open(dir.path()));
        QVERIFY(!db.isOpen());
        QVERIFY(db.lastError().contains("another application"));
    }

    void newerSchemaRefused() {
        QTemporaryDir dir;
        {
            SelectionDb db;
            QVERIFY(db.create(dir.path(), dir.path(), false));
            QVERIFY(db.setConfig(cfg::SchemaVersion, QString::number(kSchemaVersion + 1)));
        }
        SelectionDb db;
        QVERIFY(!db.open(dir.path()));
        QVERIFY(!db.isOpen());
        QVERIFY(db.lastError().contains("newer"));
    }

    void updateStatKeepsOrClearsMeta() {
        QTemporaryDir dir;
        SelectionDb db;
        QVERIFY(db.create(dir.path(), dir.path(), false));
        QVERIFY(db.insertFile(makeFile("a.jpg", true, 10, 20, 30)));
        const FileMeta meta{"2026-11-21T18:00:00", "NIKON Z 6", 0x1234};
        QVERIFY(db.setMeta("a.jpg", meta));

        // Same size and mtime (only inode changed): metadata stays valid.
        QVERIFY(db.updateStat("a.jpg", 10, 20, 31));
        auto a = find(db, "a.jpg");
        QCOMPARE(a->inode, quint64(31));
        QCOMPARE(a->meta.exifTime, meta.exifTime);
        QCOMPARE(a->meta.camera, meta.camera);
        QCOMPARE(a->meta.dhash, meta.dhash);
        QCOMPARE(a->selected, true);

        // Size changed: metadata cleared, selection kept.
        QVERIFY(db.updateStat("a.jpg", 11, 20, 31));
        a = find(db, "a.jpg");
        QCOMPARE(a->size, 11);
        QVERIFY(a->meta.exifTime.isEmpty());
        QVERIFY(a->meta.camera.isEmpty());
        QVERIFY(!a->meta.dhash);
        QCOMPARE(a->selected, true);

        // mtime changed: metadata cleared.
        QVERIFY(db.setMeta("a.jpg", meta));
        QVERIFY(db.updateStat("a.jpg", 11, 21, 31));
        a = find(db, "a.jpg");
        QCOMPARE(a->mtimeNs, 21);
        QVERIFY(a->meta.exifTime.isEmpty());
        QVERIFY(!a->meta.dhash);
    }

    void setMetaRoundTrip() {
        QTemporaryDir dir;
        {
            SelectionDb db;
            QVERIFY(db.create(dir.path(), dir.path(), false));
            QVERIFY(db.insertFile(makeFile("a.jpg")));
            QVERIFY(db.insertFile(makeFile("b.jpg")));
            QVERIFY(db.setMeta("a.jpg", FileMeta{"2026-11-21T18:00:00.250", "Google Pixel 9",
                                                  0xFEDCBA9876543210ULL}));
            QVERIFY(db.setMeta("b.jpg", FileMeta{"", "", std::nullopt}));
        }
        SelectionDb db;
        QVERIFY(db.open(dir.path()));
        const auto a = find(db, "a.jpg");
        QCOMPARE(a->meta.exifTime, QString("2026-11-21T18:00:00.250"));
        QCOMPARE(a->meta.camera, QString("Google Pixel 9"));
        QCOMPARE(a->meta.dhash, std::optional<quint64>(0xFEDCBA9876543210ULL)); // full 64 bits
        const auto b = find(db, "b.jpg");
        QVERIFY(b->meta.exifTime.isEmpty());
        QVERIFY(b->meta.camera.isEmpty());
        QVERIFY(!b->meta.dhash);
    }

    void nestedTransactionRollback() {
        QTemporaryDir dir;
        SelectionDb db;
        QVERIFY(db.create(dir.path(), dir.path(), false));
        QVERIFY(db.insertFile(makeFile("keep.jpg")));
        {
            SelectionDb::Transaction outer(db);
            QVERIFY(db.insertFile(makeFile("x.jpg")));
            QVERIFY(db.setSelected("keep.jpg", true));
            {
                SelectionDb::Transaction inner(db);
                QVERIFY(db.insertFile(makeFile("y.jpg")));
                QVERIFY(inner.commit());
            }
            QVERIFY(db.renameFile("x.jpg", "z.jpg")); // itself a nested transaction
            QCOMPARE(names(db), (QStringList{"keep.jpg", "y.jpg", "z.jpg"}));
            // outer destroyed without commit
        }
        QCOMPARE(names(db), (QStringList{"keep.jpg"}));
        QCOMPARE(find(db, "keep.jpg")->selected, false);

        // Inner rollback inside a committed outer keeps only the outer work.
        {
            SelectionDb::Transaction outer(db);
            QVERIFY(db.insertFile(makeFile("a.jpg")));
            {
                SelectionDb::Transaction inner(db);
                QVERIFY(db.insertFile(makeFile("b.jpg")));
            }
            QVERIFY(outer.commit());
        }
        db.close();
        QVERIFY(db.open(dir.path()));
        QCOMPARE(names(db), (QStringList{"a.jpg", "keep.jpg"}));
    }
};

QTEST_GUILESS_MAIN(TestSelectionDb)
#include "tst_selectiondb.moc"
