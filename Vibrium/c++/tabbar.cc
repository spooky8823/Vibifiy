#include "tabbar.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <algorithm>
using std::min;
using std::max;
// GDI+ headers need IUnknown/IStream, which WIN32_LEAN_AND_MEAN strips out.
#include <unknwn.h>
#include <objidl.h>
#include <gdiplus.h>

#include "tabbar_geometry.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {
    constexpr wchar_t TABBAR_CLASS_NAME[] =
        L"VibriumTabBar";

    constexpr int TAB_GAP = 3;          // gap along the slanted edges
    constexpr int TAB_START = 14;
    constexpr int TAB_STRIP_EXTRA_HEIGHT = 9;
    constexpr int NEW_TAB_SIZE = 30;
    constexpr int NEW_TAB_MARGIN = 10;
    constexpr int CAPTION_BUTTON_WIDTH = 46;
    constexpr int CAPTION_BUTTON_COUNT = 3;
    constexpr int BASELINE = 2;
    constexpr int CAPTION_GAP = 8;        // space before the window controls
    constexpr int CLOSE_MIN_WIDTH = 100;  // narrower tabs hide the close button
    constexpr UINT_PTR TAB_ANIMATION_TIMER = 1;
    constexpr ULONGLONG TAB_OPEN_DURATION = 190;
    constexpr ULONGLONG TAB_CLOSE_DURATION = 160;

    // Gdiplus is started once for the process and shut down at exit.
    void EnsureGdiplus() {
        struct Init {
            ULONG_PTR token = 0;
            Init() {
                Gdiplus::GdiplusStartupInput in;
                Gdiplus::GdiplusStartup(&token, &in, nullptr);
            }
            ~Init() { Gdiplus::GdiplusShutdown(token); }
        };
        static Init init;
    }

    Gdiplus::Color ToColor(COLORREF c, BYTE alpha = 255) {
        return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
    }

    // Brighter "glow" variant of the accent used for outlines (same rule as
    // the HTML mockup: keep hue, saturation >= 70%, lightness 52..68%).
    COLORREF GlowColor(COLORREF c) {
        const double r = GetRValue(c) / 255.0, g = GetGValue(c) / 255.0,
                     b = GetBValue(c) / 255.0;
        const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
        double l = (mx + mn) / 2, h = 0, s = 0;
        const double d = mx - mn;
        if (d > 0) {
            s = d / (1 - std::abs(2 * l - 1));
            if (mx == r) h = std::fmod((g - b) / d, 6.0);
            else if (mx == g) h = (b - r) / d + 2;
            else h = (r - g) / d + 4;
            h = std::fmod(h * 60 + 360, 360.0);
        }
        s = std::max(s, 0.70);
        l = std::min(std::max(l + 0.16, 0.52), 0.68);
        const double cc = (1 - std::abs(2 * l - 1)) * s;
        const double x = cc * (1 - std::abs(std::fmod(h / 60, 2.0) - 1));
        const double m = l - cc / 2;
        double rr = 0, gg = 0, bb = 0;
        if (h < 60) { rr = cc; gg = x; }
        else if (h < 120) { rr = x; gg = cc; }
        else if (h < 180) { gg = cc; bb = x; }
        else if (h < 240) { gg = x; bb = cc; }
        else if (h < 300) { rr = x; bb = cc; }
        else { rr = cc; bb = x; }
        return RGB(static_cast<int>((rr + m) * 255 + 0.5),
                   static_cast<int>((gg + m) * 255 + 0.5),
                   static_cast<int>((bb + m) * 255 + 0.5));
    }

    COLORREF BlendColor(COLORREF base, COLORREF accent, int accentPercent) {
        const int basePercent = 100 - accentPercent;
        return RGB(
            (GetRValue(base) * basePercent + GetRValue(accent) * accentPercent) / 100,
            (GetGValue(base) * basePercent + GetGValue(accent) * accentPercent) / 100,
            (GetBValue(base) * basePercent + GetBValue(accent) * accentPercent) / 100);
    }
}

COLORREF TabBar::accentColor_ = RGB(0, 120, 215);

TabBar::~TabBar() {
    for (auto& tab : tabs_) {
        if (tab.iconBitmap) {
            DeleteObject(tab.iconBitmap);
            tab.iconBitmap = nullptr;
        }
    }
}

void TabBar::SetAccentColor(COLORREF color) {
    accentColor_ = color;
}

bool TabBar::Create(
    HWND parent,
    HINSTANCE instance,
    int x,
    int y,
    int width,
    int height) {

    parent_ = parent;
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance_;
    wc.lpszClassName = TABBAR_CLASS_NAME;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    if (!RegisterClassExW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    // An owned, non-activating popup lets the tabs sit over the native
    // Windows caption without replacing the caption or its system buttons.
    EnsureGdiplus();
    hwnd_ = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        TABBAR_CLASS_NAME,
        L"",
        WS_POPUP,
        0,
        0,
        width,
        height,
        parent_,
        nullptr,
        instance_,
        this);

    if (!hwnd_) {
        return false;
    }

    Resize(x, y, width, height);
    return true;
}

void TabBar::Resize(
    int x,
    int y,
    int width,
    int height) {

    if (!hwnd_) {
        return;
    }

    if (!parent_ || !hwnd_) {
        return;
    }

    RECT windowRect{};
    GetWindowRect(parent_, &windowRect);

    POINT clientOrigin{0, 0};
    ClientToScreen(parent_, &clientOrigin);

    // A maximized window's invisible resize frame can extend above and
    // beyond the monitor. The visible caption itself begins at screen y=0.
    const int captionTop = IsZoomed(parent_)
        ? 0
        : windowRect.top;
    const int measuredCaptionHeight = clientOrigin.y - captionTop;
    const int nativeCaptionHeight =
        measuredCaptionHeight > 1 ? measuredCaptionHeight : 1;

    // The overlay covers the full caption row, including the native
    // caption-button area. We draw wider, vertically centered controls there
    // so they align with the tabs instead of sitting in the short DWM caption.
    // GetWindowRect includes the invisible resize border (and, when maximized,
    // hangs past the monitor). Use the visible frame so the bar never sticks
    // out of the window and the caption buttons are not clipped.
    RECT frame = windowRect;
    if (FAILED(DwmGetWindowAttribute(parent_, DWMWA_EXTENDED_FRAME_BOUNDS,
            &frame, sizeof(frame)))) {
        frame = windowRect;
    }
    if (IsZoomed(parent_)) {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(MonitorFromWindow(parent_, MONITOR_DEFAULTTONEAREST), &mi)) {
            frame.left = std::max(frame.left, mi.rcMonitor.left);
            frame.right = std::min(frame.right, mi.rcMonitor.right);
        }
    }
    const int overlayLeft = frame.left;
    const int overlayWidth = frame.right - frame.left;
    const int tabRowHeight = 46;
    const int overlayHeight = std::max(
        nativeCaptionHeight + TAB_STRIP_EXTRA_HEIGHT,
        tabRowHeight);

    SetWindowPos(
        hwnd_,
        HWND_TOP,
        overlayLeft,
        captionTop,
        overlayWidth,
        overlayHeight,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER);

    InvalidateRect(hwnd_, nullptr, TRUE);
    (void)x;
    (void)y;
    (void)height;
}

int TabBar::AddTab(const std::wstring& title) {
    tabs_.push_back({title});
    tabs_.back().opening = true;
    tabs_.back().animationStart = GetTickCount64();
    if (hwnd_) SetTimer(hwnd_, TAB_ANIMATION_TIMER, 16, nullptr);

    const int index = static_cast<int>(tabs_.size()) - 1;

    if (activeTab_ == -1) {
        activeTab_ = index;
    }

    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    return index;
}

void TabBar::RemoveTab(int index) {
    if (index < 0 ||
        index >= static_cast<int>(tabs_.size())) {
        return;
    }

    if (closingTab_ == index) closingTab_ = -1;
    else if (closingTab_ > index) --closingTab_;
    if (tabs_[index].iconBitmap) {
        DeleteObject(tabs_[index].iconBitmap);
    }
    tabs_.erase(tabs_.begin() + index);

    if (tabs_.empty()) {
        activeTab_ = -1;
    } else if (activeTab_ > index) {
        --activeTab_;
    } else if (activeTab_ == index) {
        activeTab_ = std::min(
            index,
            static_cast<int>(tabs_.size()) - 1);
    }

    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void TabBar::SetActiveTab(int index) {
    if (index < 0 ||
        index >= static_cast<int>(tabs_.size())) {
        return;
    }

    activeTab_ = index;

    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

int TabBar::GetActiveTab() const {
    return activeTab_;
}

int TabBar::GetTabCount() const {
    return static_cast<int>(tabs_.size());
}

void TabBar::SetOnTabSelected(TabCallback callback) {
    onTabSelected_ = std::move(callback);
}

void TabBar::SetOnTabClosed(TabCallback callback) {
    onTabClosed_ = std::move(callback);
}

void TabBar::SetOnNewTab(std::function<void()> callback) {
    onNewTab_ = std::move(callback);
}

HWND TabBar::GetHandle() const {
    return hwnd_;
}

void TabBar::SetTabTitle(
    int index,
    const std::wstring& title) {

    if (index < 0 ||
        index >= static_cast<int>(tabs_.size())) {
        return;
    }

    tabs_[index].title =
        title.empty() ? L"New Tab" : title;
    if (tabs_[index].iconText == L"•" && !title.empty()) {
        tabs_[index].iconText = title.substr(0, 1);
    }

    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void TabBar::SetTabIconText(
    int index,
    const std::wstring& text) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) {
        return;
    }
    tabs_[index].iconText = text.empty() ? L"•" : text.substr(0, 2);
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void TabBar::SetTabIconBitmap(int index, HBITMAP bitmap) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) {
        if (bitmap) DeleteObject(bitmap);
        return;
    }

    if (tabs_[index].iconBitmap) {
        DeleteObject(tabs_[index].iconBitmap);
    }
    tabs_[index].iconBitmap = bitmap;

    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

int TabBar::GetBarHeight() const {
    RECT client{};
    if (hwnd_) GetClientRect(hwnd_, &client);
    return client.bottom > 0 ? client.bottom : 40;
}

vibrium::tabgeo::Layout TabBar::GetLayout() const {
    RECT client{};
    if (hwnd_) GetClientRect(hwnd_, &client);
    return vibrium::tabgeo::ComputeLayout(
        client.right, GetBarHeight(), static_cast<int>(tabs_.size()),
        TAB_START, TAB_GAP, NEW_TAB_SIZE, NEW_TAB_MARGIN,
        CAPTION_BUTTON_WIDTH * CAPTION_BUTTON_COUNT, CAPTION_GAP,
        TAB_MAX_WIDTH);
}

int TabBar::GetTabWidth() const {
    return GetLayout().width;
}

float TabBar::GetTabAnimationScale(int index) const {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return 1.0f;
    const Tab& tab = tabs_[index];
    if (!tab.opening && !tab.closing) return 1.0f;
    const ULONGLONG elapsed = GetTickCount64() - tab.animationStart;
    const ULONGLONG duration = tab.closing ? TAB_CLOSE_DURATION : TAB_OPEN_DURATION;
    const float t = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(duration));
    const float eased = 1.0f - (1.0f - t) * (1.0f - t);
    return tab.closing ? 1.0f - eased : 0.12f + 0.88f * eased;
}

RECT TabBar::GetTabRect(int index) const {
    const auto layout = GetLayout();
    RECT rect{};
    rect.left = TAB_START + index * layout.pitch;
    // As a tab grows or shrinks, later tabs follow its animated width.
    for (int i = 0; i < index; ++i) {
        rect.left += static_cast<int>(
            layout.width * (GetTabAnimationScale(i) - 1.0f));
    }
    rect.top = 0;
    rect.right = rect.left + static_cast<int>(
        layout.width * GetTabAnimationScale(index));
    rect.bottom = GetBarHeight();
    return rect;
}

RECT TabBar::GetNewTabRect() const {
    const int h = GetBarHeight();
    RECT r{};
    r.left = GetLayout().plusLeft;
    r.right = r.left + NEW_TAB_SIZE;
    r.top = (h - NEW_TAB_SIZE - 6) / 2;   // floats slightly above centre
    r.bottom = r.top + NEW_TAB_SIZE;
    return r;
}

bool TabBar::TabContains(int index, int x, int y) const {
    const int h = GetBarHeight();
    const int slant = vibrium::tabgeo::Slant(h);
    const RECT r = GetTabRect(index);
    const auto shape = vibrium::tabgeo::MakeShape(
        r.left, r.right - r.left, h, slant, vibrium::tabgeo::IsFlipped(index));
    return vibrium::tabgeo::Contains(shape,
        static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, h, slant);
}

LRESULT CALLBACK TabBar::WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {

    TabBar* self = reinterpret_cast<TabBar*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        auto* createData =
            reinterpret_cast<CREATESTRUCTW*>(lParam);

        self = static_cast<TabBar*>(
            createData->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));

        self->hwnd_ = hwnd;
    }

    if (self) {
        return self->HandleMessage(message, wParam, lParam);
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT TabBar::HandleMessage(
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd_, &ps);

        // Double-buffer so the antialiased shapes never flicker.
        RECT client{};
        GetClientRect(hwnd_, &client);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, client.right, client.bottom);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        Paint(mem);
        BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);

        EndPaint(hwnd_, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wParam == TAB_ANIMATION_TIMER) {
            const ULONGLONG now = GetTickCount64();
            int finishedClose = -1;
            bool animationsRemain = false;
            for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
                Tab& tab = tabs_[i];
                if (tab.opening) {
                    if (now - tab.animationStart >= TAB_OPEN_DURATION) {
                        tab.opening = false;
                    } else {
                        animationsRemain = true;
                    }
                }
                if (tab.closing) {
                    if (now - tab.animationStart >= TAB_CLOSE_DURATION) {
                        finishedClose = i;
                    } else {
                        animationsRemain = true;
                    }
                }
            }
            InvalidateRect(hwnd_, nullptr, TRUE);
            if (finishedClose >= 0) {
                closingTab_ = -1;
                tabs_[finishedClose].closing = false;
                if (onTabClosed_) onTabClosed_(finishedClose);
                animationsRemain = false;
                for (const Tab& tab : tabs_) {
                    if (tab.opening || tab.closing) {
                        animationsRemain = true;
                        break;
                    }
                }
            }
            if (!animationsRemain) KillTimer(hwnd_, TAB_ANIMATION_TIMER);
            return 0;
        }
        break;

    case WM_LBUTTONDOWN:
        HandleClick(
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam));
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_SIZE:
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;

    case WM_NCDESTROY: {
        HWND oldWindow = hwnd_;

        SetWindowLongPtrW(oldWindow, GWLP_USERDATA, 0);
        hwnd_ = nullptr;

        return DefWindowProcW(
            oldWindow,
            message,
            wParam,
            lParam);
    }
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void TabBar::Paint(HDC hdc) {
    using namespace Gdiplus;
    namespace geo = vibrium::tabgeo;

    RECT client{};
    GetClientRect(hwnd_, &client);
    const int W = client.right;
    const int H = client.bottom;
    const int S = geo::Slant(H);
    const int tabW = GetTabWidth();
    const int count = static_cast<int>(tabs_.size());

    // Palette: everything derives from the accent colour.
    const COLORREF accent = accentColor_;
    const COLORREF black = RGB(0, 0, 0);
    const COLORREF barBg = BlendColor(black, accent, 34);
    const COLORREF inactiveFill = BlendColor(black, accent, 62);
    const COLORREF glow = GlowColor(accent);
    const COLORREF inactiveEdge = BlendColor(inactiveFill, glow, 50);
    const COLORREF activeFill = RGB(10, 10, 12);

    auto shapeOf = [&](int i) {
        const RECT r = GetTabRect(i);
        return geo::MakeShape(r.left, tabW, H, S, geo::IsFlipped(i));
    };

    // Outline along the top and the two slanted sides (no bottom edge),
    // inset so a thick pen stays inside the window and inside the tab.
    auto outline = [&](Graphics& g, const geo::TabShape& t, COLORREF c, float lw) {
        const float d = lw / 2.0f;
        const float k = d * 1.21f;   // 1 / cos(34 deg): horizontal inset
        auto xl = [&](float y) { return t.left + geo::EdgeOffset(t, y, H, S) + k; };
        auto xr = [&](float y) { return t.right - geo::EdgeOffset(t, y, H, S) - k; };
        PointF pts[4] = {
            {xl(static_cast<float>(H)), static_cast<float>(H)},
            {xl(d), d},
            {xr(d), d},
            {xr(static_cast<float>(H)), static_cast<float>(H)}};
        Pen pen(ToColor(c), lw);
        pen.SetLineJoin(LineJoinMiter);
        g.DrawLines(&pen, pts, 4);
    };

    auto fillShape = [&](Graphics& g, const geo::TabShape& t, COLORREF c) {
        PointF pts[4];
        for (int k = 0; k < 4; ++k) pts[k] = {t.poly[k].x, t.poly[k].y};
        SolidBrush brush(ToColor(c));
        g.FillPolygon(&brush, pts, 4);
    };

    RECT plus = GetNewTabRect();

    {
        Graphics g(hdc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);

        // 1. Darkened window bar (also behind the caption buttons).
        SolidBrush bar(ToColor(barBg));
        g.FillRectangle(&bar, 0, 0, W, H);

        // 2. Inactive tabs, thin outline.
        for (int i = 0; i < count; ++i) {
            if (i == activeTab_) continue;
            const auto t = shapeOf(i);
            fillShape(g, t, inactiveFill);
            outline(g, t, inactiveEdge, 1.0f);
        }

        // 3. Baseline across the whole bar, to both ends of the window.
        SolidBrush line(ToColor(glow));
        g.FillRectangle(&line, 0, H - BASELINE, W, BASELINE);

        // 4. Active tab on top: its fill covers the baseline beneath it so it
        //    merges with the page below; 2px accent outline around the rest.
        if (activeTab_ >= 0 && activeTab_ < count) {
            const auto t = shapeOf(activeTab_);
            fillShape(g, t, activeFill);
            outline(g, t, glow, 2.0f);
        }

        // 5. Floating rounded-square "+" button with a soft shadow.
        auto roundRect = [&](GraphicsPath& p, float x, float y, float w, float h, float r) {
            const float dd = r * 2;
            p.AddArc(x, y, dd, dd, 180, 90);
            p.AddArc(x + w - dd, y, dd, dd, 270, 90);
            p.AddArc(x + w - dd, y + h - dd, dd, dd, 0, 90);
            p.AddArc(x, y + h - dd, dd, dd, 90, 90);
            p.CloseFigure();
        };
        const float px = static_cast<float>(plus.left);
        const float py = static_cast<float>(plus.top);
        const float ps = static_cast<float>(NEW_TAB_SIZE);
        for (int i = 3; i >= 1; --i) {
            SolidBrush shadow(Color(static_cast<BYTE>(28 + i * 6), 0, 0, 0));
            GraphicsPath sp;
            roundRect(sp, px - i, py + 2 - i + 1, ps + 2 * i, ps + 2 * i, 9.0f + i);
            g.FillPath(&shadow, &sp);
        }
        GraphicsPath bp;
        roundRect(bp, px + 0.5f, py + 0.5f, ps - 1, ps - 1, 9.0f);
        SolidBrush plusFill(ToColor(activeFill));
        Pen plusPen(ToColor(glow), 1.0f);
        g.FillPath(&plusFill, &bp);
        g.DrawPath(&plusPen, &bp);
    }

    // Text, close glyphs and caption buttons use plain GDI on top.
    SetBkMode(hdc, TRANSPARENT);
    HFONT font = CreateFontW(
        16, 0, 0, 0, FW_SEMIBOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));

    const int pad = S + 12;   // keeps content inside the slanted sides
    for (int i = 0; i < count; ++i) {
        const RECT rect = GetTabRect(i);
        const bool roomy = (rect.right - rect.left) >= CLOSE_MIN_WIDTH;
        const bool showClose = roomy || i == activeTab_;
        // Narrow tabs (many tabs open) keep only a centred close button on
        // the active tab, like Chrome.
        const int closeX = roomy ? rect.right - pad - 9
                                 : (rect.left + rect.right) / 2;
        const int midY = H / 2;
        constexpr int iconSize = 18;
        const int iconX = rect.left + pad;
        const int iconY = (H - iconSize) / 2;

        // Draw the site's actual favicon when CEF has supplied one.
        // Otherwise use the tab's initial as a compact fallback.
        if (tabs_[i].iconBitmap) {
            Gdiplus::Bitmap favicon(tabs_[i].iconBitmap, nullptr);
            if (favicon.GetLastStatus() == Gdiplus::Ok) {
                Gdiplus::Graphics iconGraphics(hdc);
                iconGraphics.SetInterpolationMode(
                    Gdiplus::InterpolationModeHighQualityBicubic);
                iconGraphics.DrawImage(
                    &favicon, iconX, iconY, iconSize, iconSize);
            }
        } else {
            Gdiplus::Graphics iconGraphics(hdc);
            iconGraphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            Gdiplus::SolidBrush iconBrush(ToColor(glow));
            iconGraphics.FillEllipse(
                &iconBrush, iconX + 1, iconY + 1,
                iconSize - 2, iconSize - 2);

            HFONT fallbackFont = CreateFontW(
                10, 0, 0, 0, FW_BOLD,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                L"Segoe UI");
            HGDIOBJ oldFallbackFont = SelectObject(hdc, fallbackFont);
            SetTextColor(hdc, RGB(255, 255, 255));
            RECT fallbackRect{
                iconX, iconY, iconX + iconSize, iconY + iconSize};
            DrawTextW(hdc, tabs_[i].iconText.c_str(), -1, &fallbackRect,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
            SelectObject(hdc, oldFallbackFont);
            DeleteObject(fallbackFont);
        }

        SetTextColor(hdc, RGB(238, 238, 238));
        RECT titleRect{
            iconX + iconSize + 8, 0, closeX - 14, H};
        if (roomy && titleRect.right - titleRect.left >= 20) {
            DrawTextW(hdc, tabs_[i].title.c_str(), -1, &titleRect,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        if (showClose) {
            HPEN closePen = CreatePen(PS_SOLID, 1, RGB(225, 225, 225));
            HPEN oldClosePen = static_cast<HPEN>(SelectObject(hdc, closePen));
            MoveToEx(hdc, closeX - 4, midY - 4, nullptr);
            LineTo(hdc, closeX + 5, midY + 5);
            MoveToEx(hdc, closeX + 4, midY - 4, nullptr);
            LineTo(hdc, closeX - 5, midY + 5);
            SelectObject(hdc, oldClosePen);
            DeleteObject(closePen);
        }
    }

    // "+" glyph drawn as two strokes so it stays crisp and centred.
    {
        const int cx = (plus.left + plus.right) / 2;
        const int cy = (plus.top + plus.bottom) / 2;
        HPEN plusPen = CreatePen(PS_SOLID, 2, RGB(245, 245, 245));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, plusPen));
        MoveToEx(hdc, cx - 6, cy, nullptr);
        LineTo(hdc, cx + 7, cy);
        MoveToEx(hdc, cx, cy - 6, nullptr);
        LineTo(hdc, cx, cy + 7);
        SelectObject(hdc, oldPen);
        DeleteObject(plusPen);
    }

    // Windows caption controls, vertically centred in the bar.
    const int captionStart = W - CAPTION_BUTTON_WIDTH * CAPTION_BUTTON_COUNT;
    const int centerY = H / 2;
    for (int button = 0; button < CAPTION_BUTTON_COUNT; ++button) {
        const int centerX = captionStart +
            button * CAPTION_BUTTON_WIDTH + CAPTION_BUTTON_WIDTH / 2;
        HPEN glyphPen = CreatePen(PS_SOLID, 1, RGB(240, 240, 240));
        HGDIOBJ oldGlyphPen = SelectObject(hdc, glyphPen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

        if (button == 0) {
            MoveToEx(hdc, centerX - 5, centerY + 1, nullptr);
            LineTo(hdc, centerX + 6, centerY + 1);
        } else if (button == 1) {
            if (IsZoomed(parent_)) {
                Rectangle(hdc, centerX - 3, centerY - 5, centerX + 6, centerY + 4);
                MoveToEx(hdc, centerX - 6, centerY - 2, nullptr);
                LineTo(hdc, centerX - 6, centerY + 7);
                LineTo(hdc, centerX + 3, centerY + 7);
            } else {
                Rectangle(hdc, centerX - 5, centerY - 5, centerX + 5, centerY + 5);
            }
        } else {
            MoveToEx(hdc, centerX - 5, centerY - 5, nullptr);
            LineTo(hdc, centerX + 6, centerY + 6);
            MoveToEx(hdc, centerX + 5, centerY - 5, nullptr);
            LineTo(hdc, centerX - 6, centerY + 6);
        }

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldGlyphPen);
        DeleteObject(glyphPen);
    }

    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

void TabBar::HandleClick(int x, int y) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    if (y < 0 || y >= client.bottom) {
        return;
    }

    const int captionStart = client.right -
        CAPTION_BUTTON_WIDTH * CAPTION_BUTTON_COUNT;
    if (x >= captionStart) {
        const int button = (x - captionStart) / CAPTION_BUTTON_WIDTH;
        if (button == 0) {
            SendMessageW(parent_, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        } else if (button == 1) {
            SendMessageW(parent_, WM_SYSCOMMAND,
                IsZoomed(parent_) ? SC_RESTORE : SC_MAXIMIZE, 0);
        } else if (button == 2) {
            SendMessageW(parent_, WM_SYSCOMMAND, SC_CLOSE, 0);
        }
        return;
    }

    const int slant = vibrium::tabgeo::Slant(client.bottom);
    const int pad = slant + 12;
    const int tabCount = static_cast<int>(tabs_.size());

    // Topmost first: the active tab is painted last, then later tabs sit
    // above earlier ones (matters once tabs overlap).
    std::vector<int> order;
    if (activeTab_ >= 0 && activeTab_ < tabCount) order.push_back(activeTab_);
    for (int i = tabCount - 1; i >= 0; --i) {
        if (i != activeTab_) order.push_back(i);
    }

    for (int i : order) {
        // Tabs are slanted, so hit-test the real polygon, not the box.
        if (!TabContains(i, x, y)) {
            continue;
        }

        const RECT rect = GetTabRect(i);
        const bool roomy = (rect.right - rect.left) >= CLOSE_MIN_WIDTH;
        const bool showClose = roomy || i == activeTab_;
        const int closeX = roomy ? rect.right - pad - 9
                                 : (rect.left + rect.right) / 2;
        if (showClose && x >= closeX - 10 && x <= closeX + 10) {
            if (tabs_[i].closing || closingTab_ >= 0) return;
            tabs_[i].closing = true;
            tabs_[i].opening = false;
            tabs_[i].animationStart = GetTickCount64();
            closingTab_ = i;
            SetTimer(hwnd_, TAB_ANIMATION_TIMER, 16, nullptr);
            InvalidateRect(hwnd_, nullptr, TRUE);
            return;
        }

        activeTab_ = i;

        if (onTabSelected_) {
            onTabSelected_(i);
        }

        InvalidateRect(hwnd_, nullptr, TRUE);
        return;
    }

    const RECT plus = GetNewTabRect();
    if (x >= plus.left && x < plus.right && y >= plus.top && y < plus.bottom) {
        if (onNewTab_) {
            onNewTab_();
        }
        return;
    }

    // Empty tab-strip space acts as the draggable custom title bar.
    POINT screenPoint{x, y};
    ClientToScreen(hwnd_, &screenPoint);
    ReleaseCapture();
    SendMessageW(parent_, WM_NCLBUTTONDOWN, HTCAPTION,
        MAKELPARAM(screenPoint.x, screenPoint.y));
}
