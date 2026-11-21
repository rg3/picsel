// SelectionDb: the per-folder selection database (picsel.db).
//
// Every mutating call is its own transaction (or part of an explicit
// Transaction), committed with synchronous=FULL, so a crash never loses a
// selection change that the UI has already shown.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

struct sqlite3;

namespace picsel {

inline constexpr const char *kDbFileName = "picsel.db";
inline constexpr const char *kAppId = "picsel";
inline constexpr int kSchemaVersion = 1;

// Config keys.
namespace cfg {
inline constexpr const char *AppId = "app_id";
inline constexpr const char *SchemaVersion = "schema_version";
inline constexpr const char *SourceDir = "source_dir";
inline constexpr const char *TargetDir = "target_dir";
inline constexpr const char *DefaultSelected = "default_selected";
inline constexpr const char *SortMode = "sort_mode";
} // namespace cfg

// Cached per-file metadata. Valid only while size/mtime match the file.
struct FileMeta {
    bool scanned = false; // false until metadata was read for the current file contents
    QString exifTime;   // "YYYY-MM-DDTHH:MM:SS[.fff]" local camera time, empty if none
    QString camera;     // "Make Model", empty if unknown
    std::optional<quint64> dhash;
};

struct DbFile {
    QString name;
    bool selected = false;
    qint64 size = 0;
    qint64 mtimeNs = 0;
    quint64 inode = 0;
    FileMeta meta;
};

class SelectionDb {
public:
    SelectionDb() = default;
    ~SelectionDb();
    SelectionDb(const SelectionDb &) = delete;
    SelectionDb &operator=(const SelectionDb &) = delete;

    static QString dbPath(const QString &dir);
    static bool existsIn(const QString &dir);

    // Opens (and creates if needed) the database in |dir|. On creation the
    // config table is initialised from the given values.
    bool open(const QString &dir);
    bool create(const QString &dir, const QString &targetDir, bool defaultSelected);
    void close();
    bool isOpen() const { return m_db != nullptr; }
    QString lastError() const { return m_error; }

    QString config(const QString &key, const QString &fallback = {}) const;
    bool setConfig(const QString &key, const QString &value);

    QVector<DbFile> files() const;
    bool insertFile(const DbFile &f);
    bool removeFile(const QString &name);
    bool renameFile(const QString &from, const QString &to);
    bool updateStat(const QString &name, qint64 size, qint64 mtimeNs, quint64 inode);
    bool setSelected(const QString &name, bool selected);
    bool setSelected(const QStringList &names, bool selected);
    bool setMeta(const QString &name, const FileMeta &meta);

    // RAII transaction; commits on commit(), rolls back otherwise.
    class Transaction {
    public:
        explicit Transaction(SelectionDb &db);
        ~Transaction();
        bool commit();
    private:
        SelectionDb &m_db;
        bool m_active = false;
    };

private:
    bool exec(const char *sql);
    bool initSchema();
    bool setErrorFromDb();

    sqlite3 *m_db = nullptr;
    mutable QString m_error;
};

} // namespace picsel
