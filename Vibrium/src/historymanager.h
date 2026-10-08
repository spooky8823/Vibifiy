#pragma once
#include <QObject>
#include <QUrl>
#include <QList>
#include <QDateTime>

struct HistoryEntry {
    QUrl      url;
    QString   title;
    QDateTime visited;
};

class HistoryManager : public QObject
{
    Q_OBJECT
public:
    explicit HistoryManager(QObject *parent = nullptr);
    void addEntry(const QUrl &url, const QString &title);
    QList<HistoryEntry> entries() const { return m_entries; }
    void clear() { m_entries.clear(); save(); }

private:
    void load();
    void save();
    QList<HistoryEntry> m_entries;
    QString m_filePath;
};
