#pragma once
#include <QObject>
#include <QWebEngineDownloadRequest>
#include <QList>

class DownloadManager : public QObject
{
    Q_OBJECT
public:
    explicit DownloadManager(QObject *parent = nullptr);
    void showPanel();

public slots:
    void handleDownload(QWebEngineDownloadRequest *download);

private:
    QList<QWebEngineDownloadRequest*> m_downloads;
};
