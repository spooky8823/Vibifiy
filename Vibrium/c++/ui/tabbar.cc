
#include "tabbar.h"

#include <algorithm>
#include <string>

namespace {
    constexpr wchar_t TABBAR_CLASS_NAME[] = L"VibriumTabBar";
}

bool TabBar::Create(
    HWND parent,
    HINSTANCE instance,
    int x,
    int y,
    int width,
    int height
) {
    parent_ = parent;
    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance_;
    wc.lpszClassName = TABBAR_CLASS_NAME;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(COLOR_BACKGROUND);

    RegisterClassExW(&wc);

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
        this
    );

    return hwnd_ != nullptr;
}

void TabBar::Resize(int x, int y, int width, int height) {
    if (!hwnd_) {
        return;
    }

    MoveWindow(hwnd_, x, y, width, height, TRUE);
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
    if (index < 0 || index >= static_cast<int>(tabs_.size())) {
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
            static_cast<int>(tabs_.size()) - 1
        );
    }

    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void TabBar::SetActiveTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) {
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
        client.right - client.left - NEW_TAB_WIDTH;

    const int calculatedWidth =
        availableWidth / static_cast<int>(tabs_.size());

    return std::clamp(
        calculatedWidth,
        TAB_MIN_WIDTH,
        TAB_MAX_WIDTH
    );
}

RECT TabBar::GetTabRect(int index) const {
    const int tabWidth = GetTabWidth();

    RECT rect{};
    rect.left = index * tabWidth;
    rect.top = 0;
    rect.right = rect.left + tabWidth;
    rect.bottom = TAB_HEIGHT;

    return rect;
}

LRESULT CALLBACK TabBar::WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    TabBar* self = reinterpret_cast<TabBar*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA)
    );

    if (message == WM_NCCREATE) {
        auto* createData =
            reinterpret_cast<CREATESTRUCTW*>(lParam);

        self = static_cast<TabBar*>(createData->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self)
        );

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
    LPARAM lParam
) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd_, &ps);

        Paint(hdc);

        EndPaint(hwnd_, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);

        HandleClick(x, y);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        hwnd_ = nullptr;
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void TabBar::Paint(HDC hdc) {
    RECT client{};
    GetClientRect(hwnd_, &client);

    HBRUSH backgroundBrush = CreateSolidBrush(COLOR_BACKGROUND);
    FillRect(hdc, &client, backgroundBrush);
    DeleteObject(backgroundBrush);

    SetBkMode(hdc, TRANSPARENT);

    HFONT font = CreateFontW(
        15, 0, 0, 0,
        FW_MEDIUM,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );

    HFONT oldFont = static_cast<HFONT>(
        SelectObject(hdc, font)
    );

    const int tabWidth = GetTabWidth();

    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        RECT rect = GetTabRect(i);

        const COLORREF tabColor =
            (i == activeTab_) ? COLOR_ACTIVE : COLOR_TAB;

        HBRUSH tabBrush = CreateSolidBrush(tabColor);
        FillRect(hdc, &rect, tabBrush);
        DeleteObject(tabBrush);

        if (i == activeTab_) {
            RECT accentRect{
                rect.left,
                rect.bottom - 3,
                rect.right,
                rect.bottom
            };

            HBRUSH accentBrush = CreateSolidBrush(COLOR_ACCENT);
            FillRect(hdc, &accentRect, accentBrush);
            DeleteObject(accentBrush);
        }

        SetTextColor(
            hdc,
            i == activeTab_ ? COLOR_TEXT : COLOR_MUTED
        );

        RECT titleRect = rect;
        titleRect.left += 14;
        titleRect.right -= 38;

        DrawTextW(
            hdc,
            tabs_[i].title.c_str(),
            -1,
            &titleRect,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS
        );

        // Close button
        const int closeX = rect.right - 20;
        const int closeY = rect.top + 21;

        HPEN closePen = CreatePen(
            PS_SOLID,
            1,
            COLOR_MUTED
        );

        HPEN oldPen = static_cast<HPEN>(
            SelectObject(hdc, closePen)
        );

        MoveToEx(hdc, closeX - 4, closeY - 4, nullptr);
        LineTo(hdc, closeX + 4, closeY + 4);

        MoveToEx(hdc, closeX + 4, closeY - 4, nullptr);
        LineTo(hdc, closeX - 4, closeY + 4);

        SelectObject(hdc, oldPen);
        DeleteObject(closePen);

        // Tab separator
        if (i != activeTab_) {
            HPEN separatorPen = CreatePen(
                PS_SOLID,
                1,
                RGB(48, 45, 58)
            );

            HPEN previousPen = static_cast<HPEN>(
                SelectObject(hdc, separatorPen)
            );

            MoveToEx(hdc, rect.right, 9, nullptr);
            LineTo(hdc, rect.right, 33);

            SelectObject(hdc, previousPen);
            DeleteObject(separatorPen);
        }
    }

    // New tab button
    const int newTabX =
        static_cast<int>(tabs_.size()) * tabWidth + 12;

    SetTextColor(hdc, COLOR_TEXT);

    RECT newTabRect{
        newTabX,
        0,
        newTabX + NEW_TAB_WIDTH,
        TAB_HEIGHT
    };

    DrawTextW(
        hdc,
        L"+",
        -1,
        &newTabRect,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER
    );

    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

void TabBar::HandleClick(int x, int y) {
    if (y < 0 || y > TAB_HEIGHT) {
        return;
    }

    const int tabWidth = GetTabWidth();

    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        const RECT rect = GetTabRect(i);

        if (x < rect.left || x >= rect.right) {
            continue;
        }

        // Close button area
        if (x >= rect.right - 32) {
            if (onTabClosed_) {
                onTabClosed_(i);
            }

            return;
        }

        // Select tab
        activeTab_ = i;

        if (onTabSelected_) {
            onTabSelected_(i);
        }

        InvalidateRect(hwnd_, nullptr, TRUE);
        return;
    }

    const int newTabX =
        static_cast<int>(tabs_.size()) * tabWidth;

    if (x >= newTabX && x <= newTabX + NEW_TAB_WIDTH) {
        if (onNewTab_) {
            onNewTab_();
        }
    }
}
