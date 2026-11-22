#include "searchengine.h"
#include "theme.h"

#include <algorithm>
using std::min;
using std::max;
#include <unknwn.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cwchar>
#include <iterator>
#include <map>
#include <memory>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kSearchEngineClass[] =
    L"VibriumSearchEngine";

constexpr int kAddressControlId = 1001;

constexpr COLORREF kBackground = RGB(18, 18, 20);
constexpr COLORREF kPillColor = RGB(35, 35, 39);
constexpr COLORREF kPillBorder = RGB(68, 68, 74);
constexpr COLORREF kTextColor = RGB(245, 245, 247);
constexpr COLORREF kPlaceholderColor = RGB(155, 155, 162);

constexpr int kOuterPadding = 1;
constexpr int kNavigationWidth = 154;
// Security icon (lock / warning / info) sits inside the pill's left end and
// the URL text starts after it.
constexpr int kIconCenterX = kNavigationWidth + 21;
constexpr int kEditLeftInset = 38; // URL starts 38px inside the address pill
constexpr int kEditLeft = kNavigationWidth + kEditLeftInset;
constexpr COLORREF kSecureColor = RGB(125, 205, 145);
constexpr COLORREF kNotSecureColor = RGB(245, 180, 60);
constexpr COLORREF kNeutralIconColor = RGB(170, 170, 178);
constexpr int kEditRightPadding = 1;

// Settings (gear) button, to the right of the address pill.
constexpr int kGearAreaWidth = 52;
constexpr int kGearSize = 32;
constexpr COLORREF kGearHoverFill = RGB(52, 52, 58);

constexpr int kNavIconSize = 22;     // back / home / forward PNGs
constexpr int kReloadIconSize = 16;  // refresh.png is 16x16: draw it 1:1
constexpr int kMoreIconSize = 22;    // three-dots PNG
constexpr int kMenuWidth = 170;
constexpr int kMenuItemHeight = 36;
constexpr UINT kMenuSettingsId = 2001;

// Hover circle behind each toolbar button (like Chrome's round highlight).
constexpr UINT_PTR kHoverTimer = 1;
constexpr UINT kHoverTimerMs = 15;
constexpr int kHoverDiameter = 32;
constexpr int kHoverHitRadius = 18;
constexpr int kHoverAlpha = 34;          // 0-255, white over the dark bar
constexpr int kHoverPressedAlpha = 62;
constexpr int kNavCenters[4] = {25, 61, 97, 133};

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

// <exe folder>\icons\<name>
std::wstring IconPath(const wchar_t* name) {
    wchar_t exe[MAX_PATH * 4]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    if (!n || n >= std::size(exe)) return L"";
    std::wstring path(exe);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L"";
    return path.substr(0, slash) + L"\\icons\\" + name;
}

// Loads icons\<name> and recolours it. The shipped PNGs are black shapes (the
// settings gear also has a white centre), so the shape is taken from the
// alpha channel and darkness, then filled with `tint`. Results are cached.
Gdiplus::Bitmap* TintedIcon(const wchar_t* name, COLORREF tint) {
    static std::map<std::wstring, std::unique_ptr<Gdiplus::Bitmap>> cache;

    wchar_t key[128]{};
    swprintf_s(key, L"%ls|%06X", name, static_cast<unsigned>(tint & 0xFFFFFF));
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.get();

    std::unique_ptr<Gdiplus::Bitmap> result;
    EnsureGdiplus();
    const std::wstring path = IconPath(name);
    if (!path.empty()) {
        Gdiplus::Bitmap source(path.c_str());
        if (source.GetLastStatus() == Gdiplus::Ok) {
            const int w = static_cast<int>(source.GetWidth());
            const int h = static_cast<int>(source.GetHeight());
            auto out = std::make_unique<Gdiplus::Bitmap>(w, h, PixelFormat32bppARGB);
            Gdiplus::Rect rect(0, 0, w, h);
            Gdiplus::BitmapData sd{}, dd{};
            if (source.LockBits(&rect, Gdiplus::ImageLockModeRead,
                                PixelFormat32bppARGB, &sd) == Gdiplus::Ok) {
                if (out->LockBits(&rect, Gdiplus::ImageLockModeWrite,
                                  PixelFormat32bppARGB, &dd) == Gdiplus::Ok) {
                    for (int y = 0; y < h; ++y) {
                        const BYTE* sp = static_cast<const BYTE*>(sd.Scan0) +
                                         static_cast<ptrdiff_t>(y) * sd.Stride;
                        BYTE* dp = static_cast<BYTE*>(dd.Scan0) +
                                   static_cast<ptrdiff_t>(y) * dd.Stride;
                        for (int x = 0; x < w; ++x, sp += 4, dp += 4) {
                            const int lum = (sp[2] * 299 + sp[1] * 587 + sp[0] * 114) / 1000;
                            dp[0] = GetBValue(tint);
                            dp[1] = GetGValue(tint);
                            dp[2] = GetRValue(tint);
                            dp[3] = static_cast<BYTE>(sp[3] * (255 - lum) / 255);
                        }
                    }
                    out->UnlockBits(&dd);
                    result = std::move(out);
                }
                source.UnlockBits(&sd);
            }
        }
    }

    Gdiplus::Bitmap* raw = result.get();
    cache[key] = std::move(result);  // a failed load is cached as nullptr
    return raw;
}

// Draws icons\<name> tinted and centred on (cx, cy). Returns false if the
// file could not be loaded so the caller can draw a fallback.
bool DrawPngIcon(HDC hdc, const wchar_t* name, COLORREF tint,
                 int cx, int cy, int size) {
    Gdiplus::Bitmap* icon = TintedIcon(name, tint);
    if (!icon) return false;
    Gdiplus::Graphics g(hdc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.DrawImage(icon, Gdiplus::Rect(cx - size / 2, cy - size / 2, size, size));
    return true;
}

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
        18, 0, 0, 0,
        FW_SEMIBOLD,
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

    // Remove the EDIT control's default inner margins so the URL begins
    // exactly kEditLeftInset pixels inside the address pill's left edge.
    SendMessageW(
        addressEdit_,
        EM_SETMARGINS,
        EC_LEFTMARGIN | EC_RIGHTMARGIN,
        MAKELPARAM(0, 0));

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

    // Navigation controls live in their own strip to the left of the address
    // pill. Let the pill use all remaining horizontal space up to Settings.
    const int addressRight = std::max(kEditLeft + 50,
        width_ - kGearAreaWidth - kOuterPadding);
    const int editWidth = std::max(
        50, addressRight - kEditLeft - kEditRightPadding);

    // A single-line EDIT draws its text at the top of its client area, so make
    // the control exactly one text line tall and centre it vertically inside
    // the pill. That puts the URL in the middle of the pill's height instead of
    // hugging its top-left corner.
    int lineHeight = 20;
    if (HDC dc = GetDC(addressEdit_)) {
        HGDIOBJ oldFont = SelectObject(dc, font_);
        TEXTMETRICW tm{};
        if (GetTextMetricsW(dc, &tm)) {
            lineHeight = tm.tmHeight;
        }
        SelectObject(dc, oldFont);
        ReleaseDC(addressEdit_, dc);
    }
    const int editHeight = std::min(lineHeight, std::max(1, height_ - 2));
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

void SearchEngine::FocusAddress() {
    if (!addressEdit_) return;
    SetFocus(addressEdit_);
    SendMessageW(addressEdit_, EM_SETSEL, 0, -1);
}

SearchEngine::Security SearchEngine::ClassifyUrl(const std::wstring& url) {
    std::wstring lower;
    lower.reserve(url.size());
    for (wchar_t c : url) {
        lower.push_back(static_cast<wchar_t>(std::towlower(c)));
        if (lower.size() >= 8) break;
    }
    if (lower.rfind(L"https://", 0) == 0) return Security::kSecure;
    if (lower.rfind(L"http://", 0) == 0) return Security::kNotSecure;
    return Security::kNeutral;  // file://, about:, internal pages, empty
}

void SearchEngine::SetText(const std::wstring& text) {
    if (addressEdit_) {
        SetWindowTextW(addressEdit_, text.c_str());
    }

    const Security state = ClassifyUrl(text);
    if (state != security_) {
        security_ = state;
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void SearchEngine::DrawSecurityIcon(HDC hdc, int cx, int cy) const {
    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    if (security_ == Security::kSecure) {
        // Closed padlock.
        HPEN pen = CreatePen(PS_SOLID, 2, kSecureColor);
        HBRUSH fill = CreateSolidBrush(kSecureColor);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, cx - 4, cy - 1, nullptr);
        LineTo(hdc, cx - 4, cy - 4);
        Arc(hdc, cx - 4, cy - 8, cx + 4, cy, cx + 4, cy - 4, cx - 4, cy - 4);
        MoveToEx(hdc, cx + 4, cy - 4, nullptr);
        LineTo(hdc, cx + 4, cy - 1);
        SelectObject(hdc, fill);
        RoundRect(hdc, cx - 6, cy - 1, cx + 7, cy + 8, 3, 3);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        DeleteObject(fill);
    } else if (security_ == Security::kNotSecure) {
        // Warning triangle with an exclamation mark.
        HPEN pen = CreatePen(PS_SOLID, 1, kNotSecureColor);
        HBRUSH fill = CreateSolidBrush(kNotSecureColor);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        SelectObject(hdc, fill);
        const POINT tri[3] = {{cx, cy - 8}, {cx + 8, cy + 7}, {cx - 8, cy + 7}};
        Polygon(hdc, tri, 3);
        HPEN mark = CreatePen(PS_SOLID, 2, kPillColor);
        SelectObject(hdc, mark);
        MoveToEx(hdc, cx, cy - 3, nullptr);
        LineTo(hdc, cx, cy + 2);
        MoveToEx(hdc, cx, cy + 4, nullptr);
        LineTo(hdc, cx, cy + 5);
        SelectObject(hdc, oldPen);
        DeleteObject(mark);
        DeleteObject(pen);
        DeleteObject(fill);
    } else {
        // Neutral info circle (local files and browser pages).
        HPEN pen = CreatePen(PS_SOLID, 2, kNeutralIconColor);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        Ellipse(hdc, cx - 7, cy - 7, cx + 8, cy + 8);
        MoveToEx(hdc, cx, cy - 4, nullptr);
        LineTo(hdc, cx, cy - 3);
        MoveToEx(hdc, cx, cy, nullptr);
        LineTo(hdc, cx, cy + 4);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    SelectObject(hdc, oldBrush);
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
        StartsWith(value, L"file://") ||
        StartsWith(value, L"vibrium://")) {
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
        StartsWith(value, L"file://") ||
        StartsWith(value, L"vibrium://")) {
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

void SearchEngine::SetOnSettings(
    SettingsCallback callback) {
    onSettings_ = std::move(callback);
}

void SearchEngine::SetOnNavigation(NavigationCallback callback) {
    onNavigation_ = std::move(callback);
}

RECT SearchEngine::GearRect(const RECT& client) const {
    const int areaLeft = client.right - kGearAreaWidth;
    const int left = areaLeft + (kGearAreaWidth - kGearSize) / 2 - 2;
    const int top = (client.bottom - kGearSize) / 2;

    return RECT{left, top, left + kGearSize, top + kGearSize};
}

bool SearchEngine::PointInGear(int x, int y) const {
    if (!hwnd_) {
        return false;
    }

    RECT client{};
    GetClientRect(hwnd_, &client);

    const RECT gear = GearRect(client);
    const POINT point{x, y};

    return PtInRect(&gear, point) != FALSE;
}

POINT SearchEngine::ButtonCenter(int index, const RECT& client) const {
    if (index >= 0 && index < 4) {
        return POINT{kNavCenters[index], client.bottom / 2};
    }
    const RECT gear = GearRect(client);
    return POINT{(gear.left + gear.right) / 2, (gear.top + gear.bottom) / 2};
}

int SearchEngine::HitButton(int x, int y) const {
    if (!hwnd_) return -1;
    RECT client{};
    GetClientRect(hwnd_, &client);
    for (int i = 0; i < kButtonCount; ++i) {
        const POINT c = ButtonCenter(i, client);
        const int dx = x - c.x;
        const int dy = y - c.y;
        if (dx * dx + dy * dy <= kHoverHitRadius * kHoverHitRadius) return i;
    }
    return -1;
}

void SearchEngine::SetHot(int index) {
    if (hot_ == index) return;
    hot_ = index;
    if (hwnd_) SetTimer(hwnd_, kHoverTimer, kHoverTimerMs, nullptr);
}

// Moves every button's highlight a step toward its target (fade in / out).
void SearchEngine::StepHover() {
    bool active = false;
    for (int i = 0; i < kButtonCount; ++i) {
        const float target = (i == hot_ || (i == 4 && menuOpen_)) ? 1.0f : 0.0f;
        const float diff = target - hover_[i];
        const float mag = std::fabs(diff);
        if (mag < 0.001f) {
            hover_[i] = target;
            continue;
        }
        const float step = std::min(mag, std::max(0.08f, mag * 0.30f));
        hover_[i] += diff > 0 ? step : -step;
        active = true;
    }
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (!active) KillTimer(hwnd_, kHoverTimer);
    }
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

    // Keep navigation completely outside the address pill. The pill stretches
    // across the full remaining width, stopping only before the Settings gear.
    const int addressRight = client.right - kGearAreaWidth - kOuterPadding;

    RECT pill{
        kNavigationWidth,
        kOuterPadding,
        addressRight,
        client.bottom - kOuterPadding
    };

    HBRUSH pillBrush = CreateSolidBrush(kPillColor);
    HPEN pillPen = CreatePen(
        PS_SOLID,
        editFocused_ ? 2 : 1,
        editFocused_ ? vibrium::Theme::Accent() : kPillBorder);

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

    // Secure / not secure indicator inside the pill, left of the URL.
    DrawSecurityIcon(hdc, kIconCenterX, client.bottom / 2);

    // Circular hover highlight behind each button (fades in and out).
    EnsureGdiplus();
    {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        for (int i = 0; i < kButtonCount; ++i) {
            if (hover_[i] <= 0.01f) continue;
            const int maxAlpha = (pressed_ == i) ? kHoverPressedAlpha : kHoverAlpha;
            const BYTE alpha = static_cast<BYTE>(maxAlpha * hover_[i]);
            const POINT c = ButtonCenter(i, client);
            Gdiplus::SolidBrush brush(Gdiplus::Color(alpha, 255, 255, 255));
            g.FillEllipse(&brush, c.x - kHoverDiameter / 2, c.y - kHoverDiameter / 2,
                          kHoverDiameter, kHoverDiameter);
        }
    }

    // Compact native navigation controls: back, home, reload and forward.
    const int cy = client.bottom / 2;
    const COLORREF navColor = vibrium::Theme::Accent();
    HPEN navPen = CreatePen(PS_SOLID, 2, navColor);
    HGDIOBJ oldNavPen = SelectObject(hdc, navPen);
    SetBkMode(hdc, TRANSPARENT);
    const int centers[] = {kNavCenters[0], kNavCenters[1], kNavCenters[2], kNavCenters[3]};
    // Back (backward.png).
    if (!DrawPngIcon(hdc, L"backward.png", navColor, centers[0], cy, kNavIconSize)) {
        MoveToEx(hdc, centers[0] + 5, cy - 6, nullptr); LineTo(hdc, centers[0] - 2, cy); LineTo(hdc, centers[0] + 5, cy + 6);
    }
    // Home (home.png).
    if (!DrawPngIcon(hdc, L"home.png", navColor, centers[1], cy, kNavIconSize)) {
        MoveToEx(hdc, centers[1] - 8, cy - 1, nullptr); LineTo(hdc, centers[1], cy - 9); LineTo(hdc, centers[1] + 8, cy - 1);
        LineTo(hdc, centers[1] + 6, cy - 1); LineTo(hdc, centers[1] + 6, cy + 8); LineTo(hdc, centers[1] - 6, cy + 8); LineTo(hdc, centers[1] - 6, cy - 1);
    }
    // Reload (refresh.png). It is a 16x16 image, so it is drawn 1:1 to stay crisp.
    if (!DrawPngIcon(hdc, L"refresh.png", navColor, centers[2], cy, kReloadIconSize)) {
        Arc(hdc, centers[2] - 7, cy - 7, centers[2] + 7, cy + 7, centers[2] + 7, cy, centers[2], cy - 7);
        MoveToEx(hdc, centers[2] + 1, cy - 8, nullptr); LineTo(hdc, centers[2] + 7, cy - 8); LineTo(hdc, centers[2] + 7, cy - 2);
    }
    // Forward (forward.png).
    if (!DrawPngIcon(hdc, L"forward.png", navColor, centers[3], cy, kNavIconSize)) {
        MoveToEx(hdc, centers[3] - 5, cy - 6, nullptr); LineTo(hdc, centers[3] + 2, cy); LineTo(hdc, centers[3] - 5, cy + 6);
    }
    SelectObject(hdc, oldNavPen);
    DeleteObject(navPen);

    const RECT gear = GearRect(client);

    // "More" button: three dots (3dots.png) that open a small menu.
    const COLORREF dotsColor = kTextColor;
    const int dotsX = (gear.left + gear.right) / 2;
    const int dotsY = (gear.top + gear.bottom) / 2;
    if (!DrawPngIcon(hdc, L"3dots.png", dotsColor, dotsX, dotsY, kMoreIconSize)) {
        HBRUSH dotBrush = CreateSolidBrush(dotsColor);
        HPEN dotPen = CreatePen(PS_SOLID, 1, dotsColor);
        HGDIOBJ ob = SelectObject(hdc, dotBrush);
        HGDIOBJ op = SelectObject(hdc, dotPen);
        for (int i = -1; i <= 1; ++i) {
            Ellipse(hdc, dotsX - 2, dotsY + i * 7 - 2, dotsX + 3, dotsY + i * 7 + 3);
        }
        SelectObject(hdc, ob);
        SelectObject(hdc, op);
        DeleteObject(dotBrush);
        DeleteObject(dotPen);
    }
}

void SearchEngine::ShowMoreMenu() {
    if (!hwnd_) return;

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    // Dark background behind the (owner-drawn) item.
    HBRUSH background = CreateSolidBrush(kPillColor);
    MENUINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND;
    info.hbrBack = background;
    SetMenuInfo(menu, &info);

    AppendMenuW(menu, MF_OWNERDRAW, kMenuSettingsId, nullptr);

    RECT client{};
    GetClientRect(hwnd_, &client);
    const RECT gear = GearRect(client);
    POINT anchor{gear.right, gear.bottom + 4};
    ClientToScreen(hwnd_, &anchor);

    menuOpen_ = true;
    SetTimer(hwnd_, kHoverTimer, kHoverTimerMs, nullptr);

    SetForegroundWindow(GetAncestor(hwnd_, GA_ROOT));
    const UINT chosen = TrackPopupMenu(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_RIGHTALIGN | TPM_TOPALIGN,
        anchor.x, anchor.y, 0, hwnd_, nullptr);

    DestroyMenu(menu);
    DeleteObject(background);

    menuOpen_ = false;
    pressed_ = -1;
    hot_ = -1;
    SetTimer(hwnd_, kHoverTimer, kHoverTimerMs, nullptr);

    if (chosen == kMenuSettingsId && onSettings_) {
        onSettings_();
    }
}

LRESULT CALLBACK SearchEngine::EditProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam) {

    auto* self = reinterpret_cast<SearchEngine*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (self) {
        if (message == WM_KEYDOWN && wParam == VK_RETURN) { self->Navigate(); return 0; }
        if (message == WM_KEYDOWN && wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
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
        // Draw off-screen first so the hover animation does not flicker.
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        if (mem && bmp) {
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            self->Paint(mem);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBmp);
        } else {
            self->Paint(hdc);
        }
        if (bmp) DeleteObject(bmp);
        if (mem) DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        HDC editDC = reinterpret_cast<HDC>(wParam);

        SetTextColor(editDC, kTextColor);
        SetBkColor(editDC, kPillColor);

        return reinterpret_cast<LRESULT>(self->editBrush_);
    }

    case WM_MEASUREITEM: {
        auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (item && item->CtlType == ODT_MENU) {
            item->itemWidth = kMenuWidth;
            item->itemHeight = kMenuItemHeight;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item && item->CtlType == ODT_MENU) {
            const bool selected = (item->itemState & ODS_SELECTED) != 0;
            HBRUSH fill = CreateSolidBrush(selected ? kGearHoverFill : kPillColor);
            FillRect(item->hDC, &item->rcItem, fill);
            DeleteObject(fill);

            const int midY = (item->rcItem.top + item->rcItem.bottom) / 2;
            DrawPngIcon(item->hDC, L"settings.png", kTextColor,
                        item->rcItem.left + 22, midY, 18);

            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, kTextColor);
            HGDIOBJ oldFont = SelectObject(item->hDC, self->font_);
            RECT textRect = item->rcItem;
            textRect.left += 44;
            DrawTextW(item->hDC, L"Settings", -1, &textRect,
                      DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
            SelectObject(item->hDC, oldFont);
            return TRUE;
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        const int x = static_cast<short>(LOWORD(lParam));
        const int y = static_cast<short>(HIWORD(lParam));

        const int button = self->HitButton(x, y);
        if (button >= 0) {
            self->pressed_ = button;
            InvalidateRect(hwnd, nullptr, FALSE);
            if (button == 4) {
                // Three dots: open the small menu (currently just Settings).
                self->ShowMoreMenu();
            } else if (self->onNavigation_) {
                self->onNavigation_(button);
            }
            return 0;
        }

        // The edit is now only one line tall, so clicks on the rest of the
        // pill should still focus the address box.
        if (x >= kNavigationWidth) {
            SetFocus(self->addressEdit_);
            return 0;
        }

        break;
    }

    case WM_MOUSEMOVE: {
        self->SetHot(self->HitButton(
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam))));

        if (!self->trackingMouse_) {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;

            if (TrackMouseEvent(&track)) {
                self->trackingMouse_ = true;
            }
        }

        break;
    }

    case WM_MOUSELEAVE:
        self->trackingMouse_ = false;

        self->pressed_ = -1;
        self->SetHot(-1);
        return 0;

    case WM_LBUTTONUP:
        if (self->pressed_ != -1) {
            self->pressed_ = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case WM_TIMER:
        if (wParam == kHoverTimer) {
            self->StepHover();
            return 0;
        }
        break;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && self->hot_ >= 0) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }

        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}