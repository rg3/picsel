// SPDX-License-Identifier: GPL-3.0-or-later
#include "SelectionDb.h"

#include <QDir>
#include <QFileInfo>
#include <sqlite3.h>

namespace picsel {

namespace {

// Small RAII wrapper around a prepared statement.
class Stmt {
public:
    Stmt(sqlite3 *db, const char *sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr) != SQLITE_OK)
            m_stmt = nullptr;
    }
    ~Stmt() { sqlite3_finalize(m_stmt); }
    explicit operator bool() const { return m_stmt != nullptr; }
    sqlite3_stmt *get() const { return m_stmt; }

    void bind(int i, const QString &s) {
        const QByteArray u = s.toUtf8();
        sqlite3_bind_text(m_stmt, i, u.constData(), int(u.size()), SQLITE_TRANSIENT);
    }
    void bind(int i, qint64 v) { sqlite3_bind_int64(m_stmt, i, v); }
    void bindNull(int i) { sqlite3_bind_null(m_stmt, i); }
    int step() { return sqlite3_step(m_stmt); }

    QString text(int col) const {
        const auto *p = reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, col));
        return p ? QString::fromUtf8(p, sqlite3_column_bytes(m_stmt, col)) : QString();
    }
    qint64 i64(int col) const { return sqlite3_column_int64(m_stmt, col); }
    bool isNull(int col) const { return sqlite3_column_type(m_stmt, col) == SQLITE_NULL; }

private:
    sqlite3_stmt *m_stmt = nullptr;
};

} // namespace

SelectionDb::~SelectionDb() { close(); }

QString SelectionDb::dbPath(const QString &dir) {
    return QDir(dir).filePath(QString::fromLatin1(kDbFileName));
}

bool SelectionDb::existsIn(const QString &dir) {
    return QFileInfo(dbPath(dir)).isFile();
}

bool SelectionDb::setErrorFromDb() {
    m_error = m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QStringLiteral("database not open");
    return false;
}

bool SelectionDb::exec(const char *sql) {
    char *err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        m_error = QString::fromUtf8(err ? err : "unknown error");
        sqlite3_free(err);
        return false;
    }
    return true;
}

namespace {

QString canonicalDir(const QString &dir) {
    const QString c = QFileInfo(dir).canonicalFilePath();
    return c.isEmpty() ? QDir::cleanPath(QDir(dir).absolutePath()) : c;
}

QString notPicsels(const QString &path) {
    return QStringLiteral("%1 is not a picsel database, so picsel leaves it alone. "
                          "Move or rename it to use this folder.").arg(path);
}

// The tables and columns picsel reads and writes.
struct TableSpec {
    const char *name;
    std::initializer_list<const char *> columns;
};
const TableSpec kTables[] = {
    {"config", {"key", "value"}},
    {"files", {"name", "selected", "size", "mtime_ns", "inode", "exif_time", "camera", "dhash"}},
};

} // namespace

SelectionDb::Content SelectionDb::inspect(const QString &dir) {
    const QString path = dbPath(dir);
    auto readError = [&] {
        m_error = sqlite3_errcode(m_db) == SQLITE_NOTADB
            ? notPicsels(path)
            : QStringLiteral("Cannot read %1: %2").arg(path, QString::fromUtf8(sqlite3_errmsg(m_db)));
        return Content::Rejected;
    };

    QStringList tables;
    {
        Stmt st(m_db, "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\'");
        if (!st)
            return readError();
        int rc;
        while ((rc = st.step()) == SQLITE_ROW)
            tables << st.text(0);
        if (rc != SQLITE_DONE)
            return readError();
    }
    if (tables.isEmpty())
        return Content::Empty;
    for (const QString &t : std::as_const(tables)) {
        if (t != QLatin1String("config") && t != QLatin1String("files")) {
            m_error = notPicsels(path);
            return Content::Rejected;
        }
    }
    if (!tables.contains(QLatin1String("config"))) {
        m_error = notPicsels(path);
        return Content::Rejected;
    }

    const QString appId = config(QString::fromLatin1(cfg::AppId));
    if (!appId.isEmpty() && appId != QLatin1String(kAppId)) {
        m_error = QStringLiteral("%1 belongs to another application (%2), so picsel leaves it alone.")
                      .arg(path, appId);
        return Content::Rejected;
    }
    for (const TableSpec &spec : kTables) {
        if (!tables.contains(QLatin1String(spec.name))) {
            if (appId.isEmpty()) {
                m_error = notPicsels(path);
                return Content::Rejected;
            }
            continue; // marked as picsel's: open() creates what is missing
        }
        QStringList have;
        Stmt st(m_db, QByteArray("PRAGMA table_info(") + spec.name + ")");
        if (!st)
            return readError();
        while (st.step() == SQLITE_ROW)
            have << st.text(1);
        for (const char *column : spec.columns) {
            if (!have.contains(QLatin1String(column))) {
                m_error = QStringLiteral("%1 is damaged or comes from an incompatible picsel (table %2 has "
                                         "no column %3). Move or rename it to start this folder again.")
                              .arg(path, QLatin1String(spec.name), QLatin1String(column));
                return Content::Rejected;
            }
        }
    }

    if (appId.isEmpty()) {
        // Unmarked picsel tables: a set-up that never finished (older picsel
        // created the tables before the marker). Only accept them empty.
        Stmt st(m_db, "SELECT (SELECT count(*) FROM config) + (SELECT count(*) FROM files)");
        if (!st || st.step() != SQLITE_ROW)
            return readError();
        if (st.i64(0) != 0) {
            m_error = notPicsels(path);
            return Content::Rejected;
        }
        return Content::Empty;
    }
    const int version = config(QString::fromLatin1(cfg::SchemaVersion), QStringLiteral("0")).toInt();
    if (version > kSchemaVersion) {
        m_error = QStringLiteral("%1 was written by a newer picsel (schema %2).").arg(path).arg(version);
        return Content::Rejected;
    }
    return Content::Picsel;
}

DbProbe SelectionDb::probe(const QString &dir) {
    DbProbe p;
    auto unusable = [&p](const QString &error) {
        p.state = DbProbe::State::Unusable;
        p.error = error;
        return p;
    };
    const QString path = dbPath(dir);
    if (!QFileInfo(dir).isWritable())
        return unusable(QStringLiteral("The folder %1 is not writable, so picsel cannot keep its "
                                       "selection database there.").arg(dir));
    const QFileInfo fi(path);
    if (!fi.exists() && !fi.isSymLink())
        return p;
    if (!fi.isFile())
        return unusable(QStringLiteral("%1 is not a regular file, so picsel cannot use it. "
                                       "Move or rename it to use this folder.").arg(path));
    if (!fi.isWritable())
        return unusable(QStringLiteral("%1 is read-only, so picsel cannot save selections in it.").arg(path));
    if (fi.size() == 0)
        return p;

    SelectionDb db; // read-only: probing never changes the file
    if (sqlite3_open_v2(path.toUtf8().constData(), &db.m_db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        db.setErrorFromDb();
        return unusable(QStringLiteral("Cannot open %1: %2").arg(path, db.m_error));
    }
    switch (db.inspect(dir)) {
    case Content::Rejected:
        return unusable(db.m_error);
    case Content::Empty:
        return p;
    case Content::Picsel:
        break;
    }
    p.targetDir = db.config(QString::fromLatin1(cfg::TargetDir));
    p.defaultSelected = db.config(QString::fromLatin1(cfg::DefaultSelected)) == QLatin1String("1");
    if (p.targetDir.isEmpty())
        return p;
    const QString source = db.config(QString::fromLatin1(cfg::SourceDir));
    if (!source.isEmpty() && canonicalDir(source) != canonicalDir(dir)) {
        p.state = DbProbe::State::Moved;
        p.setUpFor = source;
        return p;
    }
    p.state = DbProbe::State::Ready;
    return p;
}

bool SelectionDb::openFile(const QString &dir, bool create) {
    close();
    const int flags = SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0);
    if (sqlite3_open_v2(dbPath(dir).toUtf8().constData(), &m_db, flags, nullptr) != SQLITE_OK) {
        setErrorFromDb();
        close();
        return false;
    }
    sqlite3_busy_timeout(m_db, 2000);
    return true;
}

bool SelectionDb::open(const QString &dir) {
    if (!openFile(dir, false))
        return false;
    // Check before anything writes (even the journal mode is a write).
    const Content content = inspect(dir);
    if (content == Content::Empty)
        m_error = QStringLiteral("%1 is not set up yet").arg(dbPath(dir));
    if (content != Content::Picsel
        || !exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;")
        || !initSchema()) {
        close();
        return false;
    }
    return true;
}

bool SelectionDb::create(const QString &dir, const QString &targetDir, bool defaultSelected) {
    if (!openFile(dir, true))
        return false;
    if (inspect(dir) == Content::Rejected
        || !exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;")) {
        close();
        return false;
    }
    // Tables and marker together, so a crash never leaves unmarked tables.
    bool ok = false;
    {
        Transaction tx(*this); // rolls back before close() below on failure
        ok = initSchema()
            && setConfig(QString::fromLatin1(cfg::AppId), QString::fromLatin1(kAppId))
            && setConfig(QString::fromLatin1(cfg::SchemaVersion), QString::number(kSchemaVersion))
            && setConfig(QString::fromLatin1(cfg::SourceDir), QDir(dir).absolutePath())
            && setConfig(QString::fromLatin1(cfg::TargetDir), QDir(targetDir).absolutePath())
            && setConfig(QString::fromLatin1(cfg::DefaultSelected),
                         defaultSelected ? QStringLiteral("1") : QStringLiteral("0"))
            && tx.commit();
    }
    if (!ok) {
        close();
        return false;
    }
    return true;
}

void SelectionDb::close() {
    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

bool SelectionDb::initSchema() {
    return exec("CREATE TABLE IF NOT EXISTS config ("
                "  key TEXT PRIMARY KEY NOT NULL,"
                "  value TEXT NOT NULL);"
                "CREATE TABLE IF NOT EXISTS files ("
                "  name TEXT PRIMARY KEY NOT NULL,"
                "  selected INTEGER NOT NULL DEFAULT 0,"
                "  size INTEGER NOT NULL DEFAULT 0,"
                "  mtime_ns INTEGER NOT NULL DEFAULT 0,"
                "  inode INTEGER NOT NULL DEFAULT 0,"
                "  exif_time TEXT,"
                "  camera TEXT,"
                "  dhash INTEGER);");
}

QString SelectionDb::config(const QString &key, const QString &fallback) const {
    if (!m_db)
        return fallback;
    Stmt st(m_db, "SELECT value FROM config WHERE key = ?1");
    if (!st)
        return fallback;
    st.bind(1, key);
    return st.step() == SQLITE_ROW ? st.text(0) : fallback;
}

bool SelectionDb::setConfig(const QString &key, const QString &value) {
    Stmt st(m_db, "INSERT INTO config(key, value) VALUES(?1, ?2) "
                  "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    if (!st)
        return setErrorFromDb();
    st.bind(1, key);
    st.bind(2, value);
    return st.step() == SQLITE_DONE || setErrorFromDb();
}

QVector<DbFile> SelectionDb::files(bool *ok) const {
    QVector<DbFile> out;
    if (ok)
        *ok = false;
    if (!m_db)
        return out;
    Stmt st(m_db, "SELECT name, selected, size, mtime_ns, inode, exif_time, camera, dhash FROM files");
    if (!st) {
        m_error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return out;
    }
    int rc;
    while ((rc = st.step()) == SQLITE_ROW) {
        DbFile f;
        f.name = st.text(0);
        f.selected = st.i64(1) != 0;
        f.size = st.i64(2);
        f.mtimeNs = st.i64(3);
        f.inode = quint64(st.i64(4));
        f.meta.scanned = !st.isNull(5);
        f.meta.exifTime = st.text(5);
        f.meta.camera = st.text(6);
        if (!st.isNull(7))
            f.meta.dhash = quint64(st.i64(7));
        out.push_back(std::move(f));
    }
    if (rc != SQLITE_DONE) {
        m_error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return out;
    }
    if (ok)
        *ok = true;
    return out;
}

bool SelectionDb::insertFile(const DbFile &f) {
    Stmt st(m_db, "INSERT INTO files(name, selected, size, mtime_ns, inode) VALUES(?1, ?2, ?3, ?4, ?5) "
                  "ON CONFLICT(name) DO UPDATE SET selected = excluded.selected, size = excluded.size, "
                  "mtime_ns = excluded.mtime_ns, inode = excluded.inode, "
                  "exif_time = NULL, camera = NULL, dhash = NULL");
    if (!st)
        return setErrorFromDb();
    st.bind(1, f.name);
    st.bind(2, qint64(f.selected ? 1 : 0));
    st.bind(3, f.size);
    st.bind(4, f.mtimeNs);
    st.bind(5, qint64(f.inode));
    return st.step() == SQLITE_DONE || setErrorFromDb();
}

bool SelectionDb::removeFile(const QString &name) {
    Stmt st(m_db, "DELETE FROM files WHERE name = ?1");
    if (!st)
        return setErrorFromDb();
    st.bind(1, name);
    return st.step() == SQLITE_DONE || setErrorFromDb();
}

bool SelectionDb::renameFile(const QString &from, const QString &to) {
    // A rename onto an existing name replaces that entry, like rename(2).
    Transaction tx(*this);
    if (from != to && !removeFile(to))
        return false;
    Stmt st(m_db, "UPDATE files SET name = ?2 WHERE name = ?1");
    if (!st)
        return setErrorFromDb();
    st.bind(1, from);
    st.bind(2, to);
    if (st.step() != SQLITE_DONE)
        return setErrorFromDb();
    return tx.commit();
}

bool SelectionDb::updateStat(const QString &name, qint64 size, qint64 mtimeNs, quint64 inode) {
    Stmt st(m_db, "UPDATE files SET size = ?2, mtime_ns = ?3, inode = ?4, "
                  "exif_time = CASE WHEN size = ?2 AND mtime_ns = ?3 THEN exif_time END, "
                  "camera = CASE WHEN size = ?2 AND mtime_ns = ?3 THEN camera END, "
                  "dhash = CASE WHEN size = ?2 AND mtime_ns = ?3 THEN dhash END "
                  "WHERE name = ?1");
    if (!st)
        return setErrorFromDb();
    st.bind(1, name);
    st.bind(2, size);
    st.bind(3, mtimeNs);
    st.bind(4, qint64(inode));
    return st.step() == SQLITE_DONE || setErrorFromDb();
}

bool SelectionDb::setSelected(const QString &name, bool selected) {
    Stmt st(m_db, "UPDATE files SET selected = ?2 WHERE name = ?1");
    if (!st)
        return setErrorFromDb();
    st.bind(1, name);
    st.bind(2, qint64(selected ? 1 : 0));
    return st.step() == SQLITE_DONE || setErrorFromDb();
}

bool SelectionDb::setSelected(const QStringList &names, bool selected) {
    Transaction tx(*this);
    for (const QString &n : names)
        if (!setSelected(n, selected))
            return false;
    return tx.commit();
}

bool SelectionDb::setMeta(const QString &name, const FileMeta &meta) {
    Stmt st(m_db, "UPDATE files SET exif_time = ?2, camera = ?3, dhash = ?4 WHERE name = ?1");
    if (!st)
        return setErrorFromDb();
    st.bind(1, name);
    // Unread EXIF stays NULL: a hash can be saved before the EXIF is read.
    if (meta.scanned) {
        st.bind(2, meta.exifTime.isNull() ? QStringLiteral("") : meta.exifTime);
        st.bind(3, meta.camera.isNull() ? QStringLiteral("") : meta.camera);
    } else {
        st.bindNull(2);
        st.bindNull(3);
    }
    if (meta.dhash)
        st.bind(4, qint64(*meta.dhash));
    else
        st.bindNull(4);
    return st.step() == SQLITE_DONE || setErrorFromDb();
}

// Transactions nest via SAVEPOINT so helpers can be composed freely.
SelectionDb::Transaction::Transaction(SelectionDb &db) : m_db(db) {
    m_active = m_db.m_db && m_db.exec("SAVEPOINT picsel_tx");
}

SelectionDb::Transaction::~Transaction() {
    if (m_active)
        m_db.exec("ROLLBACK TO picsel_tx; RELEASE picsel_tx");
}

bool SelectionDb::Transaction::commit() {
    if (!m_active)
        return false;
    m_active = false;
    return m_db.exec("RELEASE picsel_tx");
}

} // namespace picsel
