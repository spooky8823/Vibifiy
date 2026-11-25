#include "profile_picker.h"
#include "icon_util.h"
#include "theme.h"

#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <map>
#include <memory>
#include <system_error>
#include <vector>
using std::min;
using std::max;
// GDI+ headers need IUnknown/IStream, which WIN32_LEAN_AND_MEAN strips out.
#include <unknwn.h>
#include <objidl.h>
#include <gdiplus.h>

namespace vibrium {
namespace {

namespace fs = std::filesystem;

constexpr wchar_t kPickerClass[] = L"VibriumProfilePicker";

// Design-time client size. Every number below is in "96-dpi pixels" and is
// scaled by the window's real DPI (see PickerWindow::S), which is what keeps
// the picker crisp instead of being bitmap-stretched by Windows.
constexpr int kClientW = 960;
constexpr int kClientH = 640;
constexpr int kColumns = 4;
constexpr int kNameEditId = 1004;
constexpr int kHexEditId = 1005;

// DWM attribute ids (values from dwmapi.h; spelled out so older SDKs build).
constexpr DWORD kDwmImmersiveDark = 20;
constexpr DWORD kDwmCaptionColor = 35;
constexpr DWORD kDwmTextColor = 36;

// ---- what "Clear profile data" can erase ------------------------------
//
// Each category lists file/folder names. They are looked up in the profile
// folder and in its Default\, Network\ and Default\Network\ sub-folders,
// which is where the embedded Chromium keeps them. Adjust the names here if
// Vibrium starts storing these somewhere else.
constexpr int kCategoryCount = 6;
struct DataCategory {
    const wchar_t* label;
    const wchar_t* items[12];
};
const DataCategory kCategories[kCategoryCount] = {
    {L"Cookies and site data",
     {L"Cookies", L"Cookies-journal", L"Local Storage", L"Session Storage",
      L"IndexedDB", L"Service Worker", L"File System",
      L"Network Persistent State", L"TransportSecurity"}},
    {L"Browsing history",
     {L"history.tsv", L"History", L"History-journal", L"Visited Links",
      L"Top Sites", L"Top Sites-journal"}},
    {L"Download history", {L"downloads.tsv", L"DownloadMetadata"}},
    {L"Bookmarks",
     {L"Bookmarks", L"Bookmarks.bak", L"bookmarks.json", L"bookmarks.tsv"}},
    {L"Shortcuts",
     {L"Shortcuts", L"Shortcuts-journal", L"shortcuts.json",
      L"shortcuts.tsv"}},
    {L"Cached images and files",
     {L"Cache", L"Code Cache", L"GPUCache", L"DawnCache", L"GrShaderCache",
      L"ShaderCache"}},
};

// ---- colours ----------------------------------------------------------

struct Palette {
    COLORREF bgTop, bgBottom, sidebar, card, cardHover, field, text, muted,
        line, accent, onAccent, danger, dangerText;
    bool dark;
};

COLORREF Mix(COLORREF a, COLORREF b, double t) {
    const auto ch = [t](int x, int y) {
        return static_cast<int>(std::lround(x + (y - x) * t));
    };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)),
               ch(GetBValue(a), GetBValue(b)));
}

double Luma(COLORREF c) {
    return (0.299 * GetRValue(c) + 0.587 * GetGValue(c) +
            0.114 * GetBValue(c)) / 255.0;
}

enum class Mode { System = 0, Light = 1, Dark = 2, Custom = 3 };

Palette DerivePalette(Mode mode, COLORREF customBg, COLORREF customAccent,
                      bool systemDark) {
    Palette p{};
    const COLORREF white = RGB(255, 255, 255);
    const COLORREF black = RGB(0, 0, 0);
    bool dark = systemDark;
    if (mode == Mode::Light) dark = false;
    if (mode == Mode::Dark) dark = true;

    if (mode == Mode::Custom) {
        const COLORREF bg = customBg;
        dark = Luma(bg) < 0.5;
        p.accent = customAccent;
        p.bgBottom = bg;
        p.bgTop = Mix(bg, customAccent, dark ? 0.14 : 0.10);
        p.text = dark ? RGB(238, 240, 243) : RGB(32, 33, 36);
        p.muted = Mix(p.text, bg, 0.42);
        if (dark) {
            p.card = Mix(bg, white, 0.08);
            p.cardHover = Mix(bg, white, 0.15);
            p.line = Mix(bg, white, 0.20);
            p.sidebar = Mix(bg, white, 0.04);
        } else {
            p.card = Mix(bg, white, 0.70);
            p.cardHover = Mix(p.card, black, 0.06);
            p.line = Mix(bg, black, 0.13);
            p.sidebar = Mix(bg, black, 0.03);
        }
        p.field = Mix(p.card, black, dark ? 0.30 : 0.05);
    } else if (dark) {
        p.bgTop = RGB(36, 45, 43);   p.bgBottom = RGB(32, 33, 36);
        p.sidebar = RGB(37, 38, 41);
        p.card = RGB(48, 49, 52);    p.cardHover = RGB(60, 64, 67);
        p.field = RGB(32, 33, 36);
        p.text = RGB(232, 234, 237); p.muted = RGB(154, 160, 166);
        p.line = RGB(66, 69, 73);
        p.accent = RGB(138, 180, 248);
    } else {
        p.bgTop = RGB(228, 238, 252); p.bgBottom = RGB(250, 251, 253);
        p.sidebar = RGB(240, 244, 250);
        p.card = RGB(255, 255, 255);  p.cardHover = RGB(241, 243, 244);
        p.field = RGB(241, 243, 244);
        p.text = RGB(32, 33, 36);     p.muted = RGB(95, 99, 104);
        p.line = RGB(218, 220, 224);
        p.accent = RGB(26, 115, 232);
    }
    p.onAccent = Luma(p.accent) > 0.62 ? RGB(32, 33, 36) : RGB(255, 255, 255);
    p.danger = RGB(217, 48, 37);
    p.dangerText = dark ? RGB(242, 139, 130) : RGB(197, 34, 31);
    p.dark = dark;
    return p;
}

constexpr COLORREF kAvatarColors[] = {
    RGB(66, 133, 244), RGB(52, 168, 83),  RGB(234, 67, 53),
    RGB(171, 71, 188), RGB(0, 150, 167),  RGB(244, 81, 30),
    RGB(92, 107, 192), RGB(194, 24, 91),
};

COLORREF AvatarColor(const std::wstring& name) {
    unsigned hash = 17;
    for (wchar_t c : name) {
        hash = hash * 31u + static_cast<unsigned>(std::towlower(c));
    }
    return kAvatarColors[hash % (sizeof(kAvatarColors) / sizeof(kAvatarColors[0]))];
}

const COLORREF kBackgroundPresets[12] = {
    RGB(32, 33, 36),    RGB(0, 0, 0),       RGB(20, 24, 38),
    RGB(16, 32, 28),    RGB(38, 22, 38),    RGB(44, 28, 24),
    RGB(255, 255, 255), RGB(243, 244, 246), RGB(232, 240, 254),
    RGB(254, 243, 232), RGB(236, 248, 238), RGB(250, 236, 246),
};
const COLORREF kAccentPresets[12] = {
    RGB(138, 180, 248), RGB(26, 115, 232),  RGB(52, 168, 83),
    RGB(234, 67, 53),   RGB(251, 188, 4),   RGB(171, 71, 188),
    RGB(0, 172, 193),   RGB(244, 81, 30),   RGB(233, 30, 99),
    RGB(92, 107, 192),  RGB(120, 144, 156), RGB(255, 255, 255),
};

std::wstring HexString(COLORREF c) {
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"#%02X%02X%02X", GetRValue(c), GetGValue(c),
               GetBValue(c));
    return buffer;
}

bool ParseHex(std::wstring text, COLORREF* out) {
    while (!text.empty() && std::iswspace(text.front())) text.erase(text.begin());
    while (!text.empty() && std::iswspace(text.back())) text.pop_back();
    if (!text.empty() && text.front() == L'#') text.erase(text.begin());
    if (text.size() != 6 && text.size() != 3) return false;
    for (wchar_t c : text) {
        if (!std::iswxdigit(c)) return false;
    }
    if (text.size() == 3) {
        std::wstring expanded;
        for (wchar_t c : text) { expanded += c; expanded += c; }
        text = expanded;
    }
    const unsigned long v = std::wcstoul(text.c_str(), nullptr, 16);
    *out = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return true;
}

// ---- GDI+ helpers -----------------------------------------------------

Gdiplus::Color Col(COLORREF c, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

Gdiplus::RectF FR(const RECT& r) {
    return Gdiplus::RectF(static_cast<float>(r.left), static_cast<float>(r.top),
                          static_cast<float>(r.right - r.left),
                          static_cast<float>(r.bottom - r.top));
}

void RoundPath(Gdiplus::GraphicsPath& path, Gdiplus::RectF r, float radius) {
    radius = std::min(radius, std::min(r.Width, r.Height) / 2.0f);
    const float d = radius * 2.0f;
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void FillRound(Gdiplus::Graphics& g, Gdiplus::RectF r, float radius,
               Gdiplus::Color color) {
    Gdiplus::GraphicsPath path;
    RoundPath(path, r, radius);
    Gdiplus::SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

void StrokeRound(Gdiplus::Graphics& g, Gdiplus::RectF r, float radius,
                 Gdiplus::Color color, float width, bool dashed = false) {
    const float inset = width / 2.0f;
    r.X += inset; r.Y += inset; r.Width -= width; r.Height -= width;
    Gdiplus::GraphicsPath path;
    RoundPath(path, r, std::max(0.0f, radius - inset));
    Gdiplus::Pen pen(color, width);
    if (dashed) pen.SetDashStyle(Gdiplus::DashStyleDash);
    g.DrawPath(&pen, &path);
}

void FillCircle(Gdiplus::Graphics& g, Gdiplus::RectF r, Gdiplus::Color color) {
    Gdiplus::SolidBrush brush(color);
    g.FillEllipse(&brush, r);
}

void DrawLabel(Gdiplus::Graphics& g, const std::wstring& text,
               Gdiplus::RectF rect, float pixelSize, bool bold,
               COLORREF color,
               Gdiplus::StringAlignment h = Gdiplus::StringAlignmentCenter,
               Gdiplus::StringAlignment v = Gdiplus::StringAlignmentCenter) {
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, pixelSize,
                       bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);
    Gdiplus::StringFormat format(Gdiplus::StringFormatFlagsNoWrap);
    format.SetAlignment(h);
    format.SetLineAlignment(v);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::SolidBrush brush(Col(color));
    g.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
}

void DrawParagraph(Gdiplus::Graphics& g, const std::wstring& text,
                   Gdiplus::RectF rect, float pixelSize, COLORREF color) {
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, pixelSize, Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(Gdiplus::StringAlignmentNear);
    Gdiplus::SolidBrush brush(Col(color));
    g.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
}

float TextWidth(Gdiplus::Graphics& g, const std::wstring& text,
                float pixelSize, bool bold) {
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, pixelSize,
                       bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);
    Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());
    Gdiplus::RectF bounds;
    g.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0, 0), &format,
                    &bounds);
    return bounds.Width;
}

// Icons are black shapes with an alpha channel (some with a white cut-out).
// The shape is kept as an alpha mask and tinted at draw time.
std::wstring IconPath(const wchar_t* name) {
    wchar_t exe[MAX_PATH * 4]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    if (!n || n >= std::size(exe)) return L"";
    std::wstring path(exe);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L"";
    return path.substr(0, slash) + L"\\icons\\" + name;
}

Gdiplus::Bitmap* IconMask(const wchar_t* name) {
    // Never destroyed on purpose: GDI+ stays alive until the process exits.
    using Cache = std::map<std::wstring, std::unique_ptr<Gdiplus::Bitmap>>;
    static Cache& cache = *new Cache();
    const auto found = cache.find(name);
    if (found != cache.end()) return found->second.get();

    std::unique_ptr<Gdiplus::Bitmap> result;
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
                            dp[0] = dp[1] = dp[2] = 0;
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
    cache[name] = std::move(result);
    return raw;
}

// Returns false when the icon file is missing so the caller can draw a
// vector fallback.
bool DrawMaskIcon(Gdiplus::Graphics& g, const wchar_t* name, Gdiplus::RectF dst,
              COLORREF tint) {
    Gdiplus::Bitmap* mask = IconMask(name);
    if (!mask) return false;
    Gdiplus::ColorMatrix matrix = {{
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0},
        {0, 0, 0, 1, 0},
        {GetRValue(tint) / 255.0f, GetGValue(tint) / 255.0f,
         GetBValue(tint) / 255.0f, 0, 1},
    }};
    Gdiplus::ImageAttributes attributes;
    attributes.SetColorMatrix(&matrix, Gdiplus::ColorMatrixFlagsDefault,
                              Gdiplus::ColorAdjustTypeBitmap);
    g.DrawImage(mask, dst, 0.0f, 0.0f, static_cast<float>(mask->GetWidth()),
                static_cast<float>(mask->GetHeight()), Gdiplus::UnitPixel,
                &attributes);
    return true;
}

// ---- profile storage --------------------------------------------------

fs::path ProfilesRoot() {
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return {};
    executable.resize(length);
    return fs::path(executable).parent_path() / L"profiles";
}

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

bool IsValidProfileName(const std::wstring& name) {
    if (name.empty() || name.size() > 64 || name == L"." || name == L"..") {
        return false;
    }
    if (name.front() == L'.' || name.compare(0, 2, L"__") == 0) return false;
    if (name.back() == L'.' || name.back() == L' ') return false;
    for (wchar_t ch : name) {
        if (ch < 32 || ch == L'<' || ch == L'>' || ch == L':' || ch == L'"' ||
            ch == L'/' || ch == L'\\' || ch == L'|' || ch == L'?' ||
            ch == L'*') {
            return false;
        }
    }
    std::wstring base = name;
    const auto dot = base.find(L'.');
    if (dot != std::wstring::npos) base.resize(dot);
    std::transform(base.begin(), base.end(), base.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL") {
        return false;
    }
    if (base.size() == 4 &&
        (base.compare(0, 3, L"COM") == 0 || base.compare(0, 3, L"LPT") == 0) &&
        base[3] >= L'1' && base[3] <= L'9') {
        return false;
    }
    return true;
}

bool GetEncoderClsid(const wchar_t* mime, CLSID* clsid) {
    UINT count = 0, bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (bytes == 0) return false;
    std::vector<BYTE> buffer(bytes);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, info) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; ++i) {
        if (std::wcscmp(info[i].MimeType, mime) == 0) {
            *clsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

// Centre-crops any image GDI+ can read to a 256x256 PNG inside the profile.
bool SaveAvatar(const fs::path& profileDir, const wchar_t* sourceFile) {
    CLSID png{};
    if (!GetEncoderClsid(L"image/png", &png)) return false;
    Gdiplus::Bitmap source(sourceFile);
    if (source.GetLastStatus() != Gdiplus::Ok) return false;
    const int w = static_cast<int>(source.GetWidth());
    const int h = static_cast<int>(source.GetHeight());
    if (w <= 0 || h <= 0) return false;
    const int side = std::min(w, h);

    Gdiplus::Bitmap out(256, 256, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&out);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        g.DrawImage(&source, Gdiplus::Rect(0, 0, 256, 256), (w - side) / 2,
                    (h - side) / 2, side, side, Gdiplus::UnitPixel);
    }
    const fs::path dest = profileDir / L"avatar.png";
    return out.Save(dest.c_str(), &png, nullptr) == Gdiplus::Ok;
}

// Decodes into memory so the file is not left locked (which would block
// renaming or deleting the profile folder).
std::shared_ptr<Gdiplus::Bitmap> LoadAvatar(const fs::path& profileDir) {
    const fs::path file = profileDir / L"avatar.png";
    std::error_code ec;
    if (!fs::exists(file, ec)) return nullptr;
    Gdiplus::Bitmap source(file.c_str());
    if (source.GetLastStatus() != Gdiplus::Ok) return nullptr;
    const int w = static_cast<int>(source.GetWidth());
    const int h = static_cast<int>(source.GetHeight());
    if (w <= 0 || h <= 0) return nullptr;
    auto copy = std::make_shared<Gdiplus::Bitmap>(w, h, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics g(copy.get());
        g.DrawImage(&source, 0, 0, w, h);
    }
    return copy;
}

int ClearProfileData(const fs::path& profile, const bool (&checked)[kCategoryCount]) {
    const fs::path bases[] = {profile, profile / L"Default",
                              profile / L"Network",
                              profile / L"Default" / L"Network"};
    int failures = 0;
    for (int c = 0; c < kCategoryCount; ++c) {
        if (!checked[c]) continue;
        for (const wchar_t* item : kCategories[c].items) {
            if (!item) break;
            for (const fs::path& base : bases) {
                std::error_code ec;
                const fs::path target = base / item;
                if (!fs::exists(target, ec)) continue;
                fs::remove_all(target, ec);
                if (ec) ++failures;
            }
        }
    }
    return failures;
}

// ---- per-monitor DPI --------------------------------------------------

struct DpiApi {
    using SetContextFn = HANDLE(WINAPI*)(HANDLE);
    using GetDpiFn = UINT(WINAPI*)(HWND);
    using AdjustFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    SetContextFn setContext = nullptr;
    GetDpiFn getDpi = nullptr;
    AdjustFn adjust = nullptr;
};

template <typename Fn>
Fn LoadFn(HMODULE module, const char* name) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(module, name)));
}

DpiApi LoadDpiApi() {
    DpiApi api;
    HMODULE user = GetModuleHandleW(L"user32.dll");
    if (user) {
        api.setContext = LoadFn<DpiApi::SetContextFn>(user, "SetThreadDpiAwarenessContext");
        api.getDpi = LoadFn<DpiApi::GetDpiFn>(user, "GetDpiForWindow");
        api.adjust = LoadFn<DpiApi::AdjustFn>(user, "AdjustWindowRectExForDpi");
    }
    return api;
}

class PickerWindow {
public:
    explicit PickerWindow(HINSTANCE instance) : instance_(instance) {}

    ~PickerWindow() {
        if (editFont_) DeleteObject(editFont_);
        if (editBrush_) DeleteObject(editBrush_);
    }

    std::optional<ProfileSelection> Run() {
        dpiApi_ = LoadDpiApi();
        // Make this thread per-monitor DPI aware (v2) only while the picker
        // is open; the rest of the app keeps its own awareness.
        HANDLE previous = nullptr;
        if (dpiApi_.setContext) {
            previous = dpiApi_.setContext(
                reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
        }

        LoadSettings();
        RebuildPalette();
        if (Create()) {
            ShowWindow(hwnd_, SW_SHOW);
            UpdateWindow(hwnd_);
            if (entries_.empty()) OpenDialog(Dlg::Create, -1);

            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                if (message.message == WM_KEYDOWN) {
                    if (message.hwnd == nameEdit_ && dlg_ != Dlg::None) {
                        if (message.wParam == VK_RETURN) { DialogPrimary(); continue; }
                        if (message.wParam == VK_ESCAPE) { CloseDialog(); continue; }
                    }
                    if (message.hwnd == hexEdit_ &&
                        (message.wParam == VK_RETURN || message.wParam == VK_ESCAPE)) {
                        SetFocus(hwnd_);
                        continue;
                    }
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        if (previous && dpiApi_.setContext) dpiApi_.setContext(previous);
        return selected_;
    }

private:
    enum class View { Picker, Settings };
    enum class Page { Profile = 0, Appearance = 1, Data = 2, Archived = 3 };
    enum class Dlg { None, Create, Rename, Delete, DeleteForever, Clear };
    enum class Act {
        None, Tile, TileMenu, AddTile, Gear, Back, Page,
        RowRename, RowPicture, RowArchive, RowDelete, RowClear,
        RowUnarchive, RowDeleteForever,
        ThemeMode, ColorTab, Slider, Swatch, MenuItem,
        DlgPrimary, DlgSecondary, DlgCheck
    };
    enum class BtnStyle { Outline, Danger, Primary, DangerFilled };

    struct Entry {
        std::wstring name;
        fs::path path;
        bool archived = false;
        std::shared_ptr<Gdiplus::Bitmap> avatar;
    };
    struct HitReg { RECT rc; Act act; int arg; };
    struct MenuDef { int id; const wchar_t* label; bool danger; };

    struct DialogLayout {
        RECT box{}, title{}, body{}, field{}, primary{}, secondary{};
        RECT check[kCategoryCount]{};
    };
    struct AppearanceLayout {
        RECT themeLabel{}, mode[4]{}, customLabel{}, tab[2]{}, preview{},
            hexField{}, sliderHit[3]{}, track[3]{}, value[3]{}, presetLabel{},
            swatch[12]{};
    };

    static constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    static constexpr DWORD kExStyle = WS_EX_APPWINDOW;

    // ---- scaling ------------------------------------------------------
    int S(int v) const { return MulDiv(v, dpi_, 96); }
    float SF(float v) const { return v * static_cast<float>(dpi_) / 96.0f; }
    void Invalidate() const { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }

    // ---- setup --------------------------------------------------------
    bool Create() {
        const fs::path root = ProfilesRoot();
        if (root.empty()) return false;
        std::error_code ec;
        fs::create_directories(root, ec);
        if (ec) {
            MessageBoxW(nullptr, L"Could not create the profiles folder.",
                        L"Vibrium Profiles", MB_OK | MB_ICONERROR);
            return false;
        }

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &PickerWindow::WindowProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hIconSm = wc.hIcon;
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kPickerClass;
        RegisterClassExW(&wc);

        hwnd_ = CreateWindowExW(kExStyle, kPickerClass, L"Vibrium", kStyle,
                                CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
                                nullptr, nullptr, instance_, this);
        if (!hwnd_) return false;
        ResizeToDpi(true);
        ApplyChrome();
        return true;
    }

    void ResizeToDpi(bool center) {
        RECT frame{0, 0, S(kClientW), S(kClientH)};
        if (dpiApi_.adjust) {
            dpiApi_.adjust(&frame, kStyle, FALSE, kExStyle, static_cast<UINT>(dpi_));
        } else {
            AdjustWindowRectEx(&frame, kStyle, FALSE, kExStyle);
        }
        const int width = frame.right - frame.left;
        const int height = frame.bottom - frame.top;
        int x = 0, y = 0;
        UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
        if (center) {
            POINT cursor{};
            GetCursorPos(&cursor);
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &info);
            x = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
            y = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
        } else {
            flags |= SWP_NOMOVE;
        }
        SetWindowPos(hwnd_, nullptr, x, y, width, height, flags);
    }

    void AddControls() {
        nameEdit_ = CreateWindowExW(
            0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNameEditId)), instance_, nullptr);
        hexEdit_ = CreateWindowExW(
            0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHexEditId)), instance_, nullptr);
        SendMessageW(nameEdit_, EM_LIMITTEXT, 64, 0);
        SendMessageW(hexEdit_, EM_LIMITTEXT, 7, 0);
        SendMessageW(nameEdit_, 0x1501 /* EM_SETCUEBANNER */, TRUE,
                     reinterpret_cast<LPARAM>(L"Profile name"));
        RecreateFonts();
        RebuildPalette();
        Reload();
        SyncHexText();
        if (!entries_.empty()) focus_ = 0;
    }

    void RecreateFonts() {
        if (editFont_) DeleteObject(editFont_);
        editFont_ = CreateFontW(-S(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (nameEdit_) SendMessageW(nameEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
        if (hexEdit_) SendMessageW(hexEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
    }

    // ---- settings / palette --------------------------------------------
    fs::path SettingsPath() const { return ProfilesRoot() / L"picker.ini"; }

    void LoadSettings() {
        const std::wstring ini = SettingsPath().wstring();
        const int m = GetPrivateProfileIntW(L"Appearance", L"Mode", 0, ini.c_str());
        mode_ = (m >= 0 && m <= 3) ? static_cast<Mode>(m) : Mode::System;
        wchar_t buffer[32]{};
        COLORREF parsed = 0;
        GetPrivateProfileStringW(L"Appearance", L"Background", L"", buffer, 32, ini.c_str());
        if (ParseHex(buffer, &parsed)) customBg_ = parsed;
        GetPrivateProfileStringW(L"Appearance", L"Accent", L"", buffer, 32, ini.c_str());
        if (ParseHex(buffer, &parsed)) customAccent_ = parsed;
    }

    void SaveSettings() const {
        const std::wstring ini = SettingsPath().wstring();
        WritePrivateProfileStringW(L"Appearance", L"Mode",
                                   std::to_wstring(static_cast<int>(mode_)).c_str(), ini.c_str());
        WritePrivateProfileStringW(L"Appearance", L"Background",
                                   HexString(customBg_).c_str(), ini.c_str());
        WritePrivateProfileStringW(L"Appearance", L"Accent",
                                   HexString(customAccent_).c_str(), ini.c_str());
    }

    void RebuildPalette() {
        pal_ = DerivePalette(mode_, customBg_, customAccent_, Theme::IsSystemDark());
        if (editBrush_) DeleteObject(editBrush_);
        editBrush_ = CreateSolidBrush(pal_.field);
        ApplyChrome();
        Invalidate();
    }

    void ApplyChrome() {
        if (!hwnd_) return;
        const BOOL dark = pal_.dark ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd_, kDwmImmersiveDark, &dark, sizeof(dark));
        const COLORREF caption = view_ == View::Settings ? pal_.sidebar : pal_.bgTop;
        DwmSetWindowAttribute(hwnd_, kDwmCaptionColor, &caption, sizeof(caption));
        const COLORREF text = pal_.text;
        DwmSetWindowAttribute(hwnd_, kDwmTextColor, &text, sizeof(text));
    }

    COLORREF& CurrentColor() { return editBg_ ? customBg_ : customAccent_; }

    void SetCurrentColor(COLORREF c, bool updateHex) {
        CurrentColor() = c;
        mode_ = Mode::Custom;
        RebuildPalette();
        if (updateHex) SyncHexText();
    }

    void SyncHexText() {
        if (!hexEdit_) return;
        updatingHex_ = true;
        SetWindowTextW(hexEdit_, HexString(CurrentColor()).c_str());
        updatingHex_ = false;
    }

    // ---- profiles -----------------------------------------------------
    void Reload() {
        entries_.clear();
        std::error_code ec;
        for (fs::directory_iterator it(ProfilesRoot(), ec), end; !ec && it != end;
             it.increment(ec)) {
            std::error_code e2;
            if (!it->is_directory(e2) || e2) continue;
            const std::wstring name = it->path().filename().wstring();
            if (name.empty() || name[0] == L'.' || name.compare(0, 2, L"__") == 0) continue;
            Entry entry;
            entry.name = name;
            entry.path = it->path();
            entry.archived = fs::exists(entry.path / L".archived", e2);
            entry.avatar = LoadAvatar(entry.path);
            entries_.push_back(std::move(entry));
        }
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            return ToLower(a.name) < ToLower(b.name);
        });
    }

    bool EntryValid(int i) const {
        return i >= 0 && static_cast<size_t>(i) < entries_.size();
    }

    std::vector<int> Indices(bool archived) const {
        std::vector<int> result;
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].archived == archived) result.push_back(static_cast<int>(i));
        }
        return result;
    }

    int FindByName(const std::wstring& name) const {
        const std::wstring lowered = ToLower(name);
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (ToLower(entries_[i].name) == lowered) return static_cast<int>(i);
        }
        return -1;
    }

    void Warn(const wchar_t* text) const {
        MessageBoxW(hwnd_, text, L"Vibrium Profiles", MB_OK | MB_ICONWARNING);
    }

    std::wstring EditText(HWND edit) const {
        const int length = GetWindowTextLengthW(edit);
        if (length <= 0) return L"";
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(edit, text.data(), length + 1);
        text.resize(static_cast<size_t>(length));
        return text;
    }

    bool ValidateName(const std::wstring& name) const {
        if (name.empty()) { Warn(L"Enter a profile name."); return false; }
        if (!IsValidProfileName(name)) {
            Warn(L"That name can't be used. Avoid \\ / : * ? \" < > | and names "
                 L"that start with a dot or end with a space or dot.");
            return false;
        }
        return true;
    }

    void Launch(int index) {
        if (!EntryValid(index)) return;
        selected_ = ProfileSelection{entries_[static_cast<size_t>(index)].name,
                                     entries_[static_cast<size_t>(index)].path};
        DestroyWindow(hwnd_);
    }

    void SetArchived(int index, bool archived) {
        if (!EntryValid(index)) return;
        const fs::path marker = entries_[static_cast<size_t>(index)].path / L".archived";
        if (archived) {
            HANDLE file = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) { Warn(L"Could not archive the profile."); return; }
            CloseHandle(file);
        } else {
            std::error_code ec;
            fs::remove(marker, ec);
        }
        Reload();
        Invalidate();
    }

    void ChoosePicture(int index) {
        if (!EntryValid(index)) return;
        wchar_t file[MAX_PATH * 4]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFile = file;
        ofn.nMaxFile = static_cast<DWORD>(sizeof(file) / sizeof(file[0]));
        ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0All files\0*.*\0";
        ofn.lpstrTitle = L"Choose a profile picture";
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                    OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
        if (!GetOpenFileNameW(&ofn)) return;
        if (!SaveAvatar(entries_[static_cast<size_t>(index)].path, file)) {
            Warn(L"That file could not be used as a picture.");
            return;
        }
        Reload();
        Invalidate();
    }

    void RemovePicture(int index) {
        if (!EntryValid(index)) return;
        std::error_code ec;
        fs::remove(entries_[static_cast<size_t>(index)].path / L"avatar.png", ec);
        Reload();
        Invalidate();
    }

    // ---- dialogs ------------------------------------------------------
    void OpenDialog(Dlg kind, int entry) {
        CloseMenu();
        dlg_ = kind;
        dlgEntry_ = entry;
        for (bool& c : dlgChecks_) c = true;
        if (kind == Dlg::Rename && EntryValid(entry)) {
            SetWindowTextW(nameEdit_, entries_[static_cast<size_t>(entry)].name.c_str());
        } else if (kind == Dlg::Create) {
            SetWindowTextW(nameEdit_, L"");
        }
        SyncControls();
        Invalidate();
        if (kind == Dlg::Create || kind == Dlg::Rename) {
            SetFocus(nameEdit_);
            SendMessageW(nameEdit_, EM_SETSEL, 0, -1);
        }
    }

    void CloseDialog() {
        dlg_ = Dlg::None;
        dlgEntry_ = -1;
        SyncControls();
        SetFocus(hwnd_);
        Invalidate();
    }

    void DialogPrimary() {
        switch (dlg_) {
        case Dlg::Create: {
            const std::wstring name = EditText(nameEdit_);
            if (!ValidateName(name)) return;
            const fs::path path = ProfilesRoot() / name;
            std::error_code ec;
            if (!fs::create_directory(path, ec)) {
                Warn(ec ? L"Could not create the profile folder."
                        : L"A profile with that name already exists.");
                return;
            }
            Reload();
            const int index = FindByName(name);
            CloseDialog();
            if (index >= 0) Launch(index);   // like Chrome: open it right away
            return;
        }
        case Dlg::Rename: {
            if (!EntryValid(dlgEntry_)) { CloseDialog(); return; }
            const std::wstring name = EditText(nameEdit_);
            const Entry old = entries_[static_cast<size_t>(dlgEntry_)];
            if (name == old.name) { CloseDialog(); return; }
            if (!ValidateName(name)) return;
            const fs::path target = ProfilesRoot() / name;
            std::error_code ec;
            const bool caseOnly = ToLower(name) == ToLower(old.name);
            if (!caseOnly && fs::exists(target, ec)) {
                Warn(L"A profile with that name already exists.");
                return;
            }
            fs::rename(old.path, target, ec);
            if (ec) {
                Warn(L"Could not rename the profile. Make sure it is not open in Vibrium.");
                return;
            }
            Reload();
            CloseDialog();
            return;
        }
        case Dlg::Delete:
        case Dlg::DeleteForever: {
            if (EntryValid(dlgEntry_)) {
                std::error_code ec;
                fs::remove_all(entries_[static_cast<size_t>(dlgEntry_)].path, ec);
                if (ec) Warn(L"Some files could not be deleted. Close Vibrium and try again.");
            }
            Reload();
            CloseDialog();
            return;
        }
        case Dlg::Clear: {
            if (EntryValid(dlgEntry_)) {
                const int failures = ClearProfileData(
                    entries_[static_cast<size_t>(dlgEntry_)].path, dlgChecks_);
                MessageBoxW(hwnd_,
                    failures == 0 ? L"The selected data was cleared."
                                  : L"Most data was cleared, but some files were in use and were skipped.",
                    L"Vibrium Profiles", MB_OK | MB_ICONINFORMATION);
            }
            CloseDialog();
            return;
        }
        case Dlg::None:
            return;
        }
    }

    bool AnyChecked() const {
        for (bool c : dlgChecks_) if (c) return true;
        return false;
    }

    // ---- menu ---------------------------------------------------------
    void OpenMenu(int entry, RECT anchor) {
        menuOpen_ = true;
        menuEntry_ = entry;
        menuAnchor_ = anchor;
        Invalidate();
    }

    void CloseMenu() {
        if (!menuOpen_) return;
        menuOpen_ = false;
        Invalidate();
    }

    std::vector<MenuDef> MenuItems() const {
        std::vector<MenuDef> items;
        items.push_back({0, L"Rename", false});
        items.push_back({1, L"Choose profile picture", false});
        if (EntryValid(menuEntry_) && entries_[static_cast<size_t>(menuEntry_)].avatar) {
            items.push_back({2, L"Remove picture", false});
        }
        items.push_back({3, L"Archive", false});
        items.push_back({4, L"Delete", true});
        return items;
    }

    void DoMenuItem(int id) {
        const int entry = menuEntry_;
        CloseMenu();
        switch (id) {
        case 0: OpenDialog(Dlg::Rename, entry); break;
        case 1: ChoosePicture(entry); break;
        case 2: RemovePicture(entry); break;
        case 3: SetArchived(entry, true); break;
        case 4: OpenDialog(Dlg::Delete, entry); break;
        }
    }

    // ---- layouts shared by painting and control placement --------------
    DialogLayout MakeDialogLayout() const {
        DialogLayout d;
        const int bw = S(480), pad = S(28);
        const bool deleting = dlg_ == Dlg::Delete || dlg_ == Dlg::DeleteForever;
        const bool field = dlg_ == Dlg::Create || dlg_ == Dlg::Rename;
        const int bodyH = deleting ? S(68) : S(24);
        const int fieldH = field ? S(48) + S(16) : 0;
        const int checksH = dlg_ == Dlg::Clear ? kCategoryCount * S(36) + S(8) : 0;
        const int btnH = S(42);
        const int h = S(26) + S(34) + S(8) + bodyH + S(16) + fieldH + checksH +
                      S(20) + btnH + S(26);
        const int x = (S(kClientW) - bw) / 2;
        const int y = (S(kClientH) - h) / 2;
        d.box = RECT{x, y, x + bw, y + h};
        d.title = RECT{x + pad, y + S(26), x + bw - pad, y + S(26) + S(34)};
        d.body = RECT{x + pad, d.title.bottom + S(8), x + bw - pad, d.title.bottom + S(8) + bodyH};
        int cursor = d.body.bottom + S(16);
        if (field) d.field = RECT{x + pad, cursor, x + bw - pad, cursor + S(48)};
        cursor += fieldH;
        for (int i = 0; i < kCategoryCount; ++i) {
            d.check[i] = RECT{x + pad, cursor + i * S(36), x + bw - pad, cursor + i * S(36) + S(32)};
        }
        cursor += checksH + S(20);
        d.primary = RECT{x + bw - pad - S(130), cursor, x + bw - pad, cursor + btnH};
        d.secondary = RECT{d.primary.left - S(10) - S(100), cursor, d.primary.left - S(10), cursor + btnH};
        return d;
    }

    AppearanceLayout MakeAppearanceLayout() const {
        AppearanceLayout a;
        const int left = S(252);
        int y = S(104);
        a.themeLabel = RECT{left, y, left + S(400), y + S(24)};
        y += S(32);
        for (int i = 0; i < 4; ++i) {
            const int x = left + i * (S(130) + S(10));
            a.mode[i] = RECT{x, y, x + S(130), y + S(40)};
        }
        y += S(40) + S(30);
        a.customLabel = RECT{left, y, left + S(660), y + S(24)};
        y += S(34);
        for (int i = 0; i < 2; ++i) {
            const int x = left + i * (S(130) + S(10));
            a.tab[i] = RECT{x, y, x + S(130), y + S(36)};
        }
        y += S(36) + S(18);
        a.preview = RECT{left, y, left + S(56), y + S(56)};
        a.hexField = RECT{left + S(72), y + S(8), left + S(72) + S(180), y + S(8) + S(40)};
        y += S(56) + S(22);
        for (int c = 0; c < 3; ++c) {
            const int row = y + c * S(38);
            a.track[c] = RECT{left + S(32), row + S(11), left + S(32) + S(400), row + S(23)};
            a.sliderHit[c] = RECT{a.track[c].left - S(12), row, a.track[c].right + S(12), row + S(34)};
            a.value[c] = RECT{a.track[c].right + S(18), row, a.track[c].right + S(18) + S(56), row + S(34)};
        }
        y += 3 * S(38) + S(10);
        a.presetLabel = RECT{left, y, left + S(300), y + S(22)};
        y += S(28);
        for (int i = 0; i < 12; ++i) {
            const int x = left + i * (S(30) + S(14));
            a.swatch[i] = RECT{x, y, x + S(30), y + S(30)};
        }
        return a;
    }

    // Moves/shows the two real EDIT controls to match the current screen.
    void SyncControls() {
        if (!nameEdit_ || !hexEdit_) return;
        const bool showName = dlg_ == Dlg::Create || dlg_ == Dlg::Rename;
        if (showName) {
            const DialogLayout d = MakeDialogLayout();
            const int editH = S(24);
            MoveWindow(nameEdit_, d.field.left + S(16),
                       d.field.top + ((d.field.bottom - d.field.top) - editH) / 2,
                       (d.field.right - d.field.left) - S(32), editH, TRUE);
        }
        ShowWindow(nameEdit_, showName ? SW_SHOW : SW_HIDE);

        const bool showHex = view_ == View::Settings && page_ == Page::Appearance &&
                             dlg_ == Dlg::None;
        if (showHex) {
            const AppearanceLayout a = MakeAppearanceLayout();
            const int editH = S(24);
            MoveWindow(hexEdit_, a.hexField.left + S(16),
                       a.hexField.top + ((a.hexField.bottom - a.hexField.top) - editH) / 2,
                       (a.hexField.right - a.hexField.left) - S(32), editH, TRUE);
        }
        ShowWindow(hexEdit_, showHex ? SW_SHOW : SW_HIDE);
    }

    void SetView(View view) {
        view_ = view;
        pageScroll_ = 0;
        CloseMenu();
        ApplyChrome();
        SyncControls();
        Invalidate();
    }

    void SetPage(Page page) {
        page_ = page;
        pageScroll_ = 0;
        if (page == Page::Appearance) SyncHexText();
        SyncControls();
        Invalidate();
    }

    // ---- hit registry -------------------------------------------------
    void AddHit(RECT rc, Act act, int arg, const RECT* clip = nullptr,
                bool force = false) {
        if (!acceptHits_ && !force) return;
        if (clip) {
            RECT out{};
            if (!IntersectRect(&out, &rc, clip)) return;
            rc = out;
        }
        hits_.push_back(HitReg{rc, act, arg});
    }

    const HitReg* HitAt(POINT pt) const {
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
            if (PtInRect(&it->rc, pt)) return &*it;
        }
        return nullptr;
    }

    bool IsHot(Act act, int arg) const { return hotAct_ == act && hotArg_ == arg; }

    void UpdateHot(POINT pt) {
        const HitReg* reg = HitAt(pt);
        const Act act = reg ? reg->act : Act::None;
        const int arg = reg ? reg->arg : -1;
        if (act != hotAct_ || arg != hotArg_) {
            hotAct_ = act;
            hotArg_ = arg;
            Invalidate();
        }
    }

    void RefreshHot() {
        if (!hwnd_) return;
        POINT pt{};
        GetCursorPos(&pt);
        ScreenToClient(hwnd_, &pt);
        UpdateHot(hoverActive_ ? pt : POINT{-1, -1});
    }

    // ---- painting -----------------------------------------------------
    void Paint(HDC target) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        const int w = rc.right;
        const int h = rc.bottom;
        if (w <= 0 || h <= 0) return;

        HDC mem = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, w, h);
        HGDIOBJ oldBitmap = SelectObject(mem, bitmap);
        {
            Gdiplus::Graphics g(mem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            hits_.clear();
            acceptHits_ = dlg_ == Dlg::None && !menuOpen_;

            {
                Gdiplus::LinearGradientBrush bg(Gdiplus::Point(0, 0), Gdiplus::Point(0, h),
                                                Col(pal_.bgTop), Col(pal_.bgBottom));
                g.FillRectangle(&bg, 0, 0, w, h);
            }

            if (view_ == View::Picker) DrawPicker(g, w, h);
            else DrawSettings(g, w, h);
            if (menuOpen_) DrawMenu(g, w, h);
            if (dlg_ != Dlg::None) DrawDialog(g, w, h);
        }
        BitBlt(target, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(mem);
    }

    void DrawShadow(Gdiplus::Graphics& g, RECT box, float radius) {
        for (int i = 10; i >= 1; --i) {
            Gdiplus::RectF r = FR(box);
            const float grow = SF(static_cast<float>(i) * 1.6f);
            r.X -= grow;
            r.Y -= grow - SF(4);
            r.Width += grow * 2;
            r.Height += grow * 2;
            FillRound(g, r, radius + grow, Gdiplus::Color(10, 0, 0, 0));
        }
    }

    void DrawScrollbar(Gdiplus::Graphics& g, int x, int top, int viewH,
                       int contentH, int scroll) {
        if (contentH <= viewH) return;
        const float trackH = static_cast<float>(viewH);
        const float thumbH = std::max(SF(32), trackH * static_cast<float>(viewH) /
                                                  static_cast<float>(contentH));
        const float range = static_cast<float>(contentH - viewH);
        const float y = static_cast<float>(top) +
                        (trackH - thumbH) * (static_cast<float>(scroll) / range);
        FillRound(g, Gdiplus::RectF(static_cast<float>(x), y, SF(5), thumbH),
                  SF(2.5f), Col(pal_.muted, 150));
    }

    void DrawAvatar(Gdiplus::Graphics& g, const Entry& entry,
                    Gdiplus::RectF circle, float fontPx) {
        if (entry.avatar) {
            const Gdiplus::GraphicsState state = g.Save();
            Gdiplus::GraphicsPath path;
            path.AddEllipse(circle);
            g.SetClip(&path, Gdiplus::CombineModeIntersect);
            g.DrawImage(entry.avatar.get(), circle);
            g.Restore(state);
            return;
        }
        FillCircle(g, circle, Col(AvatarColor(entry.name)));
        const std::wstring initial(
            1, entry.name.empty() ? L'?' : static_cast<wchar_t>(std::towupper(entry.name[0])));
        DrawLabel(g, initial, circle, fontPx, true, RGB(255, 255, 255));
    }

    void DrawPill(Gdiplus::Graphics& g, RECT rc, const std::wstring& text,
                  BtnStyle style, bool hot, bool enabled = true) {
        const Gdiplus::RectF r = FR(rc);
        const float radius = r.Height / 2.0f;
        const COLORREF white = RGB(255, 255, 255);
        COLORREF textColor = pal_.text;
        if (!enabled) {
            FillRound(g, r, radius, Col(pal_.cardHover));
            DrawLabel(g, text, r, SF(14), true, pal_.muted);
            return;
        }
        switch (style) {
        case BtnStyle::Primary:
            FillRound(g, r, radius, Col(hot ? Mix(pal_.accent, white, 0.15) : pal_.accent));
            textColor = pal_.onAccent;
            break;
        case BtnStyle::DangerFilled:
            FillRound(g, r, radius, Col(hot ? Mix(pal_.danger, white, 0.15) : pal_.danger));
            textColor = white;
            break;
        case BtnStyle::Danger:
            if (hot) FillRound(g, r, radius, Col(pal_.danger, 40));
            StrokeRound(g, r, radius, Col(pal_.dangerText, 150), 1.0f);
            textColor = pal_.dangerText;
            break;
        case BtnStyle::Outline:
            if (hot) FillRound(g, r, radius, Col(Mix(pal_.card, pal_.text, 0.10)));
            StrokeRound(g, r, radius, Col(pal_.line), 1.0f);
            break;
        }
        DrawLabel(g, text, r, SF(14), true, textColor);
    }

    // Right-aligned row button; moves `rightEdge` left past it.
    void DrawRowButton(Gdiplus::Graphics& g, int& rightEdge, const RECT& row,
                       const std::wstring& text, Act act, int arg,
                       BtnStyle style, const RECT& clip) {
        const int bw = std::max(S(76), static_cast<int>(TextWidth(g, text, SF(14), true)) + S(34));
        const int bh = S(36);
        const int top = row.top + ((row.bottom - row.top) - bh) / 2;
        const RECT rc{rightEdge - bw, top, rightEdge, top + bh};
        AddHit(rc, act, arg, &clip);
        DrawPill(g, rc, text, style, IsHot(act, arg));
        rightEdge = rc.left - S(8);
    }

    // ---- main picker screen -----------------------------------------
    static constexpr int kTileW = 190;
    static constexpr int kTileH = 190;
    static constexpr int kTileGap = 22;
    static constexpr int kTilePad = 6;
    static constexpr int kGridTop = 148;
    static constexpr int kGridBottom = 560;
    static constexpr int kFooterH = 64;

    int PickerItemCount() const { return static_cast<int>(Indices(false).size()) + 1; }

    void DrawPicker(Gdiplus::Graphics& g, int w, int h) {
        const float fw = static_cast<float>(w);
        const std::vector<int> active = Indices(false);
        DrawLabel(g, L"Who\u2019s using Vibrium?", Gdiplus::RectF(0, SF(36), fw, SF(52)),
                  SF(34), true, pal_.text);
        DrawLabel(g, active.empty() ? L"Create a profile to get started."
                                    : L"Choose a profile to continue, or add a new one.",
                  Gdiplus::RectF(0, SF(92), fw, SF(28)), SF(15), false, pal_.muted);

        const int tileW = S(kTileW), tileH = S(kTileH), gap = S(kTileGap), pad = S(kTilePad);
        const int top = S(kGridTop), bottom = S(kGridBottom);
        const int items = static_cast<int>(active.size()) + 1;
        const int rows = (items + kColumns - 1) / kColumns;
        const int content = rows * tileH + (rows - 1) * gap + pad * 2;
        gridScroll_ = std::clamp(gridScroll_, 0, std::max(0, content - (bottom - top)));
        focus_ = std::clamp(focus_, 0, items - 1);

        const RECT clip{0, top, w, bottom};
        g.SetClip(FR(clip));
        for (int i = 0; i < items; ++i) {
            const int row = i / kColumns;
            const int col = i % kColumns;
            const int inRow = std::min(kColumns, items - row * kColumns);
            const int rowWidth = inRow * tileW + (inRow - 1) * gap;
            const int x = (w - rowWidth) / 2 + col * (tileW + gap);
            const int y = top + pad + row * (tileH + gap) - gridScroll_;
            const RECT r{x, y, x + tileW, y + tileH};
            if (r.bottom < top || r.top > bottom) continue;
            const bool focused = keyboardFocus_ && focus_ == i;
            if (i < static_cast<int>(active.size())) DrawProfileTile(g, active[static_cast<size_t>(i)], r, clip, focused);
            else DrawAddTile(g, r, clip, focused);
        }
        g.ResetClip();
        DrawScrollbar(g, w - S(16), top, bottom - top, content, gridScroll_);

        // Footer.
        const int footerTop = h - S(kFooterH);
        {
            Gdiplus::Pen line(Col(pal_.line), 1.0f);
            g.DrawLine(&line, 0, footerTop, w, footerTop);
        }
        DrawLabel(g, L"Each profile keeps its own sign-ins, cookies and settings.",
                  Gdiplus::RectF(static_cast<float>(S(40)), static_cast<float>(footerTop),
                                 static_cast<float>(w - S(40) * 3 - S(44)),
                                 static_cast<float>(S(kFooterH))),
                  SF(13), false, pal_.muted, Gdiplus::StringAlignmentNear);

        const int gear = S(44);
        const RECT gearRect{w - S(40) - gear, footerTop + (S(kFooterH) - gear) / 2,
                            w - S(40), footerTop + (S(kFooterH) - gear) / 2 + gear};
        AddHit(gearRect, Act::Gear, 0);
        if (IsHot(Act::Gear, 0)) FillCircle(g, FR(gearRect), Col(pal_.cardHover));
        const float gi = SF(26);
        const Gdiplus::RectF gearIcon(
            static_cast<float>(gearRect.left) + (static_cast<float>(gear) - gi) / 2,
            static_cast<float>(gearRect.top) + (static_cast<float>(gear) - gi) / 2, gi, gi);
        if (!DrawMaskIcon(g, L"settings.png", gearIcon, pal_.text)) {
            Gdiplus::Pen pen(Col(pal_.text), SF(2.5f));
            g.DrawEllipse(&pen, Gdiplus::RectF(gearIcon.X + gi * 0.2f, gearIcon.Y + gi * 0.2f,
                                              gi * 0.6f, gi * 0.6f));
        }
    }

    void DrawProfileTile(Gdiplus::Graphics& g, int index, const RECT& r,
                         const RECT& clip, bool focused) {
        const Entry& entry = entries_[static_cast<size_t>(index)];
        const bool hot = IsHot(Act::Tile, index) || IsHot(Act::TileMenu, index);
        AddHit(r, Act::Tile, index, &clip);

        const Gdiplus::RectF rect = FR(r);
        FillRound(g, rect, SF(18), Col(hot || focused ? pal_.cardHover : pal_.card));
        StrokeRound(g, rect, SF(18), Col(pal_.line), 1.0f);
        if (focused) StrokeRound(g, rect, SF(18), Col(pal_.accent), SF(2));

        const float d = SF(96);
        const Gdiplus::RectF circle(rect.X + (rect.Width - d) / 2, rect.Y + SF(30), d, d);
        DrawAvatar(g, entry, circle, SF(44));
        DrawLabel(g, entry.name,
                  Gdiplus::RectF(rect.X + SF(14), circle.GetBottom() + SF(12), rect.Width - SF(28), SF(32)),
                  SF(17), true, pal_.text);

        // "..." button, top right.
        const int bs = S(34);
        const RECT menuRect{r.right - S(10) - bs, r.top + S(10), r.right - S(10), r.top + S(10) + bs};
        AddHit(menuRect, Act::TileMenu, index, &clip);
        const bool menuHot = IsHot(Act::TileMenu, index);
        if (menuHot) FillCircle(g, FR(menuRect), Col(Mix(pal_.cardHover, pal_.text, 0.12)));
        const float is = SF(22);
        const Gdiplus::RectF icon(
            static_cast<float>(menuRect.left) + (static_cast<float>(bs) - is) / 2,
            static_cast<float>(menuRect.top) + (static_cast<float>(bs) - is) / 2, is, is);
        if (!DrawMaskIcon(g, L"3dots.png", icon, menuHot ? pal_.text : pal_.muted)) {
            const float dot = SF(3.5f);
            for (int k = -1; k <= 1; ++k) {
                FillCircle(g, Gdiplus::RectF(icon.X + is / 2 - dot / 2,
                                             icon.Y + is / 2 - dot / 2 + static_cast<float>(k) * SF(7), dot, dot),
                           Col(pal_.muted));
            }
        }
    }

    void DrawAddTile(Gdiplus::Graphics& g, const RECT& r, const RECT& clip, bool focused) {
        const bool hot = IsHot(Act::AddTile, 0);
        AddHit(r, Act::AddTile, 0, &clip);
        const Gdiplus::RectF rect = FR(r);
        if (hot || focused) FillRound(g, rect, SF(18), Col(pal_.cardHover, 160));
        StrokeRound(g, rect, SF(18), Col(focused ? pal_.accent : pal_.line), focused ? SF(2) : 1.5f, !focused);

        const float d = SF(96);
        const Gdiplus::RectF circle(rect.X + (rect.Width - d) / 2, rect.Y + SF(30), d, d);
        FillCircle(g, circle, Col(pal_.accent, hot ? 70 : 40));
        Gdiplus::Pen plus(Col(pal_.accent), SF(3.5f));
        plus.SetStartCap(Gdiplus::LineCapRound);
        plus.SetEndCap(Gdiplus::LineCapRound);
        const float cx = circle.X + d / 2, cy = circle.Y + d / 2, arm = SF(17);
        g.DrawLine(&plus, cx - arm, cy, cx + arm, cy);
        g.DrawLine(&plus, cx, cy - arm, cx, cy + arm);
        DrawLabel(g, L"Add profile",
                  Gdiplus::RectF(rect.X + SF(14), circle.GetBottom() + SF(12), rect.Width - SF(28), SF(32)),
                  SF(17), true, pal_.accent);
    }

    // ---- settings screen --------------------------------------------
    void DrawSettings(Gdiplus::Graphics& g, int w, int h) {
        const int sidebarW = S(220);
        {
            Gdiplus::SolidBrush brush(Col(pal_.sidebar));
            g.FillRectangle(&brush, 0, 0, sidebarW, h);
            Gdiplus::Pen line(Col(pal_.line), 1.0f);
            g.DrawLine(&line, sidebarW, 0, sidebarW, h);
        }

        // Back button + title.
        const int bs = S(40);
        const RECT back{S(14), S(14), S(14) + bs, S(14) + bs};
        AddHit(back, Act::Back, 0);
        if (IsHot(Act::Back, 0)) FillCircle(g, FR(back), Col(pal_.cardHover));
        const float is = SF(20);
        const Gdiplus::RectF icon(
            static_cast<float>(back.left) + (static_cast<float>(bs) - is) / 2,
            static_cast<float>(back.top) + (static_cast<float>(bs) - is) / 2, is, is);
        if (!DrawMaskIcon(g, L"backward.png", icon, pal_.text)) {
            Gdiplus::Pen pen(Col(pal_.text), SF(2.5f));
            Gdiplus::PointF pts[3] = {{icon.X + is * 0.65f, icon.Y + is * 0.15f},
                                      {icon.X + is * 0.3f, icon.Y + is * 0.5f},
                                      {icon.X + is * 0.65f, icon.Y + is * 0.85f}};
            g.DrawLines(&pen, pts, 3);
        }
        DrawLabel(g, L"Settings",
                  Gdiplus::RectF(static_cast<float>(S(64)), static_cast<float>(S(14)),
                                 static_cast<float>(sidebarW - S(64)), static_cast<float>(bs)),
                  SF(20), true, pal_.text, Gdiplus::StringAlignmentNear);

        // Navigation.
        static const wchar_t* const kNav[4] = {L"Profile", L"Appearance", L"Data controls", L"Archived"};
        const int archivedCount = static_cast<int>(Indices(true).size());
        int y = S(84);
        for (int p = 0; p < 4; ++p) {
            const RECT item{S(12), y, sidebarW - S(12), y + S(44)};
            AddHit(item, Act::Page, p);
            const bool selected = static_cast<int>(page_) == p;
            const bool hot = IsHot(Act::Page, p);
            if (selected) FillRound(g, FR(item), SF(22), Col(pal_.accent, 44));
            else if (hot) FillRound(g, FR(item), SF(22), Col(pal_.cardHover, 210));
            Gdiplus::RectF textRect = FR(item);
            textRect.X += SF(20);
            textRect.Width -= SF(40);
            DrawLabel(g, kNav[p], textRect, SF(15), selected, selected ? pal_.accent : pal_.text,
                      Gdiplus::StringAlignmentNear);
            if (p == 3 && archivedCount > 0) {
                DrawLabel(g, std::to_wstring(archivedCount), textRect, SF(13), false, pal_.muted,
                          Gdiplus::StringAlignmentFar);
            }
            y += S(48);
        }

        // Page header.
        static const wchar_t* const kTitles[4] = {L"Profile", L"Appearance", L"Data controls", L"Archived"};
        static const wchar_t* const kDescriptions[4] = {
            L"Rename, change the picture, archive or delete a profile.",
            L"Choose how this profile picker looks. Any color works.",
            L"Erase browsing data for an individual profile.",
            L"Archived profiles are hidden from the picker until you restore them.",
        };
        const int left = S(252), right = w - S(36);
        const int pi = static_cast<int>(page_);
        DrawLabel(g, kTitles[pi],
                  Gdiplus::RectF(static_cast<float>(left), static_cast<float>(S(26)),
                                 static_cast<float>(right - left), static_cast<float>(S(40))),
                  SF(26), true, pal_.text, Gdiplus::StringAlignmentNear);
        DrawLabel(g, kDescriptions[pi],
                  Gdiplus::RectF(static_cast<float>(left), static_cast<float>(S(66)),
                                 static_cast<float>(right - left), static_cast<float>(S(24))),
                  SF(14), false, pal_.muted, Gdiplus::StringAlignmentNear);

        if (page_ == Page::Appearance) DrawAppearance(g);
        else DrawRows(g, w, h, page_);
    }

    void DrawRows(Gdiplus::Graphics& g, int w, int h, Page page) {
        const bool archivedPage = page == Page::Archived;
        const std::vector<int> list = Indices(archivedPage);
        const int left = S(252), right = w - S(36);
        const int top = S(100), bottom = h - S(16);
        const int rowH = S(68), gap = S(10);
        const int n = static_cast<int>(list.size());
        const int content = n > 0 ? n * rowH + (n - 1) * gap + S(8) : 0;
        pageScroll_ = std::clamp(pageScroll_, 0, std::max(0, content - (bottom - top)));

        if (n == 0) {
            DrawLabel(g, archivedPage ? L"No archived profiles." : L"No profiles yet.",
                      Gdiplus::RectF(static_cast<float>(left), static_cast<float>(S(120)),
                                     static_cast<float>(right - left), static_cast<float>(S(30))),
                      SF(15), false, pal_.muted, Gdiplus::StringAlignmentNear);
            return;
        }

        const RECT clip{left - S(8), top, w, bottom};
        g.SetClip(FR(clip));
        for (int i = 0; i < n; ++i) {
            const int index = list[static_cast<size_t>(i)];
            const RECT row{left, top + S(4) + i * (rowH + gap) - pageScroll_, right,
                           top + S(4) + i * (rowH + gap) - pageScroll_ + rowH};
            if (row.bottom < top || row.top > bottom) continue;
            const Entry& entry = entries_[static_cast<size_t>(index)];

            FillRound(g, FR(row), SF(14), Col(pal_.card));
            StrokeRound(g, FR(row), SF(14), Col(pal_.line), 1.0f);

            const float d = SF(44);
            const Gdiplus::RectF circle(static_cast<float>(row.left + S(16)),
                                        static_cast<float>(row.top) + (static_cast<float>(rowH) - d) / 2, d, d);
            DrawAvatar(g, entry, circle, SF(20));

            int edge = row.right - S(16);
            switch (page) {
            case Page::Profile:
                DrawRowButton(g, edge, row, L"Delete", Act::RowDelete, index, BtnStyle::Danger, clip);
                DrawRowButton(g, edge, row, L"Archive", Act::RowArchive, index, BtnStyle::Outline, clip);
                DrawRowButton(g, edge, row, L"Picture", Act::RowPicture, index, BtnStyle::Outline, clip);
                DrawRowButton(g, edge, row, L"Rename", Act::RowRename, index, BtnStyle::Outline, clip);
                break;
            case Page::Data:
                DrawRowButton(g, edge, row, L"Clear profile data", Act::RowClear, index, BtnStyle::Outline, clip);
                break;
            case Page::Archived:
                DrawRowButton(g, edge, row, L"Delete forever", Act::RowDeleteForever, index, BtnStyle::Danger, clip);
                DrawRowButton(g, edge, row, L"Unarchive", Act::RowUnarchive, index, BtnStyle::Outline, clip);
                DrawRowButton(g, edge, row, L"Clear profile data", Act::RowClear, index, BtnStyle::Outline, clip);
                break;
            default:
                break;
            }

            DrawLabel(g, entry.name,
                      Gdiplus::RectF(circle.GetRight() + SF(14), static_cast<float>(row.top),
                                     static_cast<float>(edge - S(8)) - (circle.GetRight() + SF(14)),
                                     static_cast<float>(rowH)),
                      SF(16), true, pal_.text, Gdiplus::StringAlignmentNear);
        }
        g.ResetClip();
        DrawScrollbar(g, w - S(18), top, bottom - top, content, pageScroll_);
    }

    void DrawAppearance(Gdiplus::Graphics& g) {
        const AppearanceLayout a = MakeAppearanceLayout();
        const COLORREF current = editBg_ ? customBg_ : customAccent_;

        DrawLabel(g, L"Picker theme", FR(a.themeLabel), SF(15), true, pal_.text,
                  Gdiplus::StringAlignmentNear);
        static const wchar_t* const kModes[4] = {L"System", L"Light", L"Dark", L"Custom"};
        for (int i = 0; i < 4; ++i) {
            AddHit(a.mode[i], Act::ThemeMode, i);
            const bool selected = static_cast<int>(mode_) == i;
            const bool hot = IsHot(Act::ThemeMode, i);
            const Gdiplus::RectF r = FR(a.mode[i]);
            FillRound(g, r, r.Height / 2,
                      Col(selected ? pal_.accent : (hot ? pal_.cardHover : pal_.card)));
            if (!selected) StrokeRound(g, r, r.Height / 2, Col(pal_.line), 1.0f);
            DrawLabel(g, kModes[i], r, SF(14), true, selected ? pal_.onAccent : pal_.text);
        }

        DrawLabel(g, L"Customize colors", FR(a.customLabel), SF(15), true, pal_.text,
                  Gdiplus::StringAlignmentNear);
        DrawLabel(g, L"Changing a color switches the theme to Custom.", FR(a.customLabel),
                  SF(12), false, pal_.muted, Gdiplus::StringAlignmentFar);

        static const wchar_t* const kTabs[2] = {L"Background", L"Accent"};
        for (int i = 0; i < 2; ++i) {
            AddHit(a.tab[i], Act::ColorTab, i);
            const bool selected = (i == 0) == editBg_;
            const bool hot = IsHot(Act::ColorTab, i);
            const Gdiplus::RectF r = FR(a.tab[i]);
            FillRound(g, r, r.Height / 2,
                      Col(selected ? pal_.accent : (hot ? pal_.cardHover : pal_.card)));
            if (!selected) StrokeRound(g, r, r.Height / 2, Col(pal_.line), 1.0f);
            DrawLabel(g, kTabs[i], r, SF(14), true, selected ? pal_.onAccent : pal_.text);
        }

        // Preview + hex field.
        FillRound(g, FR(a.preview), SF(14), Col(current));
        StrokeRound(g, FR(a.preview), SF(14), Col(pal_.line), 1.5f);
        const Gdiplus::RectF field = FR(a.hexField);
        FillRound(g, field, field.Height / 2, Col(pal_.field));
        StrokeRound(g, field, field.Height / 2, Col(pal_.line), 1.0f);
        DrawLabel(g, L"Hex color",
                  Gdiplus::RectF(field.GetRight() + SF(14), field.Y, SF(200), field.Height),
                  SF(13), false, pal_.muted, Gdiplus::StringAlignmentNear);

        // RGB sliders.
        static const wchar_t* const kChannels[3] = {L"R", L"G", L"B"};
        const int comps[3] = {GetRValue(current), GetGValue(current), GetBValue(current)};
        for (int c = 0; c < 3; ++c) {
            AddHit(a.sliderHit[c], Act::Slider, c);
            DrawLabel(g, kChannels[c],
                      Gdiplus::RectF(static_cast<float>(S(252)), static_cast<float>(a.sliderHit[c].top),
                                     SF(24), static_cast<float>(a.sliderHit[c].bottom - a.sliderHit[c].top)),
                      SF(14), true, pal_.muted, Gdiplus::StringAlignmentNear);

            int lo[3] = {comps[0], comps[1], comps[2]};
            int hi[3] = {comps[0], comps[1], comps[2]};
            lo[c] = 0;
            hi[c] = 255;
            const Gdiplus::RectF track = FR(a.track[c]);
            {
                Gdiplus::GraphicsPath path;
                RoundPath(path, track, track.Height / 2);
                Gdiplus::LinearGradientBrush brush(
                    Gdiplus::PointF(track.X, 0), Gdiplus::PointF(track.GetRight(), 0),
                    Col(RGB(lo[0], lo[1], lo[2])), Col(RGB(hi[0], hi[1], hi[2])));
                g.FillPath(&brush, &path);
            }
            StrokeRound(g, track, track.Height / 2, Col(pal_.line), 1.0f);

            const float x = track.X + track.Width * static_cast<float>(comps[c]) / 255.0f;
            const float cy = track.Y + track.Height / 2;
            const float td = SF(22);
            FillCircle(g, Gdiplus::RectF(x - td / 2, cy - td / 2, td, td), Col(pal_.text));
            const float inner = td - SF(7);
            FillCircle(g, Gdiplus::RectF(x - inner / 2, cy - inner / 2, inner, inner), Col(current));

            DrawLabel(g, std::to_wstring(comps[c]), FR(a.value[c]), SF(14), false, pal_.muted,
                      Gdiplus::StringAlignmentNear);
        }

        // Presets.
        DrawLabel(g, L"Presets", FR(a.presetLabel), SF(13), true, pal_.muted,
                  Gdiplus::StringAlignmentNear);
        const COLORREF* presets = editBg_ ? kBackgroundPresets : kAccentPresets;
        for (int i = 0; i < 12; ++i) {
            AddHit(a.swatch[i], Act::Swatch, i);
            const Gdiplus::RectF r = FR(a.swatch[i]);
            FillCircle(g, r, Col(presets[i]));
            const bool selected = presets[i] == current;
            Gdiplus::Pen ring(Col(selected ? pal_.accent : pal_.line), selected ? SF(2.5f) : 1.0f);
            g.DrawEllipse(&ring, Gdiplus::RectF(r.X + (selected ? SF(-3) : 0), r.Y + (selected ? SF(-3) : 0),
                                                r.Width + (selected ? SF(6) : 0), r.Height + (selected ? SF(6) : 0)));
            if (IsHot(Act::Swatch, i) && !selected) {
                Gdiplus::Pen hover(Col(pal_.text, 160), 1.5f);
                g.DrawEllipse(&hover, Gdiplus::RectF(r.X - SF(3), r.Y - SF(3), r.Width + SF(6), r.Height + SF(6)));
            }
        }
    }

    // ---- floating menu and dialogs ---------------------------------
    void DrawMenu(Gdiplus::Graphics& g, int w, int h) {
        const std::vector<MenuDef> items = MenuItems();
        const int itemH = S(42), pad = S(8), mw = S(250);
        const int mh = static_cast<int>(items.size()) * itemH + pad * 2;
        int x = menuAnchor_.right - mw;
        int y = menuAnchor_.bottom + S(4);
        if (y + mh > h - S(8)) y = menuAnchor_.top - S(4) - mh;
        x = std::clamp(x, S(8), std::max(S(8), w - mw - S(8)));
        y = std::max(y, S(8));
        const RECT box{x, y, x + mw, y + mh};

        DrawShadow(g, box, SF(14));
        const COLORREF bg = pal_.dark ? Mix(pal_.card, RGB(255, 255, 255), 0.07) : RGB(255, 255, 255);
        FillRound(g, FR(box), SF(14), Col(bg));
        StrokeRound(g, FR(box), SF(14), Col(pal_.line), 1.0f);

        for (size_t i = 0; i < items.size(); ++i) {
            const int top = y + pad + static_cast<int>(i) * itemH;
            const RECT item{x + pad, top, x + mw - pad, top + itemH};
            AddHit(item, Act::MenuItem, items[i].id, nullptr, true);
            if (IsHot(Act::MenuItem, items[i].id)) FillRound(g, FR(item), SF(8), Col(pal_.cardHover));
            Gdiplus::RectF textRect = FR(item);
            textRect.X += SF(14);
            textRect.Width -= SF(28);
            DrawLabel(g, items[i].label, textRect, SF(14), false,
                      items[i].danger ? pal_.dangerText : pal_.text, Gdiplus::StringAlignmentNear);
        }
    }

    void DrawDialog(Gdiplus::Graphics& g, int w, int h) {
        {
            Gdiplus::SolidBrush dim(Gdiplus::Color(150, 0, 0, 0));
            g.FillRectangle(&dim, 0, 0, w, h);
        }
        const DialogLayout d = MakeDialogLayout();
        DrawShadow(g, d.box, SF(20));
        FillRound(g, FR(d.box), SF(20), Col(pal_.card));
        StrokeRound(g, FR(d.box), SF(20), Col(pal_.line), 1.0f);

        const std::wstring name = EntryValid(dlgEntry_) ? entries_[static_cast<size_t>(dlgEntry_)].name : L"";
        std::wstring title, body, primary;
        BtnStyle style = BtnStyle::Primary;
        switch (dlg_) {
        case Dlg::Create:
            title = L"Create profile";
            body = L"Give the new profile a name. It opens right away.";
            primary = L"Create";
            break;
        case Dlg::Rename:
            title = L"Rename profile";
            body = L"Choose a new name for this profile.";
            primary = L"Save";
            break;
        case Dlg::Delete:
            title = L"Delete \u201C" + name + L"\u201D?";
            body = L"This permanently deletes the profile and everything saved in it. "
                   L"Archive it instead if you might want it back.";
            primary = L"Delete";
            style = BtnStyle::DangerFilled;
            break;
        case Dlg::DeleteForever:
            title = L"Delete \u201C" + name + L"\u201D forever?";
            body = L"This permanently deletes the archived profile and all of its data. "
                   L"This can\u2019t be undone.";
            primary = L"Delete forever";
            style = BtnStyle::DangerFilled;
            break;
        case Dlg::Clear:
            title = L"Clear data for \u201C" + name + L"\u201D";
            body = L"Choose what to erase. This can\u2019t be undone.";
            primary = L"Clear data";
            style = BtnStyle::DangerFilled;
            break;
        case Dlg::None:
            return;
        }

        DrawLabel(g, title, FR(d.title), SF(21), true, pal_.text, Gdiplus::StringAlignmentNear);
        DrawParagraph(g, body, FR(d.body), SF(14), pal_.muted);

        if (dlg_ == Dlg::Create || dlg_ == Dlg::Rename) {
            const Gdiplus::RectF field = FR(d.field);
            FillRound(g, field, field.Height / 2, Col(pal_.field));
            StrokeRound(g, field, field.Height / 2, Col(pal_.accent), 1.5f);
        }

        if (dlg_ == Dlg::Clear) {
            for (int i = 0; i < kCategoryCount; ++i) {
                AddHit(d.check[i], Act::DlgCheck, i, nullptr, true);
                const bool hot = IsHot(Act::DlgCheck, i);
                if (hot) FillRound(g, FR(d.check[i]), SF(8), Col(pal_.cardHover));
                const float bs = SF(20);
                const Gdiplus::RectF box(static_cast<float>(d.check[i].left) + SF(8),
                                         static_cast<float>(d.check[i].top) +
                                             (static_cast<float>(d.check[i].bottom - d.check[i].top) - bs) / 2,
                                         bs, bs);
                if (dlgChecks_[i]) {
                    FillRound(g, box, SF(5), Col(pal_.accent));
                    Gdiplus::Pen tick(Col(pal_.onAccent), SF(2.2f));
                    tick.SetStartCap(Gdiplus::LineCapRound);
                    tick.SetEndCap(Gdiplus::LineCapRound);
                    tick.SetLineJoin(Gdiplus::LineJoinRound);
                    Gdiplus::PointF pts[3] = {{box.X + bs * 0.24f, box.Y + bs * 0.52f},
                                              {box.X + bs * 0.43f, box.Y + bs * 0.71f},
                                              {box.X + bs * 0.77f, box.Y + bs * 0.31f}};
                    g.DrawLines(&tick, pts, 3);
                } else {
                    StrokeRound(g, box, SF(5), Col(pal_.muted), SF(1.8f));
                }
                Gdiplus::RectF textRect = FR(d.check[i]);
                textRect.X += SF(8) + bs + SF(12);
                textRect.Width -= SF(8) + bs + SF(12);
                DrawLabel(g, kCategories[i].label, textRect, SF(15), false, pal_.text,
                          Gdiplus::StringAlignmentNear);
            }
        }

        const bool primaryEnabled = !(dlg_ == Dlg::Clear && !AnyChecked());
        if (primaryEnabled) AddHit(d.primary, Act::DlgPrimary, 0, nullptr, true);
        AddHit(d.secondary, Act::DlgSecondary, 0, nullptr, true);
        DrawPill(g, d.secondary, L"Cancel", BtnStyle::Outline, IsHot(Act::DlgSecondary, 0));
        DrawPill(g, d.primary, primary, style, IsHot(Act::DlgPrimary, 0), primaryEnabled);
    }

    // ---- input --------------------------------------------------------
    void EnsureVisible(int index) {
        const int pad = S(kTilePad), tileH = S(kTileH), gap = S(kTileGap);
        const int viewH = S(kGridBottom) - S(kGridTop);
        const int top = pad + (index / kColumns) * (tileH + gap);
        const int bottom = top + tileH + pad;
        if (top - pad < gridScroll_) gridScroll_ = top - pad;
        if (bottom > gridScroll_ + viewH) gridScroll_ = bottom - viewH;
    }

    void MoveFocus(int dx, int dy) {
        const int count = PickerItemCount();
        const int next = focus_ < 0 ? 0 : focus_ + dx + dy * kColumns;
        focus_ = std::clamp(next, 0, count - 1);
        keyboardFocus_ = true;
        EnsureVisible(focus_);
        Invalidate();
    }

    void SliderTo(int channel, int x) {
        const AppearanceLayout a = MakeAppearanceLayout();
        const RECT& t = a.track[channel];
        const double f = static_cast<double>(x - t.left) / static_cast<double>(t.right - t.left);
        const int value = std::clamp(static_cast<int>(std::lround(f * 255.0)), 0, 255);
        const COLORREF c = CurrentColor();
        int comps[3] = {GetRValue(c), GetGValue(c), GetBValue(c)};
        comps[channel] = value;
        SetCurrentColor(RGB(comps[0], comps[1], comps[2]), true);
    }

    void OnClick(POINT pt) {
        const HitReg* reg = HitAt(pt);
        const Act act = reg ? reg->act : Act::None;
        const int arg = reg ? reg->arg : 0;
        const RECT rect = reg ? reg->rc : RECT{};

        if (menuOpen_ && act != Act::MenuItem) { CloseMenu(); return; }

        switch (act) {
        case Act::None: break;
        case Act::Tile: Launch(arg); break;
        case Act::TileMenu: OpenMenu(arg, rect); break;
        case Act::AddTile: OpenDialog(Dlg::Create, -1); break;
        case Act::Gear: SetView(View::Settings); break;
        case Act::Back: SetView(View::Picker); break;
        case Act::Page: SetPage(static_cast<Page>(arg)); break;
        case Act::RowRename: OpenDialog(Dlg::Rename, arg); break;
        case Act::RowPicture: ChoosePicture(arg); break;
        case Act::RowArchive: SetArchived(arg, true); break;
        case Act::RowDelete: OpenDialog(Dlg::Delete, arg); break;
        case Act::RowClear: OpenDialog(Dlg::Clear, arg); break;
        case Act::RowUnarchive: SetArchived(arg, false); break;
        case Act::RowDeleteForever: OpenDialog(Dlg::DeleteForever, arg); break;
        case Act::ThemeMode:
            mode_ = static_cast<Mode>(arg);
            SaveSettings();
            RebuildPalette();
            break;
        case Act::ColorTab:
            editBg_ = arg == 0;
            SyncHexText();
            Invalidate();
            break;
        case Act::Slider:
            dragAct_ = Act::Slider;
            dragArg_ = arg;
            SetCapture(hwnd_);
            SliderTo(arg, pt.x);
            break;
        case Act::Swatch:
            SetCurrentColor(editBg_ ? kBackgroundPresets[arg] : kAccentPresets[arg], true);
            SaveSettings();
            break;
        case Act::MenuItem: DoMenuItem(arg); break;
        case Act::DlgPrimary: DialogPrimary(); break;
        case Act::DlgSecondary: CloseDialog(); break;
        case Act::DlgCheck:
            dlgChecks_[arg] = !dlgChecks_[arg];
            Invalidate();
            break;
        }
    }

    void OnKeyDown(WPARAM key) {
        if (dlg_ != Dlg::None) {
            if (key == VK_ESCAPE) CloseDialog();
            else if (key == VK_RETURN && dlg_ != Dlg::Create && dlg_ != Dlg::Rename) {
                if (!(dlg_ == Dlg::Clear && !AnyChecked())) DialogPrimary();
            }
            return;
        }
        if (menuOpen_) {
            if (key == VK_ESCAPE) CloseMenu();
            return;
        }
        if (view_ == View::Settings) {
            if (key == VK_ESCAPE) SetView(View::Picker);
            return;
        }
        switch (key) {
        case VK_LEFT:  MoveFocus(-1, 0); break;
        case VK_RIGHT: MoveFocus(1, 0);  break;
        case VK_UP:    MoveFocus(0, -1); break;
        case VK_DOWN:  MoveFocus(0, 1);  break;
        case VK_HOME:  focus_ = -1; MoveFocus(0, 0); break;
        case VK_END:   focus_ = PickerItemCount() - 1; MoveFocus(0, 0); break;
        case VK_RETURN:
        case VK_SPACE: {
            const std::vector<int> active = Indices(false);
            if (focus_ >= 0 && focus_ < static_cast<int>(active.size())) Launch(active[static_cast<size_t>(focus_)]);
            else if (focus_ == static_cast<int>(active.size())) OpenDialog(Dlg::Create, -1);
            break;
        }
        default: break;
        }
    }

    // ---- window procedure ---------------------------------------------
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        PickerWindow* self = reinterpret_cast<PickerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<PickerWindow*>(create->lpCreateParams);
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            if (self->dpiApi_.getDpi) self->dpi_ = static_cast<int>(self->dpiApi_.getDpi(hwnd));
            if (self->dpi_ <= 0) self->dpi_ = 96;
        }
        if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
        return self->HandleMessage(message, wParam, lParam);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            AddControls();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd_, &ps);
            Paint(dc);
            EndPaint(hwnd_, &ps);
            RefreshHot();
            return 0;
        }

        case 0x02E0: {  // WM_DPICHANGED
            dpi_ = static_cast<int>(HIWORD(wParam));
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            RecreateFonts();
            SyncControls();
            Invalidate();
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == kHexEditId && HIWORD(wParam) == EN_CHANGE && !updatingHex_) {
                COLORREF parsed = 0;
                if (ParseHex(EditText(hexEdit_), &parsed)) {
                    CurrentColor() = parsed;
                    mode_ = Mode::Custom;
                    SaveSettings();
                    RebuildPalette();
                }
                return 0;
            }
            break;

        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, pal_.text);
            SetBkColor(dc, pal_.field);
            return reinterpret_cast<LRESULT>(editBrush_);
        }

        case WM_MOUSEMOVE: {
            const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (!tracking_) {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
                if (TrackMouseEvent(&track)) tracking_ = true;
            }
            hoverActive_ = true;
            if (dragAct_ == Act::Slider) {
                SliderTo(dragArg_, pt.x);
                return 0;
            }
            if (view_ == View::Picker && dlg_ == Dlg::None && !menuOpen_) keyboardFocus_ = false;
            UpdateHot(pt);
            return 0;
        }

        case WM_MOUSELEAVE:
            tracking_ = false;
            hoverActive_ = false;
            hotAct_ = Act::None;
            hotArg_ = -1;
            Invalidate();
            return 0;

        case WM_MOUSEWHEEL: {
            if (dlg_ != Dlg::None) return 0;
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int step = (delta * S(70)) / WHEEL_DELTA;
            if (view_ == View::Picker) gridScroll_ -= step;
            else pageScroll_ -= step;
            CloseMenu();
            Invalidate();
            return 0;
        }

        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && hotAct_ != Act::None) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;

        case WM_LBUTTONDOWN:
            if (dlg_ != Dlg::Create && dlg_ != Dlg::Rename) SetFocus(hwnd_);
            OnClick(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;

        case WM_LBUTTONUP:
            if (dragAct_ != Act::None) {
                dragAct_ = Act::None;
                ReleaseCapture();
                SaveSettings();
            }
            return 0;

        case WM_CAPTURECHANGED:
            dragAct_ = Act::None;
            return 0;

        case WM_KEYDOWN:
            OnKeyDown(wParam);
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd_);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    // ---- state --------------------------------------------------------
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND nameEdit_ = nullptr;
    HWND hexEdit_ = nullptr;
    HFONT editFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;
    DpiApi dpiApi_{};
    int dpi_ = 96;

    Palette pal_{};
    Mode mode_ = Mode::System;
    COLORREF customBg_ = RGB(32, 33, 36);
    COLORREF customAccent_ = RGB(138, 180, 248);
    bool editBg_ = true;
    bool updatingHex_ = false;

    std::vector<Entry> entries_;
    std::optional<ProfileSelection> selected_;

    View view_ = View::Picker;
    Page page_ = Page::Profile;
    Dlg dlg_ = Dlg::None;
    int dlgEntry_ = -1;
    bool dlgChecks_[kCategoryCount]{};
    bool menuOpen_ = false;
    int menuEntry_ = -1;
    RECT menuAnchor_{};

    std::vector<HitReg> hits_;
    bool acceptHits_ = true;
    Act hotAct_ = Act::None;
    int hotArg_ = -1;
    bool hoverActive_ = false;
    bool tracking_ = false;
    Act dragAct_ = Act::None;
    int dragArg_ = 0;

    int focus_ = -1;
    bool keyboardFocus_ = false;
    int gridScroll_ = 0;
    int pageScroll_ = 0;
};

} // namespace

std::optional<ProfileSelection> ProfilePicker::Show(HINSTANCE instance) {
    EnsureGdiplus();   // process-wide GDI+ (shared with the rest of the app)
    PickerWindow picker(instance);
    return picker.Run();
}

} // namespace vibrium