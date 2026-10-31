
#include "searchengine.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kSearchEngineClass[] =
    L"VibriumSearchEngine";

constexpr int kAddressPadding = 8;
constexpr int kGoButtonWidth = 65;

constexpr int kAddressControlId = 1001;
constexpr int kGoButtonId = 1002;

std::wstring Trim(const std::wstring& value) {
    const size_t first = value.find_first_not_of(
        L" \t\r\n");

    if (first == std::wstring::npos) {
        return L"";
    }

    const size_t last = value.find_last_not_of(
        L" \t\r\n");

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

// ------------------------------------------------------------
// Create
// ------------------------------------------------------------

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
    wc.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassExW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        kSearchEngineClass,
        L"",
        WS_CHILD | WS_VISIBLE,
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

    // Address input
    addressEdit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE |
        ES_AUTOHSCROLL |
        WS_TABSTOP,
        10,
        7,
        width - 100,
        height - 14,
        hwnd_,
        reinterpret_cast<HMENU>(kAddressControlId),
        instance_,
        nullptr);

    if (!addressEdit_) {
        return false;
    }

    // Go button
    goButton_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Go",
        WS_CHILD | WS_VISIBLE |
        WS_TABSTOP |
        BS_PUSHBUTTON,
        width - 80,
        7,
        kGoButtonWidth,
        height - 14,
        hwnd_,
        reinterpret_cast<HMENU>(kGoButtonId),
        instance_,
        nullptr);

    if (!goButton_) {
        return false;
    }

    HFONT font = static_cast<HFONT>(
        GetStockObject(DEFAULT_GUI_FONT));

    SendMessageW(
        addressEdit_,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(font),
        TRUE);

    SendMessageW(
        goButton_,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(font),
        TRUE);

    // Subclass the edit control so Enter triggers navigation.
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

    width_ = width;
    height_ = height;

    return true;
}

// ------------------------------------------------------------
// Resize
// ------------------------------------------------------------

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

    MoveWindow(
        hwnd_,
        x,
        y,
        width,
        height,
        TRUE);

    const int editWidth =
        std::max(100, width - 100);

    if (addressEdit_) {
        MoveWindow(
            addressEdit_,
            10,
            7,
            editWidth,
            height - 14,
            TRUE);
    }

    if (goButton_) {
        MoveWindow(
            goButton_,
            width - 80,
            7,
            kGoButtonWidth,
            height - 14,
            TRUE);
    }
}

// ------------------------------------------------------------
// Text
// ------------------------------------------------------------

void SearchEngine::SetText(
    const std::wstring& text) {

    if (!addressEdit_) {
        return;
    }

    SetWindowTextW(
        addressEdit_,
        text.c_str());
}

std::wstring SearchEngine::GetText() const {
    if (!addressEdit_) {
        return L"";
    }

    const int length = GetWindowTextLengthW(
        addressEdit_);

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

// ------------------------------------------------------------
// Search provider
// ------------------------------------------------------------

void SearchEngine::SetSearchProvider(
    const std::wstring& provider) {

    searchProvider_ = provider;
}

// ------------------------------------------------------------
// URL detection
// ------------------------------------------------------------

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

    // Treat a domain-like string as a URL.
    // Examples: youtube.com, github.com, example.org
    if (value.find(L'.') != std::wstring::npos) {
        return true;
    }

    return false;
}

// ------------------------------------------------------------
// URL normalization
// ------------------------------------------------------------

std::wstring SearchEngine::NormalizeURL(
    const std::wstring& value) {

    if (StartsWith(value, L"https://") ||
        StartsWith(value, L"http://") ||
        StartsWith(value, L"file://")) {
        return value;
    }

    return L"https://" + value;
}

// ------------------------------------------------------------
// URL encoding
// ------------------------------------------------------------

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
            ch == '-' ||
            ch == '_' ||
            ch == '.' ||
            ch == '~';

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

// ------------------------------------------------------------
// Navigation
// ------------------------------------------------------------

void SearchEngine::Navigate() {
    if (!onNavigate_) {
        return;
    }

    std::wstring input = Trim(GetText());

    if (input.empty()) {
        return;
    }

    std::wstring destination;

    if (LooksLikeURL(input)) {
        destination = NormalizeURL(input);
    } else {
        destination =
            searchProvider_ + EncodeURL(input);
    }

    // Send the final URL to MainWindow.
    // MainWindow navigates the selected tab.
    onNavigate_(destination);
}

// ------------------------------------------------------------
// Callback
// ------------------------------------------------------------

void SearchEngine::SetOnNavigate(
    NavigateCallback callback) {

    onNavigate_ = std::move(callback);
}

HWND SearchEngine::GetHandle() const {
    return hwnd_;
}

// ------------------------------------------------------------
// Edit subclass
// ------------------------------------------------------------

LRESULT CALLBACK SearchEngine::EditProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {

    auto* self = reinterpret_cast<SearchEngine*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (self && message == WM_KEYDOWN &&
        wParam == VK_RETURN) {

        self->Navigate();
        return 0;
    }

    if (self && self->originalEditProc_) {
        return CallWindowProcW(
            self->originalEditProc_,
            hwnd,
            message,
            wParam,
            lParam);
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam);
}

// ------------------------------------------------------------
// Parent window procedure
// ------------------------------------------------------------

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
    }

    if (!self) {
        return DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam);
    }

    switch (message) {
    case WM_COMMAND: {
        const int controlId = LOWORD(wParam);
        const int notification = HIWORD(wParam);

        if (controlId == kGoButtonId &&
            notification == BN_CLICKED) {

            self->Navigate();
            return 0;
        }

        return 0;
    }

    case WM_NCDESTROY:
        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            0);

        return DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam);

    default:
        return DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam);
    }
}
