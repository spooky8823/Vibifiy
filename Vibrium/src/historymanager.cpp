#include "historymanager.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QStandardPaths>

HistoryManager::HistoryManager(QObject *parent) : QObject(parent)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_filePath = dir + "/history.json";
    load();
}

void HistoryManager::addEntry(const QUrl &url, const QString &title)
{
    HistoryEntry e; e.url=url; e.title=title; e.visited=QDateTime::currentDateTime();
    m_entries.prepend(e);
    if (m_entries.size() > 5000) m_entries.removeLast();
    save();
}

void HistoryManager::load()
{
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const auto &v : arr) {
        QJsonObject o = v.toObject();
        HistoryEntry e;
        e.url     = QUrl(o["url"].toString());
        e.title   = o["title"].toString();
        e.visited = QDateTime::fromString(o["visited"].toString(), Qt::ISODate);
        m_entries.append(e);
    }
}

void HistoryManager::save()
{
    QJsonArray arr;
    for (const auto &e : m_entries) {
        QJsonObject o;
        o["url"]     = e.url.toString();
        o["title"]   = e.title;
        o["visited"] = e.visited.toString(Qt::ISODate);
        arr.append(o);
    }
    QFile f(m_filePath);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson());
}
