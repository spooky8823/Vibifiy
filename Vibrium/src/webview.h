#pragma once
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QTimer>
class HistoryManager;

class WebView : public QWebEngineView
{
    Q_OBJECT
public:
    explicit WebView(QWidget *parent = nullptr);
    void setHistoryManager(HistoryManager *hm) { m_history = hm; }
    void load(const QUrl &url);
    void injectStoreButton();
    void setupExtensionBridge();
    void refreshWallpaper();

protected:
    QWebEngineView *createWindow(QWebEnginePage::WebWindowType type) override;

private:
    HistoryManager *m_history = nullptr;
    QTimer *m_wallTimer = nullptr;
};