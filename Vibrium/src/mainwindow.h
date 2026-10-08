#pragma once
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabBar>
#include <QLineEdit>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QWebEngineView>
#include <QProgressBar>
#include <QSettings>
#include <QTimer>
#include <QScreen>
#include <QPixmap>
#include <QBuffer>
#include <QPainter>
#include <QGraphicsBlurEffect>
#include <QNetworkAccessManager>

class WebView;
class DownloadManager;
class HistoryManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    WebView* currentWebView();

public slots:
    void newTab(const QUrl &url = QUrl("vibi://newtab"));

private slots:
    void closeTab(int index);
    void switchTab(int index);
    void navigateTo(const QString &input);
    void onUrlChanged(const QUrl &url);
    void onTitleChanged(const QString &title);
    void onLoadProgress(int progress);
    void onLoadFinished(bool ok);
    void goBack();
    void goForward();
    void reload();
    void toggleDarkMode();
    void openSettings();

protected:
    void paintEvent(QPaintEvent *event) override;
    void installExtension(const QString &crxPath);
    void showNotification(const QString &title, const QString &msg);
    void openHistory();

public:
    void downloadAndInstallExtension(const QString &extId, const QString &store);
    void openDownloads();

private:
    void setupUI();
    void setupTitleBar();
    void setupTabBar();
    void setupToolBar();
    void applyTheme();
    QString resolveUrl(const QString &input);

    // Layout
    QWidget     *m_central;
    QVBoxLayout *m_layout;

    // Title bar (frameless)
    QWidget     *m_titleBar;
    QHBoxLayout *m_titleLayout;
    QLabel      *m_logo;

    // Tab bar
    QTabBar     *m_tabBar;
    QPushButton *m_newTabBtn;

    // Toolbar
    QWidget     *m_toolBar;
    QLineEdit   *m_urlBar;
    QPushButton *m_backBtn, *m_fwdBtn, *m_reloadBtn;
    QProgressBar *m_progress;

    // Content area
    QStackedWidget *m_stack;

    // State
    bool m_darkMode = false;
    QSettings m_settings;

    // Managers
    DownloadManager         *m_dlManager   = nullptr;
    HistoryManager          *m_histManager = nullptr;
    QNetworkAccessManager   *m_netManager  = nullptr;
    QTimer                  *m_wallTimer   = nullptr;
    QPixmap                  m_desktopShot;
    QString                  m_wallpaperB64;
    QString                  m_lastWallpaper;

public slots:
    void updateDesktopSnapshot();
};
