#include "tabbar.h"

#include <windowsx.h>

#include <algorithm>
#include <string>

namespace {
    constexpr wchar_t TABBAR_CLASS_NAME[] =
        L"VibriumTabBar";

    constexpr int TAB_GAP = 7;
    constexpr int TAB_TOP = 5;
    constexpr int TAB_BOTTOM = 5;
    constexpr int TAB_CORNER = 12;
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

    hwnd_ = CreateWindowExW(
        0,
        TABBAR_CLASS_NAME,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        x,
        y,
        width,
        height,
        parent_,
        nullptr,
        instance_,
        this);

    return hwnd_ != nullptr;
}

void TabBar::Resize(
    int x,
    int y,
    int width,
    int height) {

    if (!hwnd_) {
        return;
    }

    MoveWindow(hwnd_, x, y, width, height, TRUE);
    InvalidateRect(hwnd_, nullptr, TRUE);
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

int TabBar::GetTabWidth() const {
    if (!hwnd_ || tabs_.empty()) {
        return TAB_MIN_WIDTH;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);

    const int availableWidth =
        client.right - client.left - NEW_TAB_WIDTH - 12;

    const int calculatedWidth =
        availableWidth / static_cast<int>(tabs_.size());

    return std::clamp(
        calculatedWidth,
        TAB_MIN_WIDTH,
        TAB_MAX_WIDTH);
}

RECT TabBar::GetTabRect(int index) const {
    const int tabWidth = GetTabWidth();

    RECT rect{};

    rect.left = 5 + index * (tabWidth + TAB_GAP);
    rect.top = TAB_TOP;
    rect.right = rect.left + tabWidth;
    rect.bottom = TAB_HEIGHT - TAB_BOTTOM;

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

    HBRUSH backgroundBrush =
        CreateSolidBrush(COLOR_WINDOW_BG);

    FillRect(hdc, &client, backgroundBrush);
    DeleteObject(backgroundBrush);

    SetBkMode(hdc, TRANSPARENT);

    HFONT font = CreateFontW(
        14, 0, 0, 0,
        FW_MEDIUM,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    HFONT oldFont = static_cast<HFONT>(
        SelectObject(hdc, font));

    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        RECT rect = GetTabRect(i);

        const bool active = (i == activeTab_);

        const COLORREF tabColor =
            active ? COLOR_ACTIVE : COLOR_TAB;

        HBRUSH tabBrush = CreateSolidBrush(tabColor);

        HPEN tabPen = CreatePen(
            PS_SOLID,
            1,
            active ? COLOR_ACCENT : COLOR_TAB);

        HGDIOBJ oldBrush = SelectObject(hdc, tabBrush);
        HGDIOBJ oldPen = SelectObject(hdc, tabPen);

        RoundRect(
            hdc,
            rect.left,
            rect.top,
            rect.right,
            rect.bottom,
            TAB_CORNER,
            TAB_CORNER);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);

        DeleteObject(tabBrush);
        DeleteObject(tabPen);

        // Small violet active indicator.
        if (active) {
            RECT accent{
                rect.left + 13,
                rect.bottom - 3,
                rect.right - 13,
                rect.bottom - 1
            };

            HBRUSH accentBrush =
                CreateSolidBrush(COLOR_ACCENT);

            FillRect(hdc, &accent, accentBrush);
            DeleteObject(accentBrush);
        }

        SetTextColor(
            hdc,
            active ? COLOR_TEXT : COLOR_MUTED);

        RECT titleRect = rect;
        titleRect.left += 13;
        titleRect.right -= 35;

        DrawTextW(
            hdc,
            tabs_[i].title.c_str(),
            -1,
            &titleRect,
            DT_SINGLELINE |
            DT_VCENTER |
            DT_END_ELLIPSIS);

        // Close button.
        const int closeX = rect.right - 18;
        const int closeY = (rect.top + rect.bottom) / 2;

        HPEN closePen = CreatePen(
            PS_SOLID,
            1,
            active ? COLOR_TEXT : COLOR_MUTED);

        HPEN oldClosePen = static_cast<HPEN>(
            SelectObject(hdc, closePen));

        MoveToEx(hdc, closeX - 4, closeY - 4, nullptr);
        LineTo(hdc, closeX + 4, closeY + 4);

        MoveToEx(hdc, closeX + 4, closeY - 4, nullptr);
        LineTo(hdc, closeX - 4, closeY + 4);

        SelectObject(hdc, oldClosePen);
        DeleteObject(closePen);
    }

    // Rounded new-tab button.
    const int tabWidth = GetTabWidth();

    const int newTabX =
        5 + static_cast<int>(tabs_.size()) *
        (tabWidth + TAB_GAP) + 2;

    RECT newTabRect{
        newTabX,
        6,
        newTabX + 30,
        TAB_HEIGHT - 6
    };

    HBRUSH newTabBrush =
        CreateSolidBrush(RGB(48, 37, 72));

    HPEN newTabPen =
        CreatePen(PS_SOLID, 1, COLOR_ACCENT);

    HGDIOBJ oldNewBrush = SelectObject(hdc, newTabBrush);
    HGDIOBJ oldNewPen = SelectObject(hdc, newTabPen);

    RoundRect(
        hdc,
        newTabRect.left,
        newTabRect.top,
        newTabRect.right,
        newTabRect.bottom,
        13,
        13);

    SelectObject(hdc, oldNewBrush);
    SelectObject(hdc, oldNewPen);

    DeleteObject(newTabBrush);
    DeleteObject(newTabPen);

    SetTextColor(hdc, COLOR_TEXT);

    DrawTextW(
        hdc,
        L"+",
        -1,
        &newTabRect,
        DT_SINGLELINE |
        DT_CENTER |
        DT_VCENTER);

    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

void TabBar::HandleClick(int x, int y) {
    if (y < 0 || y > TAB_HEIGHT) {
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
    }
}