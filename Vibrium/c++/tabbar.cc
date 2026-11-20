#include "tabbar.h"

#include <windowsx.h>
#include <dwmapi.h>

#include <algorithm>
#include <string>

namespace {
    constexpr wchar_t TABBAR_CLASS_NAME[] =
        L"VibriumTabBar";

    constexpr int TAB_GAP = 7;
    constexpr int TAB_TOP = 3;
    constexpr int TAB_BOTTOM = 3;
    constexpr int TAB_CORNER = 11;
    constexpr int TAB_STRIP_EXTRA_HEIGHT = 9;
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

    // Ask DWM for the actual system-caption button bounds. This is DPI-aware
    // and avoids drawing the tab overlay over the native minimize/maximize/
    // close buttons (which made the minimize glyph look clipped or missing).
    RECT captionButtons{};
    int overlayWidth = width > 0 ? width : 0;
    if (SUCCEEDED(DwmGetWindowAttribute(
            parent_,
            DWMWA_CAPTION_BUTTON_BOUNDS,
            &captionButtons,
            sizeof(captionButtons)))) {
        overlayWidth = captionButtons.left > 2 ? captionButtons.left - 2 : 0;
    }

    SetWindowPos(
        hwnd_,
        HWND_TOP,
        windowRect.left,
        captionTop,
        overlayWidth,
        captionHeight,
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
        client.right - client.left - NEW_TAB_WIDTH - 18;
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

    const COLORREF barBackground = systemAccentColor_;
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
        const COLORREF surface = active ? COLOR_ACTIVE : COLOR_TAB;

        HBRUSH tabBrush = CreateSolidBrush(surface);
        HPEN tabPen = CreatePen(PS_SOLID, 1,
            active ? RGB(100, 100, 108) : RGB(74, 74, 82));
        HGDIOBJ oldBrush = SelectObject(hdc, tabBrush);
        HGDIOBJ oldPen = SelectObject(hdc, tabPen);
        RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom,
            TAB_CORNER, TAB_CORNER);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(tabBrush);
        DeleteObject(tabPen);

        const int iconSize = 20;
        const int iconX = rect.left + 10;
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
            RECT accent{rect.left + 14, rect.bottom - 2,
                rect.right - 14, rect.bottom};
            HBRUSH accentBrush = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(hdc, &accent, accentBrush);
            DeleteObject(accentBrush);
        }
    }

    const int tabWidth = GetTabWidth();
    const int newTabX = 5 + static_cast<int>(tabs_.size()) *
        (tabWidth + TAB_GAP) + 2;
    RECT newTabRect{newTabX, 5, newTabX + 32, client.bottom - 5};
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

    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

void TabBar::HandleClick(int x, int y) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    if (y < 0 || y >= client.bottom) {
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

    const int newTabX =
        5 + static_cast<int>(tabs_.size()) *
        (tabWidth + TAB_GAP) + 2;

    if (x >= newTabX && x <= newTabX + 30) {
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