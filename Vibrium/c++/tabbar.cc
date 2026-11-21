#include "tabbar.h"

#include <windowsx.h>
#include <dwmapi.h>

#include <algorithm>
#include <string>

namespace {
    constexpr wchar_t TABBAR_CLASS_NAME[] =
        L"VibriumTabBar";

    constexpr int TAB_GAP = 7;
    constexpr int TAB_TOP = 2;
    constexpr int TAB_BOTTOM = 2;
    constexpr int TAB_CORNER = 11;
    constexpr int TAB_STRIP_EXTRA_HEIGHT = 9;

    COLORREF BlendColor(COLORREF base, COLORREF accent, int accentPercent) {
        const int basePercent = 100 - accentPercent;
        return RGB(
            (GetRValue(base) * basePercent + GetRValue(accent) * accentPercent) / 100,
            (GetGValue(base) * basePercent + GetGValue(accent) * accentPercent) / 100,
            (GetBValue(base) * basePercent + GetBValue(accent) * accentPercent) / 100);
    }
}

COLORREF TabBar::systemAccentColor_ = GetSysColor(COLOR_HIGHLIGHT);

TabBar::~TabBar() {
    for (auto& tab : tabs_) {
        if (tab.iconBitmap) {
            DeleteObject(tab.iconBitmap);
            tab.iconBitmap = nullptr;
        }
    }
}

void TabBar::SetSystemAccentColor(COLORREF color) {
    systemAccentColor_ = color;
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
    const int captionHeight = nativeCaptionHeight + TAB_STRIP_EXTRA_HEIGHT;

    // The overlay covers the full caption row, including the native
    // caption-button area. We draw wider, vertically centered controls there
    // so they align with the tabs instead of sitting in the short DWM caption.
    const int overlayWidth = windowRect.right - windowRect.left;
    const int tabRowHeight = 45;
    const int overlayHeight = std::max(
        nativeCaptionHeight + TAB_STRIP_EXTRA_HEIGHT,
        tabRowHeight);

    SetWindowPos(
        hwnd_,
        HWND_TOP,
        windowRect.left,
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

int TabBar::GetTabWidth() const {
    if (!hwnd_ || tabs_.empty()) {
        return TAB_MIN_WIDTH;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);

    const int availableWidth =
        client.right - client.left - NEW_TAB_WIDTH - 18 - (46 * 3);
    const int calculatedWidth =
        availableWidth / static_cast<int>(tabs_.size());
    if (calculatedWidth < TAB_MIN_WIDTH) {
        return calculatedWidth > 88 ? calculatedWidth : 88;
    }
    return calculatedWidth < TAB_MAX_WIDTH ? calculatedWidth : TAB_MAX_WIDTH;
}

RECT TabBar::GetTabRect(int index) const {
    const int tabWidth = GetTabWidth();

    RECT rect{};

    rect.left = 5 + index * (tabWidth + TAB_GAP);
    rect.top = TAB_TOP;
    rect.right = rect.left + tabWidth;
    RECT client{};
    GetClientRect(hwnd_, &client);
    rect.bottom = client.bottom - TAB_BOTTOM;

    return rect;
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

        Paint(hdc);

        EndPaint(hwnd_, &ps);
        return 0;
    }

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
    RECT client{};
    GetClientRect(hwnd_, &client);

    // A dark accent tint, rather than a fully saturated caption background.
    const COLORREF barBackground = BlendColor(RGB(32, 33, 36), systemAccentColor_, 12);
    HBRUSH backgroundBrush = CreateSolidBrush(barBackground);
    FillRect(hdc, &client, backgroundBrush);
    DeleteObject(backgroundBrush);
    SetBkMode(hdc, TRANSPARENT);

    HFONT font = CreateFontW(
        14, 0, 0, 0, FW_MEDIUM,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));

    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        RECT rect = GetTabRect(i);
        const bool active = (i == activeTab_);
        if (!active) {
            rect.top = 8;
            rect.bottom = client.bottom - 4;
        }
        const COLORREF surface = active
            ? BlendColor(RGB(45, 46, 50), systemAccentColor_, 8)
            : BlendColor(barBackground, RGB(18, 18, 20), 12);

        HBRUSH tabBrush = CreateSolidBrush(surface);
        HPEN tabPen = CreatePen(PS_SOLID, 1,
            active ? RGB(100, 100, 108) : RGB(74, 74, 82));
        HGDIOBJ oldBrush = SelectObject(hdc, tabBrush);
        HGDIOBJ oldPen = SelectObject(hdc, tabPen);
        RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom,
            TAB_CORNER, TAB_CORNER);
        if (active) {
            // Chrome-style active tab: rounded shoulders, flat bottom that
            // merges into the toolbar instead of looking like a floating pill.
            RECT flatBottom{rect.left + 1, rect.bottom - 12,
                rect.right - 1, rect.bottom};
            FillRect(hdc, &flatBottom, tabBrush);
        }
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(tabBrush);
        DeleteObject(tabPen);

        const int iconSize = 16;
        const int iconX = rect.left + 12;
        const int iconY = (rect.top + rect.bottom - iconSize) / 2;
        if (tabs_[i].iconBitmap) {
            HDC iconDC = CreateCompatibleDC(hdc);
            HGDIOBJ oldIcon = SelectObject(iconDC, tabs_[i].iconBitmap);
            BITMAP iconInfo{};
            GetObjectW(tabs_[i].iconBitmap, sizeof(iconInfo), &iconInfo);
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            AlphaBlend(hdc, iconX, iconY, iconSize, iconSize,
                iconDC, 0, 0, iconInfo.bmWidth, iconInfo.bmHeight, blend);
            SelectObject(iconDC, oldIcon);
            DeleteDC(iconDC);
        } else {
            HBRUSH iconBrush = CreateSolidBrush(
                active ? systemAccentColor_ : RGB(67, 67, 75));
            HPEN iconPen = CreatePen(PS_SOLID, 1,
                active ? systemAccentColor_ : RGB(67, 67, 75));
            HGDIOBJ oldIconBrush = SelectObject(hdc, iconBrush);
            HGDIOBJ oldIconPen = SelectObject(hdc, iconPen);
            RoundRect(hdc, iconX, iconY, iconX + iconSize, iconY + iconSize, 7, 7);
            SelectObject(hdc, oldIconBrush);
            SelectObject(hdc, oldIconPen);
            DeleteObject(iconBrush);
            DeleteObject(iconPen);

            SetTextColor(hdc, RGB(255, 255, 255));
            RECT iconTextRect{iconX, iconY, iconX + iconSize, iconY + iconSize};
            DrawTextW(hdc, tabs_[i].iconText.c_str(), -1, &iconTextRect,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
        }

        SetTextColor(hdc, active ? COLOR_TEXT : RGB(205, 205, 212));
        RECT titleRect = rect;
        titleRect.left = iconX + iconSize + 9;
        titleRect.right -= 34;
        DrawTextW(hdc, tabs_[i].title.c_str(), -1, &titleRect,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        const int closeX = rect.right - 17;
        const int closeY = (rect.top + rect.bottom) / 2;
        HPEN closePen = CreatePen(PS_SOLID, 1,
            active ? RGB(235, 235, 240) : COLOR_MUTED);
        HPEN oldClosePen = static_cast<HPEN>(SelectObject(hdc, closePen));
        MoveToEx(hdc, closeX - 4, closeY - 4, nullptr);
        LineTo(hdc, closeX + 4, closeY + 4);
        MoveToEx(hdc, closeX + 4, closeY - 4, nullptr);
        LineTo(hdc, closeX - 4, closeY + 4);
        SelectObject(hdc, oldClosePen);
        DeleteObject(closePen);

        if (active) {
            RECT accent{rect.left + 14, rect.bottom - 3,
                rect.right - 14, rect.bottom};
            HBRUSH accentBrush = CreateSolidBrush(systemAccentColor_);
            FillRect(hdc, &accent, accentBrush);
            DeleteObject(accentBrush);
        }
    }

    const int tabWidth = GetTabWidth();
    const int tabCount = static_cast<int>(tabs_.size());
    const int lastTabRight = TAB_START + tabCount * tabWidth +
        std::max(0, tabCount - 1) * TAB_GAP;
    const int newTabX = lastTabRight + 8;
    RECT newTabRect{newTabX, 7, newTabX + 32, 33};
    HBRUSH newTabBrush = CreateSolidBrush(COLOR_TAB);
    HPEN newTabPen = CreatePen(PS_SOLID, 1, RGB(95, 95, 105));
    HGDIOBJ oldNewBrush = SelectObject(hdc, newTabBrush);
    HGDIOBJ oldNewPen = SelectObject(hdc, newTabPen);
    RoundRect(hdc, newTabRect.left, newTabRect.top,
        newTabRect.right, newTabRect.bottom, 12, 12);
    SelectObject(hdc, oldNewBrush);
    SelectObject(hdc, oldNewPen);
    DeleteObject(newTabBrush);
    DeleteObject(newTabPen);

    SetTextColor(hdc, COLOR_TEXT);
    DrawTextW(hdc, L"+", -1, &newTabRect,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    // Windows caption controls: three equal-width targets spanning the
    // same 40px row as the tabs, with larger line icons for clear visibility.
    constexpr int captionButtonWidth = 46;
    constexpr int captionButtonCount = 3;
    const int captionStart = client.right -
        captionButtonWidth * captionButtonCount;
    const int centerY = client.bottom / 2;

    for (int button = 0; button < captionButtonCount; ++button) {
        const int centerX = captionStart +
            button * captionButtonWidth + captionButtonWidth / 2;
        COLORREF glyphColor = RGB(235, 235, 240);
        if (button == 2) glyphColor = RGB(255, 255, 255);
        HPEN glyphPen = CreatePen(PS_SOLID, 2, glyphColor);
        HGDIOBJ oldGlyphPen = SelectObject(hdc, glyphPen);

        if (button == 0) {
            // Minimize: broad, centered horizontal stroke.
            MoveToEx(hdc, centerX - 8, centerY + 6, nullptr);
            LineTo(hdc, centerX + 8, centerY + 6);
        } else if (button == 1) {
            if (IsZoomed(parent_)) {
                // Restore: two offset, clearly visible window outlines.
                Rectangle(hdc, centerX - 5, centerY - 7,
                    centerX + 8, centerY + 5);
                MoveToEx(hdc, centerX - 8, centerY - 3, nullptr);
                LineTo(hdc, centerX - 8, centerY + 8);
                LineTo(hdc, centerX + 4, centerY + 8);
            } else {
                Rectangle(hdc, centerX - 7, centerY - 7,
                    centerX + 8, centerY + 8);
            }
        } else {
            // Close: larger X, balanced against the wider caption targets.
            MoveToEx(hdc, centerX - 7, centerY - 7, nullptr);
            LineTo(hdc, centerX + 7, centerY + 7);
            MoveToEx(hdc, centerX + 7, centerY - 7, nullptr);
            LineTo(hdc, centerX - 7, centerY + 7);
        }

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

    constexpr int captionButtonWidth = 46;
    constexpr int captionButtonCount = 3;
    const int captionStart = client.right -
        captionButtonWidth * captionButtonCount;
    if (x >= captionStart) {
        const int button = (x - captionStart) / captionButtonWidth;
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

    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        const RECT rect = GetTabRect(i);

        if (x < rect.left || x >= rect.right ||
            y < rect.top || y >= rect.bottom) {
            continue;
        }

        // Close button hit area.
        if (x >= rect.right - 32) {
            if (onTabClosed_) {
                onTabClosed_(i);
            }

            return;
        }

        activeTab_ = i;

        if (onTabSelected_) {
            onTabSelected_(i);
        }

        InvalidateRect(hwnd_, nullptr, TRUE);
        return;
    }

    const int tabWidth = GetTabWidth();
    const int tabCount = static_cast<int>(tabs_.size());
    const int lastTabRight = TAB_START + tabCount * tabWidth +
        std::max(0, tabCount - 1) * TAB_GAP;
    const int newTabX = lastTabRight + 8;

    if (x >= newTabX && x <= newTabX + 32) {
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