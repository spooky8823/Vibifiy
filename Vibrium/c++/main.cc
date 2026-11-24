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
#include "include/cef_context_menu_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_audio_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_request_context.h"

#include "tabbar.h"
#include "searchengine.h"
#include "settings_page.h"
#include "history.h"
#include "downloads.h"
#include "update_launcher.h"
#include "accent_icon.h"
#include "resource.h"
#include "theme.h"
#include "adblock/adblock_service.h"
#include "adblock/request_policy.h"
#include "adblock/scriptlet_engine.h"
#include <cwctype>
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
// Posted by BrowserClient when a page asks for a new window/tab. lParam is
// an OpenTabRequest* (owned by the receiver).
constexpr UINT kOpenTabMessage = WM_APP + 2;
// Posted when a download ended so hidden browsers kept alive for it can go.
constexpr UINT kReapRetiredMessage = WM_APP + 3;
// Custom context-menu command (must be >= MENU_ID_USER_FIRST).
constexpr int kMenuOpenLinkInNewTab = MENU_ID_USER_FIRST + 1;

// A closed tab's browser, kept alive (hidden) because a download it started
// is still running: CEF cancels a download when its browser is destroyed.
struct RetiredBrowser {
    CefRefPtr<CefBrowser> browser;
    CefRefPtr<CefClient> client;
    int id = 0;
    bool closing = false;
};

struct OpenTabRequest {
    std::wstring url;
    bool activate = true;
};
constexpr UINT kMemoryCheckInterval = 15000;
constexpr ULONGLONG kUnloadTimeoutMs = 20ULL * 60ULL * 1000ULL;

constexpr int kTabBarHeight = 46;
constexpr int kTabBarClientOverlap = 9;
constexpr int kCaptionButtonsWidth = 138;
constexpr int kSearchBarTop = kTabBarHeight + 1;
constexpr int kSearchBarHeight = 47;  // was 52: 5px thinner
constexpr int kBrowserTop = kSearchBarTop + kSearchBarHeight;
// Exact gap, in pixels, between the bottom edge of the tab strip and the top
// edge of the address pill.
constexpr int kTabToAddressGap = 5;
// The address pill is drawn this many pixels inside the search bar window
// (must match kOuterPadding in searchengine.cc).
constexpr int kAddressPillInset = 1;

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

    // Browser process: runs before CEF parses its command line. Vibrium draws
    // its own download panel, so switch off Chrome's built-in download UI.
    void OnBeforeCommandLineProcessing(
        const CefString& process_type,
        CefRefPtr<CefCommandLine> command_line) override {
        if (!process_type.empty() || !command_line) return;
        std::string features =
            command_line->GetSwitchValue("disable-features").ToString();
        for (const char* feature : {"DownloadBubble", "DownloadBubbleV2"}) {
            if (features.find(feature) != std::string::npos) continue;
            if (!features.empty()) features += ",";
            features += feature;
        }
        command_line->AppendSwitchWithValue("disable-features", features);
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
// Branded window for sign-in popups (Google / Apple)
// ------------------------------------------------------------
//
// Sign-in flows talk back to the page that opened them through
// window.opener, so they cannot be turned into plain tabs. They get a real
// popup window instead, dressed in Vibrium's look: accent caption and border,
// Vibrium icon, dark address strip that always shows the site's real host.

namespace {

constexpr wchar_t kAuthPopupClass[] = L"VibriumAuthPopup";
constexpr int kAuthBarHeight = 30;
constexpr int kAuthBorder = 2;

std::wstring AuthHostOf(const std::wstring& url) {
    const size_t scheme = url.find(L"://");
    if (scheme == std::wstring::npos) return L"";
    const size_t start = scheme + 3;
    const size_t end = url.find_first_of(L"/?#", start);
    std::wstring host = url.substr(
        start, end == std::wstring::npos ? end : end - start);
    const size_t at = host.rfind(L'@');
    if (at != std::wstring::npos) host.erase(0, at + 1);
    const size_t colon = host.find(L':');
    if (colon != std::wstring::npos) host.erase(colon);
    for (auto& ch : host) ch = static_cast<wchar_t>(std::towlower(ch));
    return host;
}

bool AuthHostMatches(const std::wstring& host, const std::wstring& domain) {
    if (host == domain) return true;
    return host.size() > domain.size() &&
           host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
           host[host.size() - domain.size() - 1] == L'.';
}

// Add more sign-in providers here.
bool IsAuthPopupUrl(const std::wstring& url) {
    if (url.compare(0, 8, L"https://") != 0) return false;
    const std::wstring host = AuthHostOf(url);
    static const wchar_t* const kDomains[] = {
        L"accounts.google.com",
        L"appleid.apple.com",
        L"idmsa.apple.com",
    };
    for (const wchar_t* domain : kDomains) {
        if (AuthHostMatches(host, domain)) return true;
    }
    return false;
}

}  // namespace

class AuthPopup final : public CefClient,
                        public CefLifeSpanHandler,
                        public CefDisplayHandler {
public:
    explicit AuthPopup(HWND mainWindow) : main_(mainWindow) {}

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }

    // Creates the (still hidden) frame window. width/height of 0 = default.
    bool CreateFrame(int width, int height) {
        HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = FrameProc;
        wc.hInstance = instance;
        wc.lpszClassName = kAuthPopupClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        if (!RegisterClassExW(&wc) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        RECT mainRect{};
        GetWindowRect(main_, &mainRect);
        const int mainWidth = mainRect.right - mainRect.left;
        const int mainHeight = mainRect.bottom - mainRect.top;

        const int outerW = std::clamp(width > 0 ? width + 16 : 520, 380,
                                      std::max(380, mainWidth));
        const int outerH = std::clamp(height > 0 ? height + 39 + kAuthBarHeight : 720,
                                      420, std::max(420, mainHeight));
        const int x = mainRect.left + (mainWidth - outerW) / 2;
        const int y = mainRect.top + (mainHeight - outerH) / 2;

        // Owned by the main window: stays above it, closes with it.
        frame_ = CreateWindowExW(
            0, kAuthPopupClass, L"Vibrium", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            x, y, outerW, outerH, main_, nullptr, instance, this);
        if (!frame_) return false;

        const COLORREF accent = Theme::Accent();
        DwmSetWindowAttribute(frame_, DWMWA_CAPTION_COLOR, &accent, sizeof(accent));
        DwmSetWindowAttribute(frame_, DWMWA_BORDER_COLOR, &accent, sizeof(accent));
        const COLORREF captionText = RGB(255, 255, 255);
        DwmSetWindowAttribute(frame_, DWMWA_TEXT_COLOR, &captionText,
                              sizeof(captionText));

        // Same (accent-tinted) icon as the main window.
        // (not named "small": Windows headers #define it as char)
        const LRESULT iconBig = SendMessageW(main_, WM_GETICON, ICON_BIG, 0);
        const LRESULT iconSmall = SendMessageW(main_, WM_GETICON, ICON_SMALL, 0);
        if (iconBig) SendMessageW(frame_, WM_SETICON, ICON_BIG, iconBig);
        if (iconSmall) SendMessageW(frame_, WM_SETICON, ICON_SMALL, iconSmall);
        return true;
    }

    // Tells CEF to host the popup inside the frame.
    void PrepareWindowInfo(CefWindowInfo& info) {
        RECT area = BrowserArea();
        info.SetAsChild(frame_, CefRect(area.left, area.top,
                                        area.right - area.left,
                                        area.bottom - area.top));
    }

    static void CloseAll() {
        const auto copy = Open();
        for (AuthPopup* popup : copy) {
            if (popup->browser_) popup->browser_->GetHost()->CloseBrowser(true);
        }
    }

    // ---- CefLifeSpanHandler ----
    bool OnBeforePopup(
        CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
        int popup_id, const CefString& target_url,
        const CefString& target_frame_name,
        WindowOpenDisposition target_disposition, bool user_gesture,
        const CefPopupFeatures& popupFeatures, CefWindowInfo& windowInfo,
        CefRefPtr<CefClient>& client, CefBrowserSettings& settings,
        CefRefPtr<CefDictionaryValue>& extra_info,
        bool* no_javascript_access) override {
        // Anything the sign-in window itself opens goes to a tab.
        auto* request = new OpenTabRequest{target_url.ToWString(), true};
        if (!PostMessageW(main_, kOpenTabMessage, 0,
                          reinterpret_cast<LPARAM>(request))) {
            delete request;
        }
        return true;
    }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        browser_ = browser;
        if (!frame_) {  // frame vanished while the browser was being created
            browser->GetHost()->CloseBrowser(true);
            return;
        }
        Open().push_back(this);
        LayoutBrowser();
        ShowWindow(frame_, SW_SHOW);
        SetForegroundWindow(frame_);
    }

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
        auto& list = Open();
        list.erase(std::remove(list.begin(), list.end(), this), list.end());
        browser_ = nullptr;
        if (frame_) {
            HWND frame = frame_;
            frame_ = nullptr;
            SetWindowLongPtrW(frame, GWLP_USERDATA, 0);
            ShowWindow(frame, SW_HIDE);
            // Destroy from the message loop, not from inside this callback.
            PostMessageW(frame, WM_CLOSE, 0, 0);
        }
    }

    // ---- CefDisplayHandler ----
    void OnTitleChange(CefRefPtr<CefBrowser> browser,
                       const CefString& title) override {
        if (frame_) SetWindowTextW(frame_, title.ToWString().c_str());
    }

    void OnAddressChange(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         const CefString& url) override {
        if (!frame || !frame->IsMain()) return;
        host_ = AuthHostOf(url.ToWString());
        if (frame_) InvalidateRect(frame_, nullptr, FALSE);
    }

private:
    static std::vector<AuthPopup*>& Open() {
        static std::vector<AuthPopup*> open;
        return open;
    }

    RECT BrowserArea() const {
        RECT client{};
        GetClientRect(frame_, &client);
        return RECT{kAuthBorder, kAuthBarHeight, client.right - kAuthBorder,
                    client.bottom - kAuthBorder};
    }

    void LayoutBrowser() {
        if (!frame_ || !browser_) return;
        HWND window = browser_->GetHost()->GetWindowHandle();
        if (!window) return;
        const RECT area = BrowserArea();
        MoveWindow(window, area.left, area.top,
                   std::max<LONG>(0, area.right - area.left),
                   std::max<LONG>(0, area.bottom - area.top), TRUE);
        browser_->GetHost()->WasResized();
    }

    void Paint() {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(frame_, &ps);
        RECT client{};
        GetClientRect(frame_, &client);

        // Accent perimeter around everything...
        HBRUSH accentBrush = CreateSolidBrush(Theme::Accent());
        FillRect(hdc, &client, accentBrush);
        DeleteObject(accentBrush);

        // ...and the black address strip on top.
        RECT bar{kAuthBorder, kAuthBorder, client.right - kAuthBorder,
                 kAuthBarHeight};
        HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdc, &bar, black);
        DeleteObject(black);

        HFONT font = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(241, 237, 250));
        RECT text{bar.left + 12, bar.top, bar.right - 12, bar.bottom};
        DrawTextW(hdc, host_.c_str(), static_cast<int>(host_.size()), &text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                      DT_NOPREFIX);
        SelectObject(hdc, oldFont);
        DeleteObject(font);
        EndPaint(frame_, &ps);
    }

    static LRESULT CALLBACK FrameProc(HWND hwnd, UINT message, WPARAM wParam,
                                      LPARAM lParam) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<AuthPopup*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);

        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            self->Paint();
            return 0;
        case WM_SIZE:
            self->LayoutBrowser();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 380;
            info->ptMinTrackSize.y = 420;
            return 0;
        }
        case WM_CLOSE:
            if (self->browser_) {  // let the page's beforeunload run
                self->browser_->GetHost()->CloseBrowser(false);
                return 0;
            }
            break;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            self->frame_ = nullptr;
            break;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    HWND main_ = nullptr;
    HWND frame_ = nullptr;
    CefRefPtr<CefBrowser> browser_;
    std::wstring host_;

    IMPLEMENT_REFCOUNTING(AuthPopup);
};

// ------------------------------------------------------------
// Per-tab browser client
// ------------------------------------------------------------

class BrowserClient final
    : public CefClient,
      public CefLifeSpanHandler,
      public CefDisplayHandler,
      public CefDownloadHandler,
      public CefContextMenuHandler,
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

    CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
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

    void OnLoadEnd(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        int httpStatusCode) override;

    // Links, target=_blank and window.open() become tabs of this window
    // instead of CEF's default popup windows.
    bool OnBeforePopup(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        int popup_id,
        const CefString& target_url,
        const CefString& target_frame_name,
        WindowOpenDisposition target_disposition,
        bool user_gesture,
        const CefPopupFeatures& popupFeatures,
        CefWindowInfo& windowInfo,
        CefRefPtr<CefClient>& client,
        CefBrowserSettings& settings,
        CefRefPtr<CefDictionaryValue>& extra_info,
        bool* no_javascript_access) override;

    void OnBeforeContextMenu(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefContextMenuParams> params,
        CefRefPtr<CefMenuModel> model) override;

    bool OnContextMenuCommand(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefContextMenuParams> params,
        int command_id,
        EventFlags event_flags) override;

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
    // The one browser this client was created for. Any other browser that
    // reaches this client (e.g. a popup) must never replace the tab's browser.
    int primaryBrowserId_ = 0;

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

// Built-in pages that are shown as native views instead of a CEF browser.
enum InternalPage : int {
    kPageNone = 0,
    kPageSettings = 1,
    kPageHistory = 2,
    kPageDownloads = 3,
};

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

    // Which built-in page this tab shows (kPageNone for a normal web tab).
    // Built-in pages are native views with no CEF browser.
    int internalPage = kPageNone;
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
                kSearchBarTop,
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

        searchBar_->SetOnHistory(
            [this]() {
                OpenHistory();
            });

        searchBar_->SetOnDownloads(
            [this]() {
                OpenDownloads();
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

        // History and downloads: load saved data, then create their pages.
        HistoryStore::Get().Load();
        DownloadManager::Get().Load();

        historyPage_ = std::make_unique<HistoryPage>();
        if (!historyPage_->Create(hwnd_, instance_)) {
            historyPage_.reset();
        } else {
            // Clicking an entry opens it in the current tab.
            historyPage_->SetOnOpenUrl([this](const std::wstring& url) {
                NavigateActiveTab(url);
            });
        }

        downloadsPage_ = std::make_unique<DownloadsPage>();
        if (!downloadsPage_->Create(hwnd_, instance_)) {
            downloadsPage_.reset();
        }

        // Vibrium's own download panel (top-right, black with accent border).
        downloadPopup_ = std::make_unique<DownloadPopup>();
        if (!downloadPopup_->Create(hwnd_, instance_)) {
            downloadPopup_.reset();
        } else {
            downloadPopup_->SetOnShowAll([this]() { OpenDownloads(); });
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
        CreateTab(GetNewTabUrl(), L"New Tab", kPageNone);
    }

    // Opens `url` in a new tab of this window (links, target=_blank,
    // window.open, "Open link in new tab").
    void OpenUrlInNewTab(const std::wstring& requestedUrl, bool activate) {
        const std::wstring url =
            requestedUrl.empty() ? std::wstring(L"about:blank") : requestedUrl;

        const int page = InternalPageForUrl(url);
        if (page != kPageNone) {
            OpenInternalPage(page);
            return;
        }
        CreateTab(IsNewTabAlias(url) ? GetNewTabUrl() : url,
                  L"New Tab", kPageNone, activate);
    }

    void CreateTab(
        const std::wstring& url,
        const std::wstring& title,
        int internalPage,
        bool activate = true) {

        TabRecord tab;

        tab.id = nextTabId_++;
        tab.title = title;
        tab.url = url;
        tab.internalPage = internalPage;
        tab.lastActiveTime = GetTickCount64();

        const int id = tab.id;

        tabs_.push_back(std::move(tab));

        const int index = static_cast<int>(tabs_.size()) - 1;

        tabBar_->AddTab(title);
        if (activate) {
            SelectTab(index);
        }

        (void)id;
    }

    // --------------------------------------------------------
    // Built-in pages (native views shown in place of a browser)
    // --------------------------------------------------------

    bool HasInternalPage(int page) const {
        switch (page) {
            case kPageSettings: return settingsPage_ != nullptr;
            case kPageHistory: return historyPage_ != nullptr;
            case kPageDownloads: return downloadsPage_ != nullptr;
            default: return false;
        }
    }

    static const wchar_t* InternalPageUrl(int page) {
        switch (page) {
            case kPageSettings: return kSettingsUrl;
            case kPageHistory: return kHistoryUrl;
            case kPageDownloads: return kDownloadsUrl;
            default: return L"";
        }
    }

    static const wchar_t* InternalPageTitle(int page) {
        switch (page) {
            case kPageSettings: return L"Settings";
            case kPageHistory: return L"History";
            case kPageDownloads: return L"Downloads";
            default: return L"";
        }
    }

    // Which built-in page a typed address refers to (kPageNone if none).
    static int InternalPageForUrl(const std::wstring& url) {
        if (IsSettingsUrl(url)) return kPageSettings;
        if (IsHistoryUrl(url)) return kPageHistory;
        if (IsDownloadsUrl(url)) return kPageDownloads;
        return kPageNone;
    }

    // Opens a built-in page as a tab, or switches to it if it is open.
    void OpenInternalPage(int page) {
        if (!HasInternalPage(page)) {
            return;
        }

        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
            if (tabs_[i].internalPage == page) {
                SelectTab(i);
                return;
            }
        }

        CreateTab(InternalPageUrl(page), InternalPageTitle(page), page);
    }

    void OpenSettings() { OpenInternalPage(kPageSettings); }
    void OpenHistory() { OpenInternalPage(kPageHistory); }
    void OpenDownloads() { OpenInternalPage(kPageDownloads); }

    // Shows one built-in page over the browser area and hides the others.
    void ShowInternalPage(int page) {
        RECT rect{};
        GetBrowserBounds(rect);

        if (page != kPageSettings && settingsPage_) settingsPage_->Hide();
        if (page != kPageHistory && historyPage_) historyPage_->Hide();
        if (page != kPageDownloads && downloadsPage_) downloadsPage_->Hide();

        if (page == kPageSettings && settingsPage_) settingsPage_->Show(rect);
        if (page == kPageHistory && historyPage_) historyPage_->Show(rect);
        if (page == kPageDownloads && downloadsPage_) downloadsPage_->Show(rect);

        HWND shown = nullptr;
        if (page == kPageSettings && settingsPage_) shown = settingsPage_->handle();
        if (page == kPageHistory && historyPage_) shown = historyPage_->handle();
        if (page == kPageDownloads && downloadsPage_) shown = downloadsPage_->handle();
        if (shown) {
            SetWindowPos(shown, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        EnforceSingleVisibleView();
    }

    // Exactly one content view may be visible: the active tab's browser, or
    // the active built-in page. Hides every other child of the main window
    // (the tab strip, address bar and built-in pages excepted), so a browser
    // window that lost its tab record can never cover a native page.
    void EnforceSingleVisibleView() {
        if (!hwnd_) return;

        HWND keep = nullptr;
        if (TabRecord* active = GetActiveTab()) {
            if (!active->internalPage && active->browser) {
                keep = active->browser->GetHost()->GetWindowHandle();
            }
        }

        const HWND known[] = {
            tabBar_ ? tabBar_->GetHandle() : nullptr,
            searchBar_ ? searchBar_->GetHandle() : nullptr,
            settingsPage_ ? settingsPage_->handle() : nullptr,
            historyPage_ ? historyPage_->handle() : nullptr,
            downloadsPage_ ? downloadsPage_->handle() : nullptr,
        };

        for (HWND child = GetWindow(hwnd_, GW_CHILD); child;
             child = GetWindow(child, GW_HWNDNEXT)) {
            if (child == keep) continue;
            bool ours = false;
            for (HWND h : known) {
                if (h && h == child) ours = true;
            }
            if (ours) continue;
            if (IsWindowVisible(child)) ShowWindow(child, SW_HIDE);
        }
    }

    void HideInternalPages() {
        if (settingsPage_) settingsPage_->Hide();
        if (historyPage_) historyPage_->Hide();
        if (downloadsPage_) downloadsPage_->Hide();
    }

    // --------------------------------------------------------
    // Tab selection
    // --------------------------------------------------------

    void SelectTab(int index) {
        if (index < 0 ||
            index >= static_cast<int>(tabs_.size())) {
            return;
        }

        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
            if (i == index) continue;
            TabRecord& other = tabs_[i];

            if (other.browser) {
                HWND otherWindow =
                    other.browser->GetHost()->GetWindowHandle();

                if (otherWindow) {
                    ShowWindow(otherWindow, SW_HIDE);
                }

                other.browser->GetHost()->WasHidden(true);
            }
        }

        activeTab_ = index;

        TabRecord& selected = tabs_[activeTab_];

        selected.lastActiveTime = GetTickCount64();

        tabBar_->SetActiveTab(index);

        UpdateAddressBar();

        if (selected.internalPage) {
            ShowInternalPage(selected.internalPage);
            return;
        }

        HideInternalPages();

        if (selected.browser) {
            ShowBrowser(selected);
        } else if (!selected.browserCreating &&
                   !selected.closingBrowser) {
            CreateBrowserForTab(selected);
        }

        EnforceSingleVisibleView();
    }

    // --------------------------------------------------------
    // Browser creation
    // --------------------------------------------------------

    void CreateBrowserForTab(TabRecord& tab) {
        if (tab.internalPage ||
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

        // Alloy style: no Chrome UI around the page (that is where the
        // built-in download bubble came from). Vibrium draws its own.
        windowInfo.runtime_style = CEF_RUNTIME_STYLE_ALLOY;

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

    if (tabId == GetActiveTabId() && !tab->internalPage) {
        ShowBrowser(*tab);
    } else {
        browser->GetHost()->WasHidden(true);

        HWND window = browser->GetHost()->GetWindowHandle();

        if (window) {
            ShowWindow(window, SW_HIDE);
        }
    }

    EnforceSingleVisibleView();
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

        if (wasActive && tabs_[index].internalPage) {
            HideInternalPages();
        }

        if (tabs_[index].browser) {
            // Hide first: CEF destroys the window asynchronously, and the
            // tab record is gone by then.
            if (HWND closingWindow =
                    tabs_[index].browser->GetHost()->GetWindowHandle()) {
                ShowWindow(closingWindow, SW_HIDE);
            }
            const int browserId = tabs_[index].browser->GetIdentifier();
            if (!closeRequested_ &&
                DownloadManager::Get().HasActiveForBrowser(browserId)) {
                // Closing the browser would cancel its download. Keep it
                // alive, hidden, until the download is done.
                retired_.push_back({tabs_[index].browser,
                                    tabs_[index].client, browserId, false});
                tabs_[index].browser->GetHost()->WasHidden(true);
            } else {
                tabs_[index].closingBrowser = true;
                tabs_[index].browser->GetHost()->CloseBrowser(true);
            }
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

        if (selected.internalPage) {
            ShowInternalPage(selected.internalPage);
        } else if (selected.browser) {
            ShowBrowser(selected);
        } else if (!selected.browserCreating &&
                   !selected.closingBrowser) {
            CreateBrowserForTab(selected);
        }

        EnforceSingleVisibleView();

        (void)closingId;
    }

    // --------------------------------------------------------
    // Navigation
    // --------------------------------------------------------

    void NavigateActiveTab(const std::wstring& requestedUrl) {
    TabRecord* tab = GetActiveTab();

    if (!tab) {
        return;
    }

    // "vibrium://newtab" loads the new tab page in this tab.
    const std::wstring url =
        IsNewTabAlias(requestedUrl) ? GetNewTabUrl() : requestedUrl;

    // "vibrium://settings", "vibrium://history" and "vibrium://downloads"
    // open (or switch to) the matching built-in page tab.
    const int page = InternalPageForUrl(url);
    if (page != kPageNone) {
        if (tab->internalPage == page) {
            UpdateAddressBar();
        } else {
            OpenInternalPage(page);
        }
        return;
    }

    // Navigating away from a built-in page turns its tab into a normal tab.
    if (tab->internalPage) {
        tab->internalPage = kPageNone;
        HideInternalPages();
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
            // The new tab page shows an empty address bar with the caret
            // blinking, instead of its long file:// URL.
            if (!tab->internalPage && IsNewTabPageUrl(tab->url)) {
                searchBar_->SetText(L"");
                searchBar_->FocusAddress();
            } else {
                searchBar_->SetText(tab->url);
            }
        }
    }

    // Called when a tab finishes loading its main frame. The page may have
    // taken keyboard focus while loading, so give it back to the address bar
    // when this is the visible new tab page.
    void OnTabLoadEnd(int tabId) {
        // Log the visit in the history (web pages only).
        if (TabRecord* loaded = FindTab(tabId)) {
            if (!loaded->internalPage &&
                HistoryStore::ShouldRecord(loaded->url)) {
                HistoryStore::Get().AddVisit(
                    loaded->url,
                    loaded->title == L"New Tab" ? std::wstring() : loaded->title);
            }
        }

        if (tabId != GetActiveTabId() || !searchBar_) return;
        TabRecord* tab = GetActiveTab();
        if (tab && !tab->internalPage && IsNewTabPageUrl(tab->url)) {
            searchBar_->FocusAddress();
        }
    }

    static std::wstring LowerCopy(const std::wstring& value) {
        std::wstring out = value;
        for (auto& ch : out) ch = static_cast<wchar_t>(std::towlower(ch));
        return out;
    }

    // True for "vibrium://newtab" (any case, optional trailing slash).
    static bool IsNewTabAlias(const std::wstring& url) {
        std::wstring v = LowerCopy(url);
        while (!v.empty() && (v.back() == L'/' || v.back() == L' ')) v.pop_back();
        return v == L"vibrium://newtab";
    }

    // True when `url` is the built-in new tab page (ignoring the ?query/#hash).
    bool IsNewTabPageUrl(const std::wstring& url) const {
        if (url.empty()) return false;
        if (IsNewTabAlias(url)) return true;
        auto strip = [](std::wstring v) {
            const size_t cut = v.find_first_of(L"?#");
            if (cut != std::wstring::npos) v.erase(cut);
            return LowerCopy(v);
        };
        return strip(url) == strip(GetNewTabUrl());
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

    if (!tab->internalPage) {
        HistoryStore::Get().UpdateTitle(tab->url, tab->title);
    }

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
        if (tab.playingAudio) return true;
        return tab.browser &&
               DownloadManager::Get().HasActiveForBrowser(
                   tab.browser->GetIdentifier());
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
            // The tab strip is an overlay that covers the native caption row,
            // so the client area starts below the strip's top. Place the
            // address bar relative to the strip's real bottom edge so the
            // gap between the two is exactly kTabToAddressGap pixels.
            if (tabBar_ && tabBar_->GetHandle()) {
                RECT stripRect{};
                GetWindowRect(tabBar_->GetHandle(), &stripRect);
                POINT clientOrigin{0, 0};
                ClientToScreen(hwnd_, &clientOrigin);
                const int top = static_cast<int>(stripRect.bottom) +
                    kTabToAddressGap - kAddressPillInset -
                    static_cast<int>(clientOrigin.y);
                searchBarTop_ = top > 0 ? top : 0;
            }

            ShowWindow(
                searchBar_->GetHandle(),
                videoFullscreen_ ? SW_HIDE : SW_SHOW);
            searchBar_->Resize(
                0, searchBarTop_,
                width, kSearchBarHeight);
        }

        for (auto& tab : tabs_) {
            if (tab.browser &&
                tab.id == GetActiveTabId()) {
                ShowBrowser(tab);
            }
        }

        if (TabRecord* active = GetActiveTab()) {
            if (active->internalPage) {
                ShowInternalPage(active->internalPage);
            }
        }

        RepositionDownloadPopup();

        (void)height;
    }

    // Anchors the download panel to the top-right corner of the page area.
    void RepositionDownloadPopup() {
        if (!downloadPopup_ || !hwnd_) return;
        RECT client{};
        GetClientRect(hwnd_, &client);
        POINT anchor{client.right - 14, searchBarTop_ + kSearchBarHeight + 6};
        ClientToScreen(hwnd_, &anchor);
        downloadPopup_->SetAnchor(anchor);
    }

    void GetBrowserBounds(RECT& rect) const {
        RECT client{};
        GetClientRect(hwnd_, &client);

        // Keep a tiny two-pixel breathing edge around the page viewport.
        // It prevents the rounded browser surface from being clipped against
        // the host window while remaining visually unobtrusive.
        constexpr int pageInset = 2;
        const int top = videoFullscreen_
            ? 0
            : searchBarTop_ + kSearchBarHeight;
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

    AuthPopup::CloseAll();

    {
        std::vector<CefRefPtr<CefBrowser>> toClose;
        for (auto& entry : retired_) {
            if (entry.closing) continue;
            entry.closing = true;
            toClose.push_back(entry.browser);
        }
        for (auto& browser : toClose) {
            browser->GetHost()->CloseBrowser(true);
        }
    }

    for (auto& tab : tabs_) {
        if (tab.browser) {
            tab.closingBrowser = true;
            tab.browser->GetHost()->CloseBrowser(true);
        }
    }

    FinishShutdownIfReady();
 }

    void PostReapRetired() {
        if (hwnd_) PostMessageW(hwnd_, kReapRetiredMessage, 0, 0);
    }

    // Closes hidden browsers whose downloads have finished.
    void ReapRetiredBrowsers() {
        std::vector<CefRefPtr<CefBrowser>> toClose;
        for (auto& entry : retired_) {
            if (entry.closing) continue;
            if (DownloadManager::Get().HasActiveForBrowser(entry.id)) continue;
            entry.closing = true;
            toClose.push_back(entry.browser);
        }
        for (auto& browser : toClose) {
            browser->GetHost()->CloseBrowser(true);
        }
    }

    void OnRetiredBrowserClosed(int browserId) {
        retired_.erase(
            std::remove_if(retired_.begin(), retired_.end(),
                           [&](const RetiredBrowser& r) {
                               return r.id == browserId;
                           }),
            retired_.end());
        if (closeRequested_) FinishShutdownIfReady();
    }

    // Safe to call from CEF callbacks: the tab is created from the window's
    // own message loop, not from inside the callback.
    void QueueOpenInNewTab(const std::wstring& url, bool activate) {
        if (!hwnd_ || closeRequested_) return;
        auto* request = new OpenTabRequest{url, activate};
        if (!PostMessageW(hwnd_, kOpenTabMessage, 0,
                          reinterpret_cast<LPARAM>(request))) {
            delete request;
        }
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
                HistoryStore::Get().Flush();
                self->CheckInactiveTabs();
                self->ReapRetiredBrowsers();
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

        case kReapRetiredMessage:
            self->ReapRetiredBrowsers();
            return 0;

        case kOpenTabMessage: {
            std::unique_ptr<OpenTabRequest> request(
                reinterpret_cast<OpenTabRequest*>(lParam));
            if (request && !self->closeRequested_) {
                self->OpenUrlInNewTab(request->url, request->activate);
            }
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hwnd, kMemoryTimer);
            HistoryStore::Get().Flush();
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
        if (!retired_.empty()) {
            return false;
        }
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
    // Client-area Y of the address bar window; recomputed in ResizeUI().
    int searchBarTop_ = kSearchBarTop;
    std::unique_ptr<SettingsPage> settingsPage_;
    std::unique_ptr<HistoryPage> historyPage_;
    std::unique_ptr<DownloadsPage> downloadsPage_;
    std::unique_ptr<DownloadPopup> downloadPopup_;

    std::vector<TabRecord> tabs_;
    std::vector<RetiredBrowser> retired_;

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

bool BrowserClient::OnBeforePopup(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    int popup_id,
    const CefString& target_url,
    const CefString& target_frame_name,
    WindowOpenDisposition target_disposition,
    bool user_gesture,
    const CefPopupFeatures& popupFeatures,
    CefWindowInfo& windowInfo,
    CefRefPtr<CefClient>& client,
    CefBrowserSettings& settings,
    CefRefPtr<CefDictionaryValue>& extra_info,
    bool* no_javascript_access) {

    const std::wstring popupUrl = target_url.ToWString();

    // Google / Apple sign-in needs a real popup (window.opener). Give it a
    // Vibrium-branded window instead of CEF's default one.
    if (target_disposition != CEF_WOD_NEW_BACKGROUND_TAB &&
        IsAuthPopupUrl(popupUrl)) {
        CefRefPtr<AuthPopup> popup = new AuthPopup(owner_->hwnd_);
        const int width = popupFeatures.widthSet ? popupFeatures.width : 0;
        const int height = popupFeatures.heightSet ? popupFeatures.height : 0;
        if (popup->CreateFrame(width, height)) {
            popup->PrepareWindowInfo(windowInfo);
            client = popup;
            return false;  // let CEF create it (keeps window.opener)
        }
    }

    // Everything else: never let CEF create its own window, use a tab.
    const bool background =
        target_disposition == CEF_WOD_NEW_BACKGROUND_TAB;
    owner_->QueueOpenInNewTab(target_url.ToWString(), !background);
    return true;  // cancel CEF's popup
}

void BrowserClient::OnBeforeContextMenu(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params,
    CefRefPtr<CefMenuModel> model) {

    if (!params || !model || params->GetLinkUrl().empty()) {
        return;
    }

    // Chrome-style entries that open links in a separate Chrome window
    // (open in new tab / new window / incognito window).
    for (int id : {50100, 50101, 50102}) {
        model->Remove(id);
    }

    model->InsertItemAt(0, kMenuOpenLinkInNewTab, "Open link in new tab");
    model->InsertSeparatorAt(1);
}

bool BrowserClient::OnContextMenuCommand(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefContextMenuParams> params,
    int command_id,
    EventFlags event_flags) {

    if (command_id == kMenuOpenLinkInNewTab && params) {
        owner_->QueueOpenInNewTab(params->GetLinkUrl().ToWString(), true);
        return true;
    }
    return false;
}

void BrowserClient::OnAfterCreated(
    CefRefPtr<CefBrowser> browser) {

    if (primaryBrowserId_ == 0) {
        primaryBrowserId_ = browser->GetIdentifier();
        owner_->OnBrowserCreated(tabId_, browser);
        return;
    }

    // A second browser reached this client (a popup that slipped through).
    // It must not replace the tab's browser; close it.
    if (browser->GetIdentifier() != primaryBrowserId_) {
        browser->GetHost()->CloseBrowser(true);
    }
}

void BrowserClient::OnFullscreenModeChange(
    CefRefPtr<CefBrowser> browser,
    bool fullscreen) {
    owner_->SetVideoFullscreen(tabId_, fullscreen);
}

void BrowserClient::OnBeforeClose(
    CefRefPtr<CefBrowser> browser) {

    AdblockService::Get().OnBrowserClosed(browser);

    // Whatever that browser was still downloading died with it.
    const int closedBrowserId = browser->GetIdentifier();
    DownloadManager::Get().OnBrowserClosed(closedBrowserId);
    owner_->OnRetiredBrowserClosed(closedBrowserId);

    // Only the tab's own browser closing may clear the tab record.
    if (browser->GetIdentifier() != primaryBrowserId_) {
        return;
    }
    primaryBrowserId_ = 0;
    owner_->OnBrowserClosed(tabId_);  // may release this client
}

void BrowserClient::OnLoadStart(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    TransitionType transition_type) {

    AdblockService::Get().ApplyCosmeticFilters(frame);
}

void BrowserClient::OnLoadEnd(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    int httpStatusCode) {

    if (frame && frame->IsMain()) {
        owner_->OnTabLoadEnd(tabId_);
    }
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

    // DownloadManager picks the destination: the user's Downloads folder, or
    // its own Save As window when "Ask where to save each file" is on.
    const bool accepted = DownloadManager::Get().HandleBeforeDownload(
        item,
        suggestedName.ToWString(),
        callback,
        browser->GetIdentifier());

    if (accepted) {
        owner_->OnDownloadStarted(tabId_);
    }

    return true;
}

void BrowserClient::OnDownloadUpdated(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> item,
    CefRefPtr<CefDownloadItemCallback> callback) {

    owner_->OnDownloadUpdated(tabId_, item);
    DownloadManager::Get().HandleUpdated(item, callback,
                                         browser->GetIdentifier());
    if (item && (item->IsComplete() || item->IsCanceled())) {
        owner_->PostReapRetired();
    }
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