
#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_audio_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_request_context.h"

#include "ui/tabbar.h"
#include "ui/searchengine.h"


namespace vibrium {

constexpr wchar_t kWindowClass[] = L"VibriumMainWindow";
constexpr wchar_t kWindowTitle[] = L"Vibrium";

constexpr UINT_PTR kMemoryTimer = 1;
constexpr UINT kMemoryCheckInterval = 15000;
constexpr ULONGLONG kUnloadTimeoutMs = 20ULL * 60ULL * 1000ULL;

constexpr int kTabBarHeight = 36;
constexpr int kSearchBarHeight = 44;
constexpr int kBrowserTop = kTabBarHeight + kSearchBarHeight;

class MainWindow;

// ------------------------------------------------------------
// CEF application
// ------------------------------------------------------------

class VibriumApp final : public CefApp {
public:
    IMPLEMENT_REFCOUNTING(VibriumApp);
};

// ------------------------------------------------------------
// Per-tab browser client
// ------------------------------------------------------------

class BrowserClient final
    : public CefClient,
      public CefLifeSpanHandler,
      public CefDisplayHandler,
      public CefDownloadHandler,
      public CefAudioHandler {
public:
    BrowserClient(MainWindow* owner, int tabId)
        : owner_(owner), tabId_(tabId) {}

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }

    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
        return this;
    }

    CefRefPtr<CefDownloadHandler> GetDownloadHandler() override {
        return this;
    }

    CefRefPtr<CefAudioHandler> GetAudioHandler() override {
        return this;
    }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    void OnAddressChange(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        const CefString& url) override;

    void OnTitleChange(
        CefRefPtr<CefBrowser> browser,
        const CefString& title) override;

    bool OnBeforeDownload(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefDownloadItem> item,
        const CefString& suggestedName,
        CefRefPtr<CefBeforeDownloadCallback> callback) override;

    void OnDownloadUpdated(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefDownloadItem> item,
        CefRefPtr<CefDownloadItemCallback> callback) override;

    void OnAudioStreamStarted(
        CefRefPtr<CefBrowser> browser,
        const CefAudioParameters& params,
        int channels) override;

    void OnAudioStreamStopped(
        CefRefPtr<CefBrowser> browser) override;

private:
    MainWindow* owner_;
    int tabId_;

    IMPLEMENT_REFCOUNTING(BrowserClient);
};

// ------------------------------------------------------------
// Tab state
// ------------------------------------------------------------

struct TabRecord {
    int id = 0;

    std::wstring title = L"New Tab";
    std::wstring url;

    CefRefPtr<CefBrowser> browser;
    CefRefPtr<BrowserClient> client;

    ULONGLONG lastActiveTime = 0;

    int activeDownloads = 0;

    bool playingAudio = false;
    bool closingBrowser = false;
    bool browserCreating = false;
};

// ------------------------------------------------------------
// Main window
// ------------------------------------------------------------

class MainWindow {
public:
    bool Create(HINSTANCE instance) {
        instance_ = instance;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance_;
        wc.lpszClassName = kWindowClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

        if (!RegisterClassExW(&wc) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        hwnd_ = CreateWindowExW(
            0,
            kWindowClass,
            kWindowTitle,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1280,
            800,
            nullptr,
            nullptr,
            instance_,
            this);

        if (!hwnd_) {
            return false;
        }

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        return true;
    }

    // --------------------------------------------------------
    // UI initialization
    // --------------------------------------------------------

    bool InitializeUI() {
        tabBar_ = std::make_unique<TabBar>();

        if (!tabBar_->Create(
                hwnd_,
                instance_,
                0,
                0,
                1280,
                kTabBarHeight)) {
            return false;
        }

        searchBar_ = std::make_unique<SearchEngine>();

        if (!searchBar_->Create(
                hwnd_,
                instance_,
                0,
                kTabBarHeight,
                1280,
                kSearchBarHeight)) {
            return false;
        }

        tabBar_->SetOnTabSelected(
            [this](int index) {
                SelectTab(index);
            });

        tabBar_->SetOnTabClosed(
            [this](int index) {
                CloseTab(index);
            });

        tabBar_->SetOnNewTab(
            [this]() {
                CreateNewTab();
            });

        searchBar_->SetOnNavigate(
            [this](const std::wstring& url) {
                NavigateActiveTab(url);
            });

        SetTimer(
            hwnd_,
            kMemoryTimer,
            kMemoryCheckInterval,
            nullptr);

        CreateNewTab();

        return true;
    }

    // --------------------------------------------------------
    // Tab lookup
    // --------------------------------------------------------

    TabRecord* FindTab(int id) {
        for (auto& tab : tabs_) {
            if (tab.id == id) {
                return &tab;
            }
        }

        return nullptr;
    }

    int FindTabIndex(int id) const {
        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
            if (tabs_[i].id == id) {
                return i;
            }
        }

        return -1;
    }

    TabRecord* GetActiveTab() {
        if (activeTab_ < 0 ||
            activeTab_ >= static_cast<int>(tabs_.size())) {
            return nullptr;
        }

        return &tabs_[activeTab_];
    }

    // --------------------------------------------------------
    // Tab creation
    // --------------------------------------------------------

    void CreateNewTab() {
        TabRecord tab;

        tab.id = nextTabId_++;
        tab.title = L"New Tab";
        tab.url = GetNewTabUrl();
        tab.lastActiveTime = GetTickCount64();

        const int id = tab.id;

        tabs_.push_back(std::move(tab));

        const int index = static_cast<int>(tabs_.size()) - 1;

        tabBar_->AddTab(L"New Tab");
        SelectTab(index);

        (void)id;
    }

    // --------------------------------------------------------
    // Tab selection
    // --------------------------------------------------------

    void SelectTab(int index) {
        if (index < 0 ||
            index >= static_cast<int>(tabs_.size())) {
            return;
        }

        if (activeTab_ >= 0 &&
            activeTab_ < static_cast<int>(tabs_.size())) {
            TabRecord& previous = tabs_[activeTab_];

            if (previous.browser) {
                HWND previousWindow =
                    previous.browser->GetHost()->GetWindowHandle();

                if (previousWindow) {
                    ShowWindow(previousWindow, SW_HIDE);
                }

                previous.browser->GetHost()->WasHidden(true);
            }
        }

        activeTab_ = index;

        TabRecord& selected = tabs_[activeTab_];

        selected.lastActiveTime = GetTickCount64();

        tabBar_->SetActiveTab(index);

        UpdateAddressBar();

        if (selected.browser) {
            ShowBrowser(selected);
        } else if (!selected.browserCreating &&
                   !selected.closingBrowser) {
            CreateBrowserForTab(selected);
        }
    }

    // --------------------------------------------------------
    // Browser creation
    // --------------------------------------------------------

    void CreateBrowserForTab(TabRecord& tab) {
        if (tab.browser ||
            tab.browserCreating ||
            tab.closingBrowser) {
            return;
        }

        tab.browserCreating = true;

        RECT rect{};
        GetBrowserBounds(rect);

        CefWindowInfo windowInfo;
        windowInfo.SetAsChild(hwnd_, rect);

        CefBrowserSettings browserSettings{};

        tab.client = new BrowserClient(this, tab.id);

        const bool created = CefBrowserHost::CreateBrowser(
            windowInfo,
            tab.client,
            tab.url,
            browserSettings,
            nullptr,
            CefRequestContext::GetGlobalContext());

        if (!created) {
            tab.browserCreating = false;
            tab.client = nullptr;

            MessageBoxW(
                hwnd_,
                L"Could not create this tab's browser.",
                L"Vibrium",
                MB_OK | MB_ICONERROR);
        }
    }

    void OnBrowserCreated(
        int tabId,
        CefRefPtr<CefBrowser> browser) {

        TabRecord* tab = FindTab(tabId);

        if (!tab) {
            browser->GetHost()->CloseBrowser(true);
            return;
        }

        tab->browser = browser;
        tab->browserCreating = false;

        if (tabId == GetActiveTabId()) {
            ShowBrowser(*tab);
        } else {
            browser->GetHost()->WasHidden(true);

            HWND window =
                browser->GetHost()->GetWindowHandle();

            if (window) {
                ShowWindow(window, SW_HIDE);
            }
        }
    }

    void ShowBrowser(TabRecord& tab) {
        if (!tab.browser) {
            return;
        }

        RECT rect{};
        GetBrowserBounds(rect);

        HWND window =
            tab.browser->GetHost()->GetWindowHandle();

        if (window) {
            MoveWindow(
                window,
                rect.left,
                rect.top,
                rect.right - rect.left,
                rect.bottom - rect.top,
                TRUE);

            ShowWindow(window, SW_SHOW);
        }

        tab.browser->GetHost()->WasHidden(false);
        tab.browser->GetHost()->WasResized();

        tab.lastActiveTime = GetTickCount64();
    }

    int GetActiveTabId() const {
        if (activeTab_ < 0 ||
            activeTab_ >= static_cast<int>(tabs_.size())) {
            return -1;
        }

        return tabs_[activeTab_].id;
    }

    // --------------------------------------------------------
    // Tab closing
    // --------------------------------------------------------

    void CloseTab(int index) {
        if (index < 0 ||
            index >= static_cast<int>(tabs_.size())) {
            return;
        }

        const int closingId = tabs_[index].id;

        const bool wasActive = index == activeTab_;

        if (tabs_[index].browser) {
            tabs_[index].closingBrowser = true;
            tabs_[index].browser->GetHost()->CloseBrowser(true);
        }

        tabs_.erase(tabs_.begin() + index);
        tabBar_->RemoveTab(index);

        if (tabs_.empty()) {
            activeTab_ = -1;
            CreateNewTab();
            return;
        }

        if (wasActive) {
            activeTab_ = std::min(
                index,
                static_cast<int>(tabs_.size()) - 1);
        } else if (activeTab_ > index) {
            --activeTab_;
        }

        tabBar_->SetActiveTab(activeTab_);
        UpdateAddressBar();

        TabRecord& selected = tabs_[activeTab_];

        if (selected.browser) {
            ShowBrowser(selected);
        } else if (!selected.browserCreating &&
                   !selected.closingBrowser) {
            CreateBrowserForTab(selected);
        }

        (void)closingId;
    }

    // --------------------------------------------------------
    // Navigation
    // --------------------------------------------------------

    void NavigateActiveTab(const std::wstring& url) {
    TabRecord* tab = GetActiveTab();

    if (!tab) {
        return;
    }

    tab->url = url;

    if (tab->browser) {
        tab->browser->GetMainFrame()->LoadURL(url);
    } else if (!tab->browserCreating) {
        CreateBrowserForTab(*tab);
    }
}

    void UpdateAddressBar() {
        TabRecord* tab = GetActiveTab();

        if (tab && searchBar_) {
            searchBar_->SetText(tab->url);
        }
    }

    // --------------------------------------------------------
    // Address and title updates
    // --------------------------------------------------------

    void OnAddressChanged(
        int tabId,
        const std::wstring& url) {

        TabRecord* tab = FindTab(tabId);

        if (!tab) {
            return;
        }

        tab->url = url;

        if (tabId == GetActiveTabId()) {
            UpdateAddressBar();
        }
    }

    void OnTitleChanged(
        int tabId,
        const std::wstring& title) {

        TabRecord* tab = FindTab(tabId);

        if (!tab) {
            return;
        }

        tab->title = title;

        // TabBar currently has no SetTitle() method.
        // Add one there when you want live tab titles.
    }

    // --------------------------------------------------------
    // Activity protection
    // --------------------------------------------------------

    void OnDownloadStarted(int tabId) {
        TabRecord* tab = FindTab(tabId);

        if (tab) {
            ++tab->activeDownloads;
        }
    }

    void OnDownloadUpdated(
        int tabId,
        CefRefPtr<CefDownloadItem> item) {

        TabRecord* tab = FindTab(tabId);

        if (!tab || !item) {
            return;
        }

        if (item->IsComplete() || item->IsCanceled()) {
            tab->activeDownloads =
                std::max(0, tab->activeDownloads - 1);
        }
    }

    void OnAudioStarted(int tabId) {
        TabRecord* tab = FindTab(tabId);

        if (tab) {
            tab->playingAudio = true;
        }
    }

    void OnAudioStopped(int tabId) {
        TabRecord* tab = FindTab(tabId);

        if (tab) {
            tab->playingAudio = false;
        }
    }

    bool IsProtected(const TabRecord& tab) const {
        return tab.activeDownloads > 0 ||
               tab.playingAudio;
    }

    // --------------------------------------------------------
    // Memory manager
    // --------------------------------------------------------

    void CheckInactiveTabs() {
        const ULONGLONG now = GetTickCount64();

        for (auto& tab : tabs_) {
            if (tab.id == GetActiveTabId()) {
                continue;
            }

            if (!tab.browser ||
                tab.browserCreating ||
                tab.closingBrowser) {
                continue;
            }

            if (IsProtected(tab)) {
                continue;
            }

            const ULONGLONG inactiveFor =
                now - tab.lastActiveTime;

            if (inactiveFor >= kUnloadTimeoutMs) {
                UnloadTab(tab);
            }
        }
    }

    void UnloadTab(TabRecord& tab) {
        if (!tab.browser ||
            tab.closingBrowser ||
            IsProtected(tab)) {
            return;
        }

        // Keep the URL and title in TabRecord.
        // The CEF page instance is closed to release resources.
        tab.closingBrowser = true;

        tab.browser->GetHost()->CloseBrowser(true);
    }

    void OnBrowserClosed(int tabId) {
        TabRecord* tab = FindTab(tabId);

        if (!tab) {
            return;
        }

        tab->browser = nullptr;
        tab->client = nullptr;
        tab->browserCreating = false;
        tab->closingBrowser = false;

        // Do not recreate an unloaded tab until selected.
        if (closeRequested_ && tabs_.empty()) {
            DestroyWindow(hwnd_);
        }
    }

    // --------------------------------------------------------
    // Window resizing
    // --------------------------------------------------------

    void ResizeUI() {
        if (!hwnd_) {
            return;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);

        const int width = client.right;
        const int height = client.bottom;

        if (tabBar_) {
            tabBar_->Resize(
                0, 0, width, kTabBarHeight);
        }

        if (searchBar_) {
            searchBar_->Resize(
                0, kTabBarHeight,
                width, kSearchBarHeight);
        }

        for (auto& tab : tabs_) {
            if (tab.browser &&
                tab.id == GetActiveTabId()) {
                ShowBrowser(tab);
            }
        }

        (void)height;
    }

    void GetBrowserBounds(RECT& rect) const {
        RECT client{};
        GetClientRect(hwnd_, &client);

        rect.left = 0;
        rect.top = kBrowserTop;
        rect.right = client.right;
        rect.bottom = std::max(
            kBrowserTop,
            static_cast<int>(client.bottom));
    }

    // --------------------------------------------------------
    // Shutdown
    // --------------------------------------------------------

    void RequestClose() {
        if (closeRequested_) {
            return;
        }

        closeRequested_ = true;

        KillTimer(hwnd_, kMemoryTimer);

        bool anyBrowser = false;

        for (auto& tab : tabs_) {
            if (tab.browser) {
                anyBrowser = true;
                tab.closingBrowser = true;
                tab.browser->GetHost()->CloseBrowser(true);
            }
        }

        if (!anyBrowser) {
            DestroyWindow(hwnd_);
        }
    }

private:
    std::wstring GetNewTabUrl() const {
        const auto path =
            std::filesystem::absolute(
                std::filesystem::current_path() /
                L"html" /
                L"newtab.html");

        std::wstring value = path.generic_wstring();
        std::wstring encoded;

        for (wchar_t ch : value) {
            if (ch == L' ') encoded += L"%20";
            else if (ch == L'#') encoded += L"%23";
            else if (ch == L'%') encoded += L"%25";
            else if (ch == L'?') encoded += L"%3F";
            else encoded += ch;
        }

        return L"file:///" + encoded;
    }

    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) {

        MainWindow* self = nullptr;

        if (message == WM_NCCREATE) {
            auto* create =
                reinterpret_cast<CREATESTRUCTW*>(lParam);

            self = static_cast<MainWindow*>(
                create->lpCreateParams);

            self->hwnd_ = hwnd;

            SetWindowLongPtrW(
                hwnd,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<MainWindow*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (!self) {
            return DefWindowProcW(
                hwnd, message, wParam, lParam);
        }

        switch (message) {
        case WM_CREATE:
            return self->InitializeUI() ? 0 : -1;

        case WM_SIZE:
            self->ResizeUI();
            return 0;

        case WM_TIMER:
            if (wParam == kMemoryTimer) {
                self->CheckInactiveTabs();
            }
            return 0;

        case WM_CLOSE:
            self->RequestClose();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, kMemoryTimer);
            PostQuitMessage(0);
            return 0;

        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            self->hwnd_ = nullptr;
            return DefWindowProcW(
                hwnd, message, wParam, lParam);

        default:
            return DefWindowProcW(
                hwnd, message, wParam, lParam);
        }
    }

private:
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;

    std::unique_ptr<TabBar> tabBar_;
    std::unique_ptr<SearchEngine> searchBar_;

    std::vector<TabRecord> tabs_;

    int activeTab_ = -1;
    int nextTabId_ = 1;

    bool closeRequested_ = false;

    friend class BrowserClient;
};

// ------------------------------------------------------------
// CEF callbacks
// ------------------------------------------------------------

void BrowserClient::OnAfterCreated(
    CefRefPtr<CefBrowser> browser) {

    owner_->OnBrowserCreated(tabId_, browser);
}

void BrowserClient::OnBeforeClose(
    CefRefPtr<CefBrowser> browser) {

    owner_->OnBrowserClosed(tabId_);
}

void BrowserClient::OnAddressChange(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& url) {

    if (frame && frame->IsMain()) {
        owner_->OnAddressChanged(
            tabId_,
            url.ToWString());
    }
}

void BrowserClient::OnTitleChange(
    CefRefPtr<CefBrowser> browser,
    const CefString& title) {

    owner_->OnTitleChanged(
        tabId_,
        title.ToWString());
}

bool BrowserClient::OnBeforeDownload(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> item,
    const CefString& suggestedName,
    CefRefPtr<CefBeforeDownloadCallback> callback) {

    owner_->OnDownloadStarted(tabId_);

    // Let CEF choose its normal download location.
    callback->Continue(
        CefString(suggestedName),
        false);

    return true;
}

void BrowserClient::OnDownloadUpdated(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> item,
    CefRefPtr<CefDownloadItemCallback> callback) {

    owner_->OnDownloadUpdated(tabId_, item);
}

void BrowserClient::OnAudioStreamStarted(
    CefRefPtr<CefBrowser> browser,
    const CefAudioParameters& params,
    int channels) {

    owner_->OnAudioStarted(tabId_);
}

void BrowserClient::OnAudioStreamStopped(
    CefRefPtr<CefBrowser> browser) {

    owner_->OnAudioStopped(tabId_);
}

} // namespace vibrium

// ------------------------------------------------------------
// Application entry point
// ------------------------------------------------------------

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int) {

    CefMainArgs mainArgs(instance);

    CefRefPtr<vibrium::VibriumApp> app =
        new vibrium::VibriumApp();

    const int subprocessExit =
        CefExecuteProcess(mainArgs, app, nullptr);

    if (subprocessExit >= 0) {
        return subprocessExit;
    }

    CefSettings settings;
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = false;

    if (!CefInitialize(
            mainArgs,
            settings,
            app,
            nullptr)) {
        return 1;
    }

    vibrium::MainWindow mainWindow;

    if (!mainWindow.Create(instance)) {
        CefShutdown();
        return 1;
    }

    CefRunMessageLoop();
    CefShutdown();

    return 0;
}
