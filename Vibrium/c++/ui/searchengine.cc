#include "searchengine.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kSearchEngineClass[] =
    L"VibriumSearchEngine";

constexpr int kAddressControlId = 1001;

constexpr COLORREF kBackground = RGB(24, 20, 36);
constexpr COLORREF kPillColor = RGB(35, 29, 51);
constexpr COLORREF kPillBorder = RGB(77, 61, 112);
constexpr COLORREF kFocusBorder = RGB(151, 111, 255);
constexpr COLORREF kButtonColor = RGB(111, 76, 220);
constexpr COLORREF kButtonHover = RGB(132, 99, 242);
constexpr COLORREF kTextColor = RGB(241, 237, 250);
constexpr COLORREF kPlaceholderColor = RGB(157, 148, 177);

constexpr int kOuterPadding = 3;
constexpr int kEditLeft = 20;
constexpr int kEditRightPadding = 91;
constexpr int kButtonWidth = 70;

std::wstring Trim(const std::wstring& value) {
    const size_t first =
        value.find_first_not_of(L" \t\r\n");

    if (first == std::wstring::npos) {
        return L"";
    }

    const size_t last =
        value.find_last_not_of(L" \t\r\n");

    return value.substr(first, last - first + 1);
}

bool StartsWith(
    const std::wstring& value,
    const std::wstring& prefix) {

    return value.size() >= prefix.size() &&
        value.compare(0, prefix.size(), prefix) == 0;
}

bool ContainsWhitespace(const std::wstring& value) {
    return value.find_first_of(L" \t\r\n") !=
        std::wstring::npos;
}

} // namespace

bool SearchEngine::Create(
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
    wc.lpszClassName = kSearchEngineClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    if (!RegisterClassExW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        kSearchEngineClass,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        x,
        y,
        width,
        height,
        parent_,
        nullptr,
        instance_,
        this);

    if (!hwnd_) {
        return false;
    }

    width_ = width;
    height_ = height;

    editBrush_ = CreateSolidBrush(kPillColor);

    font_ = CreateFontW(
        16, 0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    addressEdit_ = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE |
        ES_AUTOHSCROLL |
        WS_TABSTOP,
        0,
        0,
        100,
        30,
        hwnd_,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(kAddressControlId)),
        instance_,
        nullptr);

    if (!addressEdit_) {
        return false;
    }

    SendMessageW(
        addressEdit_,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(font_),
        TRUE);

    SetWindowLongPtrW(
        addressEdit_,
        GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(this));

    originalEditProc_ =
        reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(
                addressEdit_,
                GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(EditProc)));

    LayoutControls();

    return true;
}

void SearchEngine::LayoutControls() {
    if (!addressEdit_) {
        return;
    }

    const int editWidth = std::max(
        50,
        width_ - kEditLeft - kEditRightPadding);

    const int editHeight = std::max(18, height_ - 16);
    const int editY = (height_ - editHeight) / 2;

    MoveWindow(
        addressEdit_,
        kEditLeft,
        editY,
        editWidth,
        editHeight,
        TRUE);
}

void SearchEngine::Resize(
    int x,
    int y,
    int width,
    int height) {

    width_ = width;
    height_ = height;

    if (!hwnd_) {
        return;
    }

    MoveWindow(hwnd_, x, y, width, height, TRUE);
    LayoutControls();

    InvalidateRect(hwnd_, nullptr, TRUE);
}

void SearchEngine::SetText(const std::wstring& text) {
    if (addressEdit_) {
        SetWindowTextW(addressEdit_, text.c_str());
    }
}

std::wstring SearchEngine::GetText() const {
    if (!addressEdit_) {
        return L"";
    }

    const int length = GetWindowTextLengthW(addressEdit_);

    if (length <= 0) {
        return L"";
    }

    std::wstring text(length + 1, L'\0');

    GetWindowTextW(
        addressEdit_,
        text.data(),
        length + 1);

    text.resize(length);
    return text;
}

void SearchEngine::SetSearchProvider(
    const std::wstring& provider) {

    if (!provider.empty()) {
        searchProvider_ = provider;
    }
}

bool SearchEngine::LooksLikeURL(
    const std::wstring& value) {

    if (value.empty() || ContainsWhitespace(value)) {
        return false;
    }

    if (StartsWith(value, L"https://") ||
        StartsWith(value, L"http://") ||
        StartsWith(value, L"file://")) {
        return true;
    }

    if (StartsWith(value, L"localhost") ||
        StartsWith(value, L"127.0.0.1")) {
        return true;
    }

    return value.find(L'.') != std::wstring::npos;
}

std::wstring SearchEngine::NormalizeURL(
    const std::wstring& value) {

    if (StartsWith(value, L"https://") ||
        StartsWith(value, L"http://") ||
        StartsWith(value, L"file://")) {
        return value;
    }

    return L"https://" + value;
}

std::wstring SearchEngine::EncodeURL(
    const std::wstring& value) {

    if (value.empty()) {
        return L"";
    }

    const int utf8Length = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);

    if (utf8Length <= 0) {
        return L"";
    }

    std::vector<char> utf8(utf8Length);

    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        -1,
        utf8.data(),
        utf8Length,
        nullptr,
        nullptr);

    static constexpr wchar_t hex[] =
        L"0123456789ABCDEF";

    std::wstring encoded;

    for (int i = 0; i < utf8Length - 1; ++i) {
        const unsigned char ch =
            static_cast<unsigned char>(utf8[i]);

        const bool unreserved =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' ||
            ch == '.' || ch == '~';

        if (unreserved) {
            encoded += static_cast<wchar_t>(ch);
        } else {
            encoded += L'%';
            encoded += hex[(ch >> 4) & 0x0F];
            encoded += hex[ch & 0x0F];
        }
    }

    return encoded;
}

void SearchEngine::Navigate() {
    if (!onNavigate_) {
        return;
    }

    const std::wstring input = Trim(GetText());

    if (input.empty()) {
        return;
    }

    const std::wstring destination =
        LooksLikeURL(input)
        ? NormalizeURL(input)
        : searchProvider_ + EncodeURL(input);

    onNavigate_(destination);
}

void SearchEngine::SetOnNavigate(
    NavigateCallback callback) {

    onNavigate_ = std::move(callback);
}

HWND SearchEngine::GetHandle() const {
    return hwnd_;
}

void SearchEngine::Paint(HDC hdc) {
    RECT client{};
    GetClientRect(hwnd_, &client);

    HBRUSH background = CreateSolidBrush(kBackground);
    FillRect(hdc, &client, background);
    DeleteObject(background);

    RECT pill{
        kOuterPadding,
        kOuterPadding,
        client.right - kOuterPadding,
        client.bottom - kOuterPadding
    };

    HBRUSH pillBrush = CreateSolidBrush(kPillColor);
    HPEN pillPen = CreatePen(
        PS_SOLID,
        editFocused_ ? 2 : 1,
        editFocused_ ? kFocusBorder : kPillBorder);

    HGDIOBJ oldBrush = SelectObject(hdc, pillBrush);
    HGDIOBJ oldPen = SelectObject(hdc, pillPen);

    RoundRect(
        hdc,
        pill.left,
        pill.top,
        pill.right,
        pill.bottom,
        client.bottom - 6,
        client.bottom - 6);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);

    DeleteObject(pillBrush);
    DeleteObject(pillPen);

    const int buttonLeft = client.right - kButtonWidth - 9;
    const int buttonTop = 8;
    const int buttonBottom = client.bottom - 8;

    RECT button{
        buttonLeft,
        buttonTop,
        client.right - 9,
        buttonBottom
    };

    HBRUSH buttonBrush = CreateSolidBrush(kButtonColor);
    HGDIOBJ previousBrush = SelectObject(hdc, buttonBrush);

    HPEN buttonPen = CreatePen(PS_NULL, 0, kButtonColor);
    HGDIOBJ previousPen = SelectObject(hdc, buttonPen);

    RoundRect(
        hdc,
        button.left,
        button.top,
        button.right,
        button.bottom,
        18,
        18);

    SelectObject(hdc, previousBrush);
    SelectObject(hdc, previousPen);

    DeleteObject(buttonBrush);
    DeleteObject(buttonPen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    HFONT buttonFont = CreateFontW(
        14, 0, 0, 0,
        FW_SEMIBOLD,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");

    HGDIOBJ oldFont = SelectObject(hdc, buttonFont);

    DrawTextW(
        hdc,
        L"Go",
        -1,
        &button,
        DT_SINGLELINE | DT_CENTER | DT_VCENTER);

    SelectObject(hdc, oldFont);
    DeleteObject(buttonFont);
}

LRESULT CALLBACK SearchEngine::EditProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {

    auto* self = reinterpret_cast<SearchEngine*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (self) {
        if (message == WM_KEYDOWN && wParam == VK_RETURN) {
            self->Navigate();
            return 0;
        }

        if (message == WM_SETFOCUS ||
            message == WM_KILLFOCUS) {

            self->editFocused_ = (message == WM_SETFOCUS);

            if (self->hwnd_) {
                InvalidateRect(self->hwnd_, nullptr, TRUE);
            }
        }

        if (self->originalEditProc_) {
            return CallWindowProcW(
                self->originalEditProc_,
                hwnd,
                message,
                wParam,
                lParam);
        }
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK SearchEngine::WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {

    auto* self = reinterpret_cast<SearchEngine*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        auto* create =
            reinterpret_cast<CREATESTRUCTW*>(lParam);

        self = static_cast<SearchEngine*>(
            create->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));

        self->hwnd_ = hwnd;
    }

    if (!self) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        self->Paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        HDC editDC = reinterpret_cast<HDC>(wParam);

        SetTextColor(editDC, kTextColor);
        SetBkColor(editDC, kPillColor);

        return reinterpret_cast<LRESULT>(self->editBrush_);
    }

    case WM_LBUTTONDOWN: {
        const int x = LOWORD(lParam);

        if (x >= self->width_ - kButtonWidth - 12) {
            self->Navigate();
            return 0;
        }

        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
