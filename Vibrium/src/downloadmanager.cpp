#include "downloadmanager.h"
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>

DownloadManager::DownloadManager(QObject *parent) : QObject(parent) {}

void DownloadManager::handleDownload(QWebEngineDownloadRequest *download)
{
    // Ask where to save
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                          + "/" + download->suggestedFileName();
    QString path = QFileDialog::getSaveFileName(nullptr, "Save File", defaultPath);
    if (path.isEmpty()) { download->cancel(); return; }

    download->setDownloadFileName(path);
    download->accept();
    m_downloads.append(download);

    connect(download, &QWebEngineDownloadRequest::isFinishedChanged, this, [download]{
        if (download->isFinished()) {
            if (download->state() == QWebEngineDownloadRequest::DownloadCompleted)
                QMessageBox::information(nullptr, "Download Complete",
                    "Saved: " + download->downloadFileName());
        }
    });
}

void DownloadManager::showPanel()
{
    // TODO: show downloads panel
}
