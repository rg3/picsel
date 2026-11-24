// SPDX-License-Identifier: GPL-3.0-or-later
// Listing of the pictures in a source folder and reconciliation with the DB.
#pragma once

#include "SelectionDb.h"

#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace picsel {

struct DiskFile {
    QString name;
    qint64 size = 0;
    qint64 mtimeNs = 0;
    quint64 inode = 0;
};

enum class MediaKind { None, Image, Video };

// Classification by file name extension (case-insensitive).
MediaKind mediaKindForName(const QString &name);
inline bool isSupportedName(const QString &name) { return mediaKindForName(name) != MediaKind::None; }
// True for picsel.db and its -wal/-shm/-journal companions.
bool isDatabaseFile(const QString &name);

// Stats one entry; returns nothing unless it is a regular file (lstat, so
// symlinks are ignored) with a supported extension.
std::optional<DiskFile> statEntry(const QString &dir, const QString &name);

// All supported regular files directly inside |dir|. When the folder cannot
// be listed completely, returns nothing, sets |ok| to false and leaves the
// cause in errno. Callers must not take a failed listing as "every file was removed".
QVector<DiskFile> scanDir(const QString &dir, bool *ok = nullptr);

struct ReconcileResult {
    QVector<DiskFile> added;                       // new on disk: insert with default state
    QStringList removed;                           // gone from disk: delete from DB
    QVector<QPair<QString, QString>> renamed;      // (old name, new name), selection kept
    QVector<DiskFile> changed;                     // same name, different size/mtime/inode
    bool isEmpty() const {
        return added.isEmpty() && removed.isEmpty() && renamed.isEmpty() && changed.isEmpty();
    }
};

// Compares DB rows with what is on disk. A DB row whose name vanished is
// matched to a new disk name with the same inode and size as a rename.
ReconcileResult reconcile(const QVector<DbFile> &db, const QVector<DiskFile> &disk);

// Applies a reconcile result to the DB in a single transaction.
bool applyReconcile(SelectionDb &db, const ReconcileResult &r, bool defaultSelected);

} // namespace picsel
