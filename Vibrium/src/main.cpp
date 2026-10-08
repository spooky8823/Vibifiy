#include <QApplication>
#include <QWebEngineProfile>
#include <QDir>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    // Qt6: High-DPI scaling is always enabled, no attribute needed

// Performance flags for low-end hardware
// Create extensions directory if it doesn't exist
QString extPath = QDir::homePath() + "/.config/VibiBrowser/extensions";
QDir().mkpath(extPath);

// Build extension load flags from all installed extensions
QString extFlags = "";
QDir extDir(extPath);
QStringList extensions = extDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
for (const QString &ext : extensions) {
    if (!extFlags.isEmpty()) extFlags += ",";
    extFlags += extPath + "/" + ext;
}

QString chromiumFlags =
    "--enable-gpu-rasterization "
    "--enable-zero-copy "
    "--disable-gpu-driver-bug-workarounds "
    "--enable-accelerated-video-decode "
    "--num-raster-threads=2 "
    "--renderer-process-limit=3 "
    "--memory-pressure-thresholds-mb=256,512 "
    "--disable-features=UseChromeOSDirectVideoDecoder "
    "--enable-features=VaapiVideoDecoder,VaapiVideoEncoder "
    "--enable-extensions ";

if (!extFlags.isEmpty())
    chromiumFlags += "--load-extension=" + extFlags;

qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromiumFlags.toUtf8());
    QCoreApplication::setApplicationName("VibiBrowser");
    QCoreApplication::setApplicationVersion(APP_VERSION);
    QCoreApplication::setOrganizationName("Vibifiy");

    QApplication app(argc, argv);

    // Set up persistent profile
    QWebEngineProfile::defaultProfile()->setPersistentCookiesPolicy(
        QWebEngineProfile::AllowPersistentCookies);
        QWebEngineProfile::defaultProfile()->setHttpCacheMaximumSize(50 * 1024 * 1024); // 50MB cache limit
    QWebEngineProfile::defaultProfile()->setCachePath(
        QDir::homePath() + "/.config/VibiBrowser/cache");
    QWebEngineProfile::defaultProfile()->setPersistentStoragePath(
        QDir::homePath() + "/.config/VibiBrowser/storage");

    MainWindow w;
    w.show();

    return app.exec();
}
