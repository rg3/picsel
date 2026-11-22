// SPDX-License-Identifier: GPL-3.0-or-later
// The list of files shown in the mosaic, in display order.
#pragma once

#include "core/DirScanner.h"
#include "core/SelectionDb.h"
#include "core/Sorting.h"

#include <QAbstractListModel>
#include <QSize>
#include <QtQmlIntegration>

namespace picsel {

struct Item {
    quint32 uid = 0;      // stable id for image provider URLs
    QString name;
    bool selected = false;
    MediaKind kind = MediaKind::Image;
    qint64 size = 0;
    qint64 mtimeNs = 0;
    quint64 inode = 0;
    FileMeta meta;
    QSize dims;           // upright pixel size, once known
    qint64 durationMs = -1; // videos
    int thumbVersion = 0; // bumped whenever a new thumbnail is available
    bool thumbFailed = false;
    int groupId = -1;     // similar-pictures group, -1 = none
    qint64 sortTimeMs = kNoTime;
};

class FileListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by Session")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        UidRole,
        SelectedRole,
        IsVideoRole,
        ThumbVersionRole,
        ThumbFailedRole,
        GroupIdRole,
        GroupStartRole,
        GroupEndRole,
        DurationRole,
    };

    explicit FileListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_items.size()); }

    const QVector<Item> &items() const { return m_items; }
    const Item *at(int row) const { return row >= 0 && row < m_items.size() ? &m_items[row] : nullptr; }
    Item *mutableAt(int row) { return row >= 0 && row < m_items.size() ? &m_items[row] : nullptr; }
    int rowOf(const QString &name) const;
    int rowOfUid(quint32 uid) const;
    // Renames the item in |row| (keeps the name index in sync).
    void setName(int row, const QString &name);

    void reset(QVector<Item> items);
    // Inserts keeping the sort order; returns the row.
    int insertSorted(Item item, SortMode mode);
    void removeRow(int row);
    // Moves the item to its sorted position after a name/time change; returns new row.
    int resort(int row, SortMode mode);
    void notifyChanged(int row, const QList<int> &roles);
    void notifyAllChanged(const QList<int> &roles);

signals:
    void countChanged();

private:
    int sortedPosition(const Item &item, SortMode mode, int skipRow) const;
    void invalidateIndex() { m_indexDirty = true; }

    QVector<Item> m_items;
    mutable QHash<QString, int> m_index; // name -> row, rebuilt lazily
    mutable bool m_indexDirty = true;
};

SortKey sortKeyOf(const Item &item);

} // namespace picsel
