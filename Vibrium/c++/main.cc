#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <unknwn.h>
#include <objidl.h>
#include <shobjidl.h>
#include <gdiplus.h>

// Common-controls macros collide with identically named CEF DOM methods.
#ifdef GetNextSibling
#undef GetNextSibling
#endif
#ifdef GetFirstChild
#undef GetFirstChild
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
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

#include "tabbar.h"
#include "searchengine.h"
#include "settings_page.h"
#include "update_launcher.h"
#include "accent_icon.h"
#include "resource.h"
#include "theme.h"
#include "adblock/adblock_service.h"
#include "adblock/request_policy.h"
#include "adblock/scriptlet_engine.h"
#include "selftest.h"
#include "include/cef_load_handler.h"
#include "include/cef_image.h"
#include "include/cef_urlrequest.h"


namespace vibrium {

constexpr wchar_t kWindowClass[] = L"VibriumMainWindow";
constexpr wchar_t kWindowTitle[] = L"Vibrium";

constexpr UINT_PTR kMemoryTimer = 1;
constexpr UINT_PTR kSelfTestTimer = 2;
constexpr UINT kFinalizeShutdownMessage = WM_APP + 1;
constexpr UINT kMemoryCheckInterval = 15000;
constexpr ULONGLONG kUnloadTimeoutMs = 20ULL * 60ULL * 1000ULL;

constexpr int kTabBarHeight = 46;
constexpr int kTabBarClientOverlap = 9;
constexpr int kCaptionButtonsWidth = 138;
constexpr int kSearchBarHeight = 52;
constexpr int kBrowserTop = kTabBarClientOverlap + kSearchBarHeight;

class MainWindow;

// ------------------------------------------------------------
// CEF application
// ------------------------------------------------------------

// Runs in the browser process and in every renderer. The renderer half
// exposes window.cefQuery to page scripts so the cosmetic content script can
// ask the browser for generic class/id selectors (see adblock/
// adblock_cosmetic_router.h).
class VibriumApp final
    : public CefApp,
      public CefBrowserProcessHandler,
      public CefRenderProcessHandler {
public:
    // Command-line switch that tells child processes where the filter lists
    // and resources.json are, so a renderer can build its scriptlet engine.
    static constexpr char kScriptletDirSwitch[] = "vibrium-scriptlet-dir";

    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }

    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
        return this;
    }

    // Browser process: runs for every child process just before it launches.
    void OnBeforeChildProcessLaunch(
        CefRefPtr<CefCommandLine> command_line) override {
        CefRefPtr<CefCommandLine> own = CefCommandLine::GetGlobalCommandLine();
        if (own && own->HasSwitch("disable-scriptlets")) {
            return;
        }

        const vibrium::AdblockService& service =
            vibrium::AdblockService::Get();

        if (!service.enabled() || service.filter_dir().empty()) {
            return;
        }

        command_line->AppendSwitchWithValue(
            kScriptletDirSwitch, service.filter_dir());
    }

    void OnWebKitInitialized() override {
        // Keep the renderer half of the cosmetic query router disabled during
        // the CI isolation run as well. Disabling only the browser-side router
        // is not a complete isolation because renderer-side callbacks still
        // participate in CEF teardown.
        CefRefPtr<CefCommandLine> command_line =
            CefCommandLine::GetGlobalCommandLine();
        if (command_line &&
            command_line->HasSwitch("disable-cosmetic-router")) {
            return;
        }

        rendererRouter_ = CefMessageRouterRendererSide::Create(
            vibrium::CosmeticRouterConfig());
    }

    void OnContextCreated(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefV8Context> context) override {
        // First, before the page runs anything: see InjectScriptlets().
        InjectScriptlets(frame, context);

        if (rendererRouter_) {
            rendererRouter_->OnContextCreated(browser, frame, context);
        }
    }

    void OnContextReleased(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefV8Context> context) override {
        if (rendererRouter_) {
            rendererRouter_->OnContextReleased(browser, frame, context);
        }
    }

    bool OnProcessMessageReceived(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefProcessId source_process,
        CefRefPtr<CefProcessMessage> message) override {
        return rendererRouter_ &&
               rendererRouter_->OnProcessMessageReceived(
                   browser, frame, source_process, message);
    }

private:
    // Runs the ad-blocking scriptlets for `frame` (for example, the ones that
    // strip ads from YouTube's player data) in the page's own JavaScript
    // context. OnContextCreated fires when the context is created, before any
    // script of the page has run, and Eval is synchronous, so the scriptlet is
    // guaranteed to be in place first. V8 evaluation is not subject to the
    // page's Content-Security-Policy.
    void InjectScriptlets(
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefV8Context> context) {
        if (scriptletsUnavailable_ || !frame || !context) {
            return;
        }

        const std::string url = frame->GetURL().ToString();

        if (!vibrium::IsFilterableUrl(url)) {
            return;  // about:blank, file:, devtools, ...
        }

        if (!scriptlets_) {
            CefRefPtr<CefCommandLine> command_line =
                CefCommandLine::GetGlobalCommandLine();

            if (!command_line ||
                !command_line->HasSwitch(kScriptletDirSwitch)) {
                scriptletsUnavailable_ = true;
                return;
            }

            scriptlets_ = std::make_unique<vibrium::ScriptletProvider>(
                command_line->GetSwitchValue(kScriptletDirSwitch).ToString());
        }

        const std::optional<std::string> script = scriptlets_->ScriptFor(url);

        if (!script) {
            return;
        }

        CefRefPtr<CefV8Value> result;
        CefRefPtr<CefV8Exception> exception;

        // A failing scriptlet must never break the page: errors are ignored.
        context->Eval(
            vibrium::BuildScriptletScript(*script),
            url,
            0,
            result,
            exception);
    }

    CefRefPtr<CefMessageRouterRendererSide> rendererRouter_;
    std::unique_ptr<vibrium::ScriptletProvider> scriptlets_;
    bool scriptletsUnavailable_ = false;

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
      public CefAudioHandler,
      public CefLoadHandler {
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

    // Ad blocking: shared request handler + cosmetic filter injection.
    CefRefPtr<CefRequestHandler> GetRequestHandler() override {
        return AdblockService::Get().request_handler();
    }

    CefRefPtr<CefLoadHandler> GetLoadHandler() override {
        return this;
    }

    // window.cefQuery calls from page scripts (generic cosmetic selectors).
    bool OnProcessMessageReceived(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefProcessId source_process,
        CefRefPtr<CefProcessMessage> message) override {
        return AdblockService::Get().OnProcessMessageReceived(
            browser, frame, source_process, message);
    }

    void OnLoadStart(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        TransitionType transition_type) override;

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    void OnAddressChange(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        const CefString& url) override;

    void OnTitleChange(
        CefRefPtr<CefBrowser> browser,
        const CefString& title) override;

    void OnFaviconURLChange(
        CefRefPtr<CefBrowser> browser,
        const std::vector<CefString>& icon_urls) override;

    void OnFullscreenModeChange(
        CefRefPtr<CefBrowser> browser,
        bool fullscreen) override;

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

    void OnAudioStreamPacket(
        CefRefPtr<CefBrowser> browser,
        const float** data,
        int frames,
        int64_t pts) override;

    void OnAudioStreamError(
        CefRefPtr<CefBrowser> browser,
        const CefString& message) override;

private:
    MainWindow* owner_;
    int tabId_;

    IMPLEMENT_REFCOUNTING(BrowserClient);
};

// ------------------------------------------------------------
// Generate the themed window/taskbar icon from the repository's original
// PNG. Hue is taken directly from the selected accent; luminance and alpha
// remain from the artwork so its petals and glow keep their depth.
namespace {
struct FlowerHsv { double h, s, v; };

FlowerHsv FlowerRgbToHsv(BYTE r, BYTE g, BYTE b) {
    double rf=r/255.0, gf=g/255.0, bf=b/255.0;
    double mx=std::max({rf,gf,bf}), mn=std::min({rf,gf,bf}), d=mx-mn;
    FlowerHsv out{0.0, mx==0.0?0.0:d/mx, mx};
    if (d==0.0) return out;
    if (mx==rf) out.h=60.0*std::fmod((gf-bf)/d,6.0);
    else if (mx==gf) out.h=60.0*((bf-rf)/d+2.0);
    else out.h=60.0*((rf-gf)/d+4.0);
    if (out.h<0.0) out.h+=360.0;
    return out;
}

void FlowerHsvToRgb(FlowerHsv c, BYTE& r, BYTE& g, BYTE& b) {
    double chroma=c.v*c.s;
    double x=chroma*(1.0-std::abs(std::fmod(c.h/60.0,2.0)-1.0));
    double m=c.v-chroma, rf=0, gf=0, bf=0;
    if(c.h<60){rf=chroma;gf=x;} else if(c.h<120){rf=x;gf=chroma;}
    else if(c.h<180){gf=chroma;bf=x;} else if(c.h<240){gf=x;bf=chroma;}
    else if(c.h<300){rf=x;bf=chroma;} else {rf=chroma;bf=x;}
    r=static_cast<BYTE>(std::clamp((rf+m)*255.0,0.0,255.0));
    g=static_cast<BYTE>(std::clamp((gf+m)*255.0,0.0,255.0));
    b=static_cast<BYTE>(std::clamp((bf+m)*255.0,0.0,255.0));
}

HICON CreateAccentFlowerIcon(COLORREF accent) {
    constexpr int size=256;
    wchar_t exe[MAX_PATH*4]{};
    DWORD n=GetModuleFileNameW(nullptr,exe,static_cast<DWORD>(std::size(exe)));
    if(!n || n>=std::size(exe)) return nullptr;
    auto path=std::filesystem::path(exe).parent_path()/L"icons"/L"appicon.png";
    Gdiplus::Bitmap source(path.c_str());
    if(source.GetLastStatus()!=Gdiplus::Ok) return nullptr;
    Gdiplus::Bitmap image(size,size,PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&image);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    if(graphics.DrawImage(&source,0,0,size,size)!=Gdiplus::Ok) return nullptr;
    Gdiplus::Rect rect(0,0,size,size);
    Gdiplus::BitmapData data{};
    if(image.LockBits(&rect,Gdiplus::ImageLockModeRead|Gdiplus::ImageLockModeWrite,
       PixelFormat32bppARGB,&data)!=Gdiplus::Ok) return nullptr;
    FlowerHsv target=FlowerRgbToHsv(GetRValue(accent),GetGValue(accent),GetBValue(accent));
    for(int y=0;y<size;++y){
        BYTE* row=static_cast<BYTE*>(data.Scan0)+static_cast<ptrdiff_t>(y)*data.Stride;
        for(int x=0;x<size;++x){
            BYTE* p=row+x*4; BYTE a=p[3]; if(!a) continue;
            FlowerHsv original=FlowerRgbToHsv(p[2],p[1],p[0]);
            if(original.s<0.075 || original.v<0.035) continue; // neutral detail/background
            original.h=target.h;
            original.s*=target.s;
            original.v*=0.72+0.28*target.v;
            BYTE r=0,g=0,b=0; FlowerHsvToRgb(original,r,g,b);
            p[0]=b;p[1]=g;p[2]=r;
        }
    }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=size;
    bi.bmiHeader.biHeight=-size; bi.bmiHeader.biPlanes=1;
    bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HBITMAP color=CreateDIBSection(nullptr,&bi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!color||!bits){if(color)DeleteObject(color);image.UnlockBits(&data);return nullptr;}
    for(int y=0;y<size;++y){
        const BYTE* src=static_cast<const BYTE*>(data.Scan0)+static_cast<ptrdiff_t>(y)*data.Stride;
        BYTE* dst=static_cast<BYTE*>(bits)+y*size*4;
        for(int x=0;x<size;++x){
            BYTE a=src[x*4+3];dst[x*4+3]=a;
            dst[x*4]=static_cast<BYTE>(src[x*4]*a/255);
            dst[x*4+1]=static_cast<BYTE>(src[x*4+1]*a/255);
            dst[x*4+2]=static_cast<BYTE>(src[x*4+2]*a/255);
        }
    }
    image.UnlockBits(&data);
    HBITMAP mask=CreateBitmap(size,size,1,1,nullptr);
    if(mask){HDC dc=CreateCompatibleDC(nullptr);HGDIOBJ old=SelectObject(dc,mask);
        PatBlt(dc,0,0,size,size,BLACKNESS);SelectObject(dc,old);DeleteDC(dc);}
    ICONINFO ii{};ii.fIcon=TRUE;ii.hbmColor=color;ii.hbmMask=mask;
    HICON icon=mask?CreateIconIndirect(&ii):nullptr;
    DeleteObject(color);if(mask)DeleteObject(mask);return icon;
}
} // namespace

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

    // The built-in settings page: a native view, no CEF browser.
    bool isSettings = false;
};

// ------------------------------------------------------------
// Main window
// ------------------------------------------------------------

class MainWindow {
public:
    ~MainWindow() {
        if (windowIconSmall_) DestroyIcon(windowIconSmall_);
        if (windowIconLarge_ && windowIconLarge_ != windowIconSmall_) DestroyIcon(windowIconLarge_);
        if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
    }

    bool Create(HINSTANCE instance) {
        instance_ = instance;
        Theme::Load();
        Gdiplus::GdiplusStartupInput gdiplusInput;
        if (Gdiplus::GdiplusStartup(&gdiplusToken_, &gdiplusInput, nullptr) != Gdiplus::Ok) {
            gdiplusToken_ = 0;  // Browser remains usable with the default icon.
        }

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance_;
        wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_VIBRIUM));
        wc.hIconSm = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_VIBRIUM));
        wc.lpszClassName = kWindowClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        // The client area extends into the caption for the integrated tab row.
        // Paint that area explicitly; COLOR_WINDOW would leave an ugly white
        // rectangle behind the native caption buttons.
        wc.hbrBackground = nullptr;

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

        TabBar::SetAccentColor(Theme::Accent());
        ApplyTitleBarTheme();
        UpdateWindowIcon();

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        return true;
    }

    void ApplyTitleBarTheme() {
        if (!hwnd_) return;

        // Keep the real Windows non-client title bar. DWM paints its native
        // caption buttons; the tab strip is a separate, non-activating overlay
        // positioned over the left side of that same row.
        const COLORREF accent = Theme::Accent();
        DwmSetWindowAttribute(
            hwnd_, DWMWA_CAPTION_COLOR, &accent, sizeof(accent));

        const COLORREF captionText = RGB(255, 255, 255);
        DwmSetWindowAttribute(
            hwnd_, DWMWA_TEXT_COLOR, &captionText, sizeof(captionText));

        SetWindowPos(
            hwnd_, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOACTIVATE | SWP_FRAMECHANGED);
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
                kTabBarClientOverlap,
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

        searchBar_->SetOnSettings(
            [this]() {
                OpenSettings();
            });

        searchBar_->SetOnNavigation([this](int action) {
            TabRecord* tab = GetActiveTab();
            if (!tab || !tab->browser) return;
            switch (action) {
                case 0: tab->browser->GoBack(); break;
                case 1: NavigateActiveTab(GetNewTabUrl()); break;
                case 2: tab->browser->Reload(); break;
                case 3: tab->browser->GoForward(); break;
                default: break;
            }
        });

        settingsPage_ = std::make_unique<SettingsPage>();

        if (!settingsPage_->Create(hwnd_, instance_)) {
            // Not fatal: browsing works, the gear button just does nothing.
            settingsPage_.reset();
        }

        if (settingsPage_) {
            settingsPage_->SetOnUpdateRequested([this]() {
                std::wstring error;
                if (!StartUpdater(&error)) {
                    MessageBoxW(hwnd_, error.c_str(), L"Vibrium update", MB_OK | MB_ICONERROR);
                    return;
                }
                // The updater waits for this process to exit, then installs
                // the newest build and starts Vibrium again.
                RequestClose();
            });
            settingsPage_->SetOnThemeChanged([this](COLORREF color) {
                wchar_t script[128]{};
                swprintf_s(script, L"document.documentElement.style.setProperty('--vibrium-accent', '#%02X%02X%02X');",
                    GetRValue(color), GetGValue(color), GetBValue(color));
                for (auto& tab : tabs_) {
                    if (tab.browser) {
                        tab.browser->GetMainFrame()->ExecuteJavaScript(script, tab.browser->GetMainFrame()->GetURL(), 0);
                    }
                }
                ApplyTitleBarTheme();
                UpdateWindowIcon();
                if (tabBar_) InvalidateRect(tabBar_->GetHandle(), nullptr, TRUE);
                if (searchBar_) InvalidateRect(searchBar_->GetHandle(), nullptr, TRUE);
            });
        }

        SetTimer(
            hwnd_,
            kMemoryTimer,
            kMemoryCheckInterval,
            nullptr);

        if (SelfTest::Get().enabled()) {
            SetTimer(hwnd_, kSelfTestTimer, SelfTest::kTimeoutMs, nullptr);
        }

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
        CreateTab(GetNewTabUrl(), L"New Tab", false);
    }

    void CreateTab(
        const std::wstring& url,
        const std::wstring& title,
        bool isSettings) {

        TabRecord tab;

        tab.id = nextTabId_++;
        tab.title = title;
        tab.url = url;
        tab.isSettings = isSettings;
        tab.lastActiveTime = GetTickCount64();

        const int id = tab.id;

        tabs_.push_back(std::move(tab));

        const int index = static_cast<int>(tabs_.size()) - 1;

        tabBar_->AddTab(title);
        SelectTab(index);

        (void)id;
    }

    // --------------------------------------------------------
    // Settings page (native view shown in place of a browser)
    // --------------------------------------------------------

    // Opens the settings page as a tab, or switches to it if it is open.
    void OpenSettings() {
        if (!settingsPage_) {
            return;
        }

        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
            if (tabs_[i].isSettings) {
                SelectTab(i);
                return;
            }
        }

        CreateTab(kSettingsUrl, L"Settings", true);
    }

    void ShowSettingsPage() {
        if (!settingsPage_) {
            return;
        }

        RECT rect{};
        GetBrowserBounds(rect);
        settingsPage_->Show(rect);
    }

    void HideSettingsPage() {
        if (settingsPage_) {
            settingsPage_->Hide();
        }
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

        if (selected.isSettings) {
            ShowSettingsPage();
            return;
        }

        HideSettingsPage();

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
        if (tab.isSettings ||
            tab.browser ||
            tab.browserCreating ||
            tab.closingBrowser) {
            return;
        }

        tab.browserCreating = true;

        RECT rect{};
        GetBrowserBounds(rect);

        CefWindowInfo windowInfo;
        windowInfo.SetAsChild(
            hwnd_,
            CefRect(
                rect.left,
                rect.top,
                rect.right - rect.left,
                rect.bottom - rect.top
            )
        );

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

    // If shutdown started while CEF was creating this browser,
    // close it immediately instead of displaying it.
    if (closeRequested_) {
        tab->closingBrowser = true;
        browser->GetHost()->CloseBrowser(true);
        return;
    }

    if (tabId == GetActiveTabId()) {
        ShowBrowser(*tab);
    } else {
        browser->GetHost()->WasHidden(true);

        HWND window = browser->GetHost()->GetWindowHandle();

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
            const int rawPageWidth = rect.right - rect.left;
            const int pageWidth = rawPageWidth > 0 ? rawPageWidth : 0;
            const int rawPageHeight = rect.bottom - rect.top;
            const int pageHeight = rawPageHeight > 0 ? rawPageHeight : 0;

            MoveWindow(
                window,
                rect.left,
                rect.top,
                pageWidth,
                pageHeight,
                TRUE);

            // Clip the CEF child window to a subtle rounded rectangle. The
            // region is recreated on resize so it always matches the viewport.
            if (pageWidth > 0 && pageHeight > 0) {
                constexpr int pageCornerRadius = 12;
                HRGN pageRegion = CreateRoundRectRgn(
                    0,
                    0,
                    pageWidth + 1,
                    pageHeight + 1,
                    pageCornerRadius,
                    pageCornerRadius);
                if (pageRegion && !SetWindowRgn(window, pageRegion, TRUE)) {
                    DeleteObject(pageRegion);
                }
            }

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

        if (tabs_[index].isSettings) {
            HideSettingsPage();
        }

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

        if (selected.isSettings) {
            ShowSettingsPage();
        } else if (selected.browser) {
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

    // "vibrium://settings" opens (or switches to) the settings tab.
    if (IsSettingsUrl(url)) {
        if (tab->isSettings) {
            UpdateAddressBar();
        } else {
            OpenSettings();
        }
        return;
    }

    // Navigating away from the settings tab turns it into a normal tab.
    if (tab->isSettings) {
        tab->isSettings = false;
        HideSettingsPage();
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

        // Clear the previous site's icon while the new page is loading.
        // A fresh favicon callback will replace the fallback when available.
        const int tabIndex = FindTabIndex(tabId);
        if (tabIndex >= 0 && tabBar_) {
            tabBar_->SetTabIconBitmap(tabIndex, nullptr);
        }

        if (tabId == GetActiveTabId()) {
            UpdateAddressBar();
        }
    }

    void OnFaviconReady(int tabId, HBITMAP bitmap) {
        const int index = FindTabIndex(tabId);
        if (index < 0 || !tabBar_) {
            if (bitmap) DeleteObject(bitmap);
            return;
        }
        tabBar_->SetTabIconBitmap(index, bitmap);
    }

    void OnTitleChanged(
    int tabId,
    const std::wstring& title) {

    TabRecord* tab = FindTab(tabId);

    if (!tab) {
        return;
    }

    tab->title = title.empty() ? L"New Tab" : title;

    const int index = FindTabIndex(tabId);

    if (index >= 0 && tabBar_) {
        tabBar_->SetTabTitle(index, tab->title);
    }

    if (SelfTest::Get().OnTitleChanged(tab->title)) {
        RequestClose();
    }
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

    if (tab) {
        tab->browser = nullptr;
        tab->client = nullptr;
        tab->browserCreating = false;
        tab->closingBrowser = false;
    }

    if (closeRequested_) {
        FinishShutdownIfReady();
    }
 }

    // --------------------------------------------------------
    // Window resizing
    // --------------------------------------------------------


    void SetVideoFullscreen(int tabId, bool fullscreen) {
        if (!hwnd_) {
            return;
        }

        if (fullscreen) {
            if (videoFullscreen_ || tabId != GetActiveTabId()) {
                return;
            }

            videoFullscreen_ = true;
            fullscreenTabId_ = tabId;
            windowedStyle_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);
            windowedExStyle_ = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
            windowedPlacement_ = {};
            windowedPlacement_.length = sizeof(windowedPlacement_);
            GetWindowPlacement(hwnd_, &windowedPlacement_);

            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            HMONITOR target = MonitorFromWindow(
                hwnd_, MONITOR_DEFAULTTONEAREST);

            if (GetMonitorInfoW(target, &monitor)) {
                SetWindowLongPtrW(
                    hwnd_,
                    GWL_STYLE,
                    WS_POPUP | WS_VISIBLE);

                SetWindowPos(
                    hwnd_,
                    HWND_TOPMOST,
                    monitor.rcMonitor.left,
                    monitor.rcMonitor.top,
                    monitor.rcMonitor.right - monitor.rcMonitor.left,
                    monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                    SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
            } else {
                videoFullscreen_ = false;
                fullscreenTabId_ = -1;
                return;
            }
        } else {
            if (!videoFullscreen_ || tabId != fullscreenTabId_) {
                return;
            }

            videoFullscreen_ = false;
            fullscreenTabId_ = -1;

            SetWindowLongPtrW(hwnd_, GWL_STYLE, windowedStyle_);
            SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, windowedExStyle_);
            SetWindowPlacement(hwnd_, &windowedPlacement_);

            SetWindowPos(
                hwnd_,
                (windowedExStyle_ & WS_EX_TOPMOST)
                    ? HWND_TOPMOST
                    : HWND_NOTOPMOST,
                0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED |
                    SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        }

        ResizeUI();
    }

    void ResizeUI() {
        if (!hwnd_) {
            return;
        }

        RECT client{};
        GetClientRect(hwnd_, &client);

        const int width = client.right;
        const int height = client.bottom;

        if (tabBar_) {
            ShowWindow(
                tabBar_->GetHandle(),
                videoFullscreen_ ? SW_HIDE : SW_SHOW);
            tabBar_->Resize(
                0, 0, width, kTabBarHeight);
        }

        if (searchBar_) {
            ShowWindow(
                searchBar_->GetHandle(),
                videoFullscreen_ ? SW_HIDE : SW_SHOW);
            searchBar_->Resize(
                0, kTabBarClientOverlap,
                width, kSearchBarHeight);
        }

        for (auto& tab : tabs_) {
            if (tab.browser &&
                tab.id == GetActiveTabId()) {
                ShowBrowser(tab);
            }
        }

        if (settingsPage_ && settingsPage_->visible()) {
            ShowSettingsPage();
        }

        (void)height;
    }

    void GetBrowserBounds(RECT& rect) const {
        RECT client{};
        GetClientRect(hwnd_, &client);

        // Keep a tiny two-pixel breathing edge around the page viewport.
        // It prevents the rounded browser surface from being clipped against
        // the host window while remaining visually unobtrusive.
        constexpr int pageInset = 2;
        const int top = videoFullscreen_ ? 0 : kBrowserTop;
        rect.left = videoFullscreen_ ? 0 : pageInset;
        rect.top = videoFullscreen_ ? 0 : top + pageInset;
        const int availableRight =
            static_cast<int>(client.right) -
            (videoFullscreen_ ? 0 : pageInset);
        rect.right = availableRight > rect.left ? availableRight : rect.left;
        const int availableBottom =
            static_cast<int>(client.bottom) -
            (videoFullscreen_ ? 0 : pageInset);
        rect.bottom =
            availableBottom > rect.top ? availableBottom : rect.top;
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

    for (auto& tab : tabs_) {
        if (tab.browser) {
            tab.closingBrowser = true;
            tab.browser->GetHost()->CloseBrowser(true);
        }
    }

    FinishShutdownIfReady();
 }

private:
    void UpdateWindowIcon() {
        if (!hwnd_ || !gdiplusToken_) return;
        HICON large = CreateAccentFlowerIcon(Theme::Accent());
        if (!large) return;  // Keep the last valid icon if the source is unavailable.
        HICON iconSmall = CopyIcon(large);
        if (!iconSmall) iconSmall = large;
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large));
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall));
        // Windows 7+ taskbar can request ICON_SMALL2 separately from ICON_SMALL.
        // Update that slot too, otherwise the caption icon can change while
        // the running taskbar button keeps showing the old cached artwork.
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL2, reinterpret_cast<LPARAM>(iconSmall));

        // Explorer and the taskbar may query the window-class icons rather
        // than the per-window WM_SETICON values. Keep both sources in sync so
        // the live accent icon is visible outside the client area as well.
        SetClassLongPtrW(hwnd_, GCLP_HICON, reinterpret_cast<LONG_PTR>(large));
        SetClassLongPtrW(hwnd_, GCLP_HICONSM, reinterpret_cast<LONG_PTR>(iconSmall));
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);

        // Explorer can cache the taskbar button's icon after the window was
        // first added. Re-register the existing button after changing its
        // WM_SETICON values so the shell obtains the newly rendered accent.
        ITaskbarList* taskbar = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&taskbar)))) {
            if (SUCCEEDED(taskbar->HrInit())) {
                taskbar->DeleteTab(hwnd_);
                taskbar->AddTab(hwnd_);
            }
            taskbar->Release();
        }

        if (windowIconLarge_) DestroyIcon(windowIconLarge_);
        if (windowIconSmall_ && windowIconSmall_ != windowIconLarge_) DestroyIcon(windowIconSmall_);
        windowIconLarge_ = large;
        windowIconSmall_ = iconSmall;

        // The caption and the running taskbar button now show the accent. The
        // pinned taskbar button, desktop / Start shortcuts and vibrium.exe read
        // their icon from files, so update those too (debounced, off-thread).
        if (!SelfTest::Get().enabled()) {
            AccentIcon::Request(Theme::Accent());
        }
    }

    std::wstring GetNewTabUrl() const {
        if (SelfTest::Get().enabled()) {
            return SelfTest::Get().start_url();
        }

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

        wchar_t accent[32]{};
        const COLORREF color = Theme::Accent();
        swprintf_s(accent, L"?vibriumAccent=%02X%02X%02X",
            GetRValue(color), GetGValue(color), GetBValue(color));
        return L"file:///" + encoded + accent;
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
        case WM_GETICON:
            // Return the current accent-rendered icon for every icon size the
            // shell may request. In particular, taskbar buttons often ask for
            // ICON_SMALL2 rather than the caption's ICON_SMALL slot.
            if (wParam == ICON_BIG && self->windowIconLarge_) {
                return reinterpret_cast<LRESULT>(self->windowIconLarge_);
            }
            if ((wParam == ICON_SMALL || wParam == ICON_SMALL2) &&
                self->windowIconSmall_) {
                return reinterpret_cast<LRESULT>(self->windowIconSmall_);
            }
            break;

        case WM_CREATE:
            return self->InitializeUI() ? 0 : -1;

        // Leave WM_NCCALCSIZE and all non-client messages to Windows. This
        // preserves the genuine title bar, resize frame and caption buttons.

        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT client{};
            GetClientRect(hwnd, &client);
            HBRUSH contentBrush = CreateSolidBrush(RGB(18, 18, 20));
            FillRect(hdc, &client, contentBrush);
            DeleteObject(contentBrush);
            return 1;
        }

        case WM_DWMCOLORIZATIONCOLORCHANGED:
            // Vibrium owns its accent palette. Windows accent changes must not
            // alter the browser theme or its dynamically tinted app icon.
            return 0;

        case WM_SYSCOLORCHANGE:
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;

        case WM_SIZE:
        case WM_MOVE:
            self->ResizeUI();
            return 0;

        case WM_TIMER:
            if (wParam == kMemoryTimer) {
                self->CheckInactiveTabs();
            } else if (wParam == kSelfTestTimer) {
                KillTimer(hwnd, kSelfTestTimer);
                SelfTest::Get().OnTimeout();
                self->RequestClose();
            }
            return 0;

        case WM_CLOSE:
            self->RequestClose();
            return 0;

        // Defer final window destruction until the current CEF callback has
        // returned. Destroying the parent HWND from OnBeforeClose can re-enter
        // CEF's native window teardown while it is still dispatching callbacks.
        case kFinalizeShutdownMessage:
            self->FinalizeShutdown();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, kMemoryTimer);
            // This application uses CefRunMessageLoop(), so use CEF's matching
            // quit API rather than injecting WM_QUIT into its Windows queue.
            CefQuitMessageLoop();
            return 0;

        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            self->hwnd_ = nullptr;
            return DefWindowProcW(
                hwnd, message, wParam, lParam);

        default:
            break;
        }

        // Keep every path through this window procedure returning a result.
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    bool AllBrowsersClosed() const {
        for (const auto& tab : tabs_) {
            // A browser creation can complete asynchronously after shutdown
            // starts. OnBrowserCreated() closes it, and OnBeforeClose() clears
            // the record before finalization is allowed.
            if (tab.browser || tab.browserCreating) {
                return false;
            }
        }
        return true;
    }

    void FinishShutdownIfReady() {
        if (!closeRequested_ || !hwnd_ || shutdownFinalizePosted_ ||
            !AllBrowsersClosed()) {
            return;
        }

        // Never destroy the parent window synchronously from a CEF lifecycle
        // callback. Queue finalization onto the host window's message queue so
        // CEF can finish the current OnBeforeClose dispatch first.
        shutdownFinalizePosted_ = true;
        if (!PostMessageW(hwnd_, kFinalizeShutdownMessage, 0, 0)) {
            shutdownFinalizePosted_ = false;
        }
    }

    void FinalizeShutdown() {
        shutdownFinalizePosted_ = false;
        if (!closeRequested_ || !hwnd_ || !AllBrowsersClosed()) {
            return;
        }

        KillTimer(hwnd_, kMemoryTimer);
        KillTimer(hwnd_, kSelfTestTimer);
        DestroyWindow(hwnd_);
    }

private:
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
    HICON windowIconSmall_ = nullptr;
    HICON windowIconLarge_ = nullptr;

    bool videoFullscreen_ = false;
    int fullscreenTabId_ = -1;
    LONG_PTR windowedStyle_ = 0;
    LONG_PTR windowedExStyle_ = 0;
    WINDOWPLACEMENT windowedPlacement_{};

    std::unique_ptr<TabBar> tabBar_;
    std::unique_ptr<SearchEngine> searchBar_;
    std::unique_ptr<SettingsPage> settingsPage_;

    std::vector<TabRecord> tabs_;

    int activeTab_ = -1;
    int nextTabId_ = 1;

    bool closeRequested_ = false;
    bool shutdownFinalizePosted_ = false;

    friend class BrowserClient;
};

// Downloads and decodes a page-provided PNG favicon asynchronously.
class FaviconRequestClient final : public CefURLRequestClient {
public:
    using Completion = std::function<void(HBITMAP)>;

    explicit FaviconRequestClient(Completion completion)
        : completion_(std::move(completion)) {}

    void Start(CefRefPtr<CefRequest> request) {
        request_ = CefURLRequest::Create(request, this, nullptr);
    }

    void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
        HBITMAP bitmap = nullptr;
        if (request && request->GetRequestStatus() == UR_SUCCESS &&
            !data_.empty()) {
            CefRefPtr<CefImage> image = CefImage::CreateImage();
            if (image && image->AddPNG(1.0f, data_.data(), data_.size())) {
                int width = 0;
                int height = 0;
                CefRefPtr<CefBinaryValue> pixels = image->GetAsBitmap(
                    1.0f,
                    CEF_COLOR_TYPE_BGRA_8888,
                    CEF_ALPHA_TYPE_PREMULTIPLIED,
                    width,
                    height);
                if (pixels && width > 0 && height > 0 &&
                    width <= 256 && height <= 256) {
                    BITMAPINFO info{};
                    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    info.bmiHeader.biWidth = width;
                    info.bmiHeader.biHeight = -height;
                    info.bmiHeader.biPlanes = 1;
                    info.bmiHeader.biBitCount = 32;
                    info.bmiHeader.biCompression = BI_RGB;

                    void* bits = nullptr;
                    HDC screen = GetDC(nullptr);
                    bitmap = CreateDIBSection(
                        screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
                    ReleaseDC(nullptr, screen);

                    const size_t byteCount =
                        static_cast<size_t>(width) *
                        static_cast<size_t>(height) * 4;
                    if (!bitmap || !bits ||
                        pixels->GetSize() < byteCount ||
                        pixels->GetData(bits, byteCount, 0) != byteCount) {
                        if (bitmap) DeleteObject(bitmap);
                        bitmap = nullptr;
                    }
                }
            }
        }

        if (completion_) {
            completion_(bitmap);
            completion_ = nullptr;
        }
        request_ = nullptr;
    }

    void OnUploadProgress(
        CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}

    void OnDownloadProgress(
        CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}

    void OnDownloadData(
        CefRefPtr<CefURLRequest>,
        const void* data,
        size_t dataLength) override {
        constexpr size_t kMaxFaviconBytes = 512 * 1024;
        if (!data || dataLength == 0 ||
            data_.size() + dataLength > kMaxFaviconBytes) {
            return;
        }
        const auto* bytes = static_cast<const char*>(data);
        data_.append(bytes, dataLength);
    }

    bool GetAuthCredentials(
        bool,
        const CefString&,
        int,
        const CefString&,
        const CefString&,
        CefRefPtr<CefAuthCallback>) override {
        return false;
    }

private:
    std::string data_;
    Completion completion_;
    CefRefPtr<CefURLRequest> request_;

    IMPLEMENT_REFCOUNTING(FaviconRequestClient);
};

// ------------------------------------------------------------
// CEF callbacks
// ------------------------------------------------------------

void BrowserClient::OnFaviconURLChange(
    CefRefPtr<CefBrowser>,
    const std::vector<CefString>& iconUrls) {
    if (iconUrls.empty()) {
        return;
    }

    const std::wstring iconUrl = iconUrls.front().ToWString();
    if (iconUrl.rfind(L"https://", 0) != 0 &&
        iconUrl.rfind(L"http://", 0) != 0) {
        return;
    }

    CefRefPtr<CefRequest> request = CefRequest::Create();
    request->SetURL(iconUrls.front());
    request->SetMethod("GET");

    CefRefPtr<FaviconRequestClient> client =
        new FaviconRequestClient(
            [owner = owner_, tabId = tabId_](HBITMAP bitmap) {
                if (owner) {
                    owner->OnFaviconReady(tabId, bitmap);
                } else if (bitmap) {
                    DeleteObject(bitmap);
                }
            });
    client->Start(request);
}

void BrowserClient::OnAfterCreated(
    CefRefPtr<CefBrowser> browser) {

    owner_->OnBrowserCreated(tabId_, browser);
}

void BrowserClient::OnFullscreenModeChange(
    CefRefPtr<CefBrowser> browser,
    bool fullscreen) {
    owner_->SetVideoFullscreen(tabId_, fullscreen);
}

void BrowserClient::OnBeforeClose(
    CefRefPtr<CefBrowser> browser) {

    AdblockService::Get().OnBrowserClosed(browser);
    owner_->OnBrowserClosed(tabId_);
}

void BrowserClient::OnLoadStart(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    TransitionType transition_type) {

    AdblockService::Get().ApplyCosmeticFilters(frame);
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

void BrowserClient::OnAudioStreamPacket(
    CefRefPtr<CefBrowser> browser,
    const float** data,
    int frames,
    int64_t pts) {
    // Audio packet processing is intentionally not implemented yet.
}

void BrowserClient::OnAudioStreamError(
    CefRefPtr<CefBrowser> browser,
    const CefString& message) {
    // Audio error reporting can be added here later.
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
    // DEVELOPMENT ONLY: a release build must run sandboxed (see
    // c++/adblock/README.md, "Sandbox").
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = false;

    if (!CefInitialize(
            mainArgs,
            settings,
            app,
            nullptr)) {
        return 1;
    }

    vibrium::SelfTest::Get().Configure(CefCommandLine::GetGlobalCommandLine());
    vibrium::AdblockService::Get().Initialize(
        CefCommandLine::GetGlobalCommandLine());

    int appExitCode = 1;
    {
        // Destroy the window and its TabRecord/BrowserClient references before
        // tearing down the process-wide CEF services.
        vibrium::MainWindow mainWindow;

        if (!mainWindow.Create(instance)) {
            vibrium::AdblockService::Get().Shutdown();
            CefShutdown();
            app = nullptr;
            return 1;
        }

        CefRunMessageLoop();
        appExitCode = vibrium::SelfTest::Get().exit_code();

        // Must run while GDI+ is alive (it is shut down by ~MainWindow below).
        vibrium::AccentIcon::Shutdown(
            vibrium::Theme::Accent(), !vibrium::SelfTest::Get().enabled());
    }

    // All browsers have delivered OnBeforeClose before the message loop exits.
    // Release router/request-handler refs before CefShutdown; releasing CEF
    // objects from static destructors after CefShutdown is unsafe.
    vibrium::AdblockService::Get().Shutdown();
    CefShutdown();
    app = nullptr;

    return appExitCode;
}