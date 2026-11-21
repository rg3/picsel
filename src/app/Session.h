// Session: everything about one open source folder (one window).
#pragma once

#include "FileListModel.h"
#include "core/SelectionDb.h"
#include "core/Sorting.h"
#include "image/ImageCache.h"
#include "image/ImageProviders.h"

#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QtQmlIntegration>
#include <memory>

namespace picsel {

class Session : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")

    Q_PROPERTY(int sessionId READ sessionId CONSTANT)
    Q_PROPERTY(QString sourceDir READ sourceDir CONSTANT)
    Q_PROPERTY(QString targetDir READ targetDir NOTIFY targetDirChanged)
    Q_PROPERTY(bool defaultSelected READ defaultSelected WRITE setDefaultSelected NOTIFY defaultSelectedChanged)
    Q_PROPERTY(QString sortMode READ sortModeString WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(FileListModel *model READ model CONSTANT)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY countsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY columnsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)

public:
    explicit Session(const QString &dir, QObject *parent = nullptr);
    ~Session() override;

    // Opens an existing picsel.db, or creates one when |create| is set.
    bool open();
    bool create(const QString &targetDir, bool defaultSelected);
    QString errorString() const { return m_error; }

    int sessionId() const { return m_id; }
    QString sourceDir() const { return m_dir; }
    QString targetDir() const { return m_targetDir; }
    bool defaultSelected() const { return m_defaultSelected; }
    void setDefaultSelected(bool v);
    QString sortModeString() const { return sortModeToString(m_sortMode); }
    void setSortMode(const QString &mode);
    FileListModel *model() { return &m_model; }
    int selectedCount() const { return m_selectedCount; }
    int totalCount() const { return m_model.count(); }
    int columns() const { return m_columns; }
    void setColumns(int n);
    bool canUndo() const { return !m_undo.isEmpty(); }

    Q_INVOKABLE int rowOf(const QString &name) const { return m_model.rowOf(name); }
    Q_INVOKABLE void setSelected(int row, bool selected);
    Q_INVOKABLE void toggleSelected(int row);
    // Reverts the last selection change; returns its row (or -1).
    Q_INVOKABLE int undo();
    Q_INVOKABLE bool setTargetDir(const QString &pathOrUrl);
    Q_INVOKABLE QVariantMap info(int row) const;

    // View feedback, used to prioritise decoding.
    Q_INVOKABLE void setThumbBox(int w, int h);
    Q_INVOKABLE void setVisibleRange(int first, int last);
    Q_INVOKABLE void setFullBox(int w, int h);
    Q_INVOKABLE void setFullFocus(int row); // -1 when the full view is closed

    // Asks the window to highlight |name| (from a second "open with").
    void requestHighlight(const QString &name);

signals:
    void targetDirChanged();
    void defaultSelectedChanged();
    void sortModeChanged();
    void countsChanged();
    void columnsChanged();
    void canUndoChanged();
    void highlightRequested(int row);
    void itemInfoChanged(int row);
    void errorOccurred(const QString &message);

private:
    struct UndoEntry {
        QString name;
        bool previous;
    };

    bool loadFiles();
    Item makeItem(const DbFile &f);
    void rebuildCacheOrder();
    void scheduleCacheOrder();
    void onThumbReady(const QString &name, QSize fullSize);
    void onThumbFailed(const QString &name);
    void recountSelected();
    bool applySelected(int row, bool selected, bool recordUndo);
    void fail(const QString &message);

    const int m_id;
    const QString m_dir;
    QString m_targetDir;
    bool m_defaultSelected = false;
    SortMode m_sortMode = SortMode::Name;
    int m_columns = 4;
    int m_selectedCount = 0;
    quint32 m_nextUid = 1;
    QString m_error;

    SelectionDb m_db;
    FileListModel m_model;
    std::shared_ptr<ImageCache> m_thumbs;
    std::shared_ptr<ImageCache> m_full;
    std::shared_ptr<UidMap> m_uids;
    QVector<UndoEntry> m_undo;
    QTimer m_cacheOrderTimer;
};

} // namespace picsel
