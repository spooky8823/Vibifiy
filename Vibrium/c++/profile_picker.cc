#include "profile_picker.h"
#include "theme.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <algorithm>
#include <cwctype>
#include <system_error>
#include <vector>
using std::min;
using std::max;
// GDI+ headers need IUnknown/IStream, which WIN32_LEAN_AND_MEAN strips out.
#include <unknwn.h>
#include <objidl.h>
#include <gdiplus.h>

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

namespace vibrium {
namespace {

constexpr wchar_t kPickerClass[] = L"VibriumProfilePicker";

// Client-area geometry (device pixels; the process is not DPI-aware, so
// Windows scales the whole window on high-DPI displays).
constexpr int kClientW = 840;
constexpr int kClientH = 600;
constexpr int kColumns = 4;
constexpr int kTileW = 180;
constexpr int kTileH = 168;
constexpr int kTileGap = 20;
constexpr int kTilePad = 4;       // room above the first row for outlines
constexpr int kGridTop = 148;
constexpr int kGridBottom = 512;
constexpr int kFooterTop = 528;
constexpr int kAvatar = 84;
constexpr int kPillH = 44;
constexpr int kMargin = 30;
constexpr int kNameEditId = 1004;

struct Palette {
    COLORREF bgTop, bgBottom, card, cardHover, text, muted, line;
    COLORREF accent, onAccent;
};
Palette g_pal{};

Palette MakePalette(bool dark) {
    Palette p{};
    if (dark) {
        p.bgTop = RGB(36, 45, 43);   p.bgBottom = RGB(32, 33, 36);
        p.card = RGB(48, 49, 52);    p.cardHover = RGB(60, 64, 67);
        p.text = RGB(232, 234, 237); p.muted = RGB(154, 160, 166);
        p.line = RGB(66, 69, 73);
        p.accent = RGB(138, 180, 248); p.onAccent = RGB(32, 33, 36);
    } else {
        p.bgTop = RGB(228, 238, 252); p.bgBottom = RGB(250, 251, 253);
        p.card = RGB(255, 255, 255);  p.cardHover = RGB(241, 243, 244);
        p.text = RGB(32, 33, 36);     p.muted = RGB(95, 99, 104);
        p.line = RGB(218, 220, 224);
        p.accent = RGB(26, 115, 232); p.onAccent = RGB(255, 255, 255);
    }
    return p;
}

// Avatar colors, picked deterministically from the profile name.
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

// ---- GDI+ helpers -----------------------------------------------------

Gdiplus::Color Col(COLORREF c, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

Gdiplus::RectF F(const RECT& r) {
    return Gdiplus::RectF(static_cast<float>(r.left), static_cast<float>(r.top),
                          static_cast<float>(r.right - r.left),
                          static_cast<float>(r.bottom - r.top));
}

void RoundPath(Gdiplus::GraphicsPath& path, Gdiplus::RectF r, float radius) {
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
                 Gdiplus::Color color, float width) {
    const float inset = width / 2.0f;
    r.X += inset; r.Y += inset; r.Width -= width; r.Height -= width;
    Gdiplus::GraphicsPath path;
    RoundPath(path, r, std::max(0.0f, radius - inset));
    Gdiplus::Pen pen(color, width);
    g.DrawPath(&pen, &path);
}

void DrawLabel(Gdiplus::Graphics& g, const std::wstring& text,
               Gdiplus::RectF rect, float pixelSize, bool bold,
               COLORREF color,
               Gdiplus::StringAlignment align = Gdiplus::StringAlignmentCenter) {
    Gdiplus::FontFamily family(L"Segoe UI");
    Gdiplus::Font font(&family, pixelSize,
                       bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);
    Gdiplus::StringFormat format(Gdiplus::StringFormatFlagsNoWrap);
    format.SetAlignment(align);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::SolidBrush brush(Col(color));
    g.DrawString(text.c_str(), -1, &font, rect, &format, &brush);
}

// ---- profile storage --------------------------------------------------

std::filesystem::path ProfilesRoot() {
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return {};
    executable.resize(length);
    return std::filesystem::path(executable).parent_path() / L"profiles";
}

bool IsValidProfileName(const std::wstring& name) {
    if (name.empty() || name.size() > 64 || name == L"." ||
        name == L"..") {
        return false;
    }
    if (name.back() == L'.' || name.back() == L' ') return false;
    for (wchar_t ch : name) {
        if (ch < 32 || ch == L'<' || ch == L'>' || ch == L':' ||
            ch == L'"' || ch == L'/' || ch == L'\\' ||
            ch == L'|' || ch == L'?' || ch == L'*') {
            return false;
        }
    }

    std::wstring base = name;
    const auto dot = base.find(L'.');
    if (dot != std::wstring::npos) base.resize(dot);
    std::transform(base.begin(), base.end(), base.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    if (base == L"CON" || base == L"PRN" || base == L"AUX" ||
        base == L"NUL") {
        return false;
    }
    if (base.size() == 4 &&
        (base.compare(0, 3, L"COM") == 0 ||
         base.compare(0, 3, L"LPT") == 0) &&
        base[3] >= L'1' && base[3] <= L'9') {
        return false;
    }
    return true;
}

} // namespace

ProfilePicker::ProfilePicker(HINSTANCE instance) : instance_(instance) {}

ProfilePicker::~ProfilePicker() {
    if (editFont_) DeleteObject(editFont_);
    if (editBrush_) DeleteObject(editBrush_);
}

std::optional<ProfileSelection> ProfilePicker::Show(HINSTANCE instance) {
    Gdiplus::GdiplusStartupInput startup;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) {
        return std::nullopt;
    }

    std::optional<ProfileSelection> result;
    {
        ProfilePicker picker(instance);
        if (picker.Create()) {
            ShowWindow(picker.hwnd_, SW_SHOW);
            UpdateWindow(picker.hwnd_);

            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                // Enter / Esc inside the name field.
                if (message.message == WM_KEYDOWN && picker.creating_ &&
                    message.hwnd == picker.nameEdit_) {
                    if (message.wParam == VK_RETURN) {
                        picker.CreateProfileFromInput();
                        continue;
                    }
                    if (message.wParam == VK_ESCAPE) {
                        picker.EndCreate();
                        continue;
                    }
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            result = picker.selected_;
        }
    }
    Gdiplus::GdiplusShutdown(token);
    return result;
}

bool ProfilePicker::Create() {
    const bool dark = Theme::IsSystemDark();
    g_pal = MakePalette(dark);

    const std::filesystem::path root = ProfilesRoot();
    if (root.empty()) return false;

    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) {
        MessageBoxW(nullptr, L"Could not create the profiles folder.",
                    L"Vibrium Profiles", MB_OK | MB_ICONERROR);
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ProfilePicker::WindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPickerClass;
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_APPWINDOW;
    RECT frame{0, 0, kClientW, kClientH};
    AdjustWindowRectEx(&frame, style, FALSE, exStyle);
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;

    hwnd_ = CreateWindowExW(exStyle, kPickerClass, L"Vibrium", style,
                            x, y, width, height, nullptr, nullptr,
                            instance_, this);
    if (hwnd_) {
        const BOOL useDark = dark ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                              &useDark, sizeof(useDark));
        const COLORREF caption = g_pal.bgTop;
        DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        const COLORREF captionText = g_pal.text;
        DwmSetWindowAttribute(hwnd_, DWMWA_TEXT_COLOR, &captionText, sizeof(captionText));
    }
    return hwnd_ != nullptr;
}

void ProfilePicker::AddControls() {
    nameEdit_ = CreateWindowExW(
        0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNameEditId)),
        instance_, nullptr);
    editFont_ = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    editBrush_ = CreateSolidBrush(g_pal.card);
    SendMessageW(nameEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
    SendMessageW(nameEdit_, EM_LIMITTEXT, 64, 0);
    SendMessageW(nameEdit_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Profile name"));

    PopulateProfiles();
    ComputeLayout();
    if (profiles_.empty()) {
        StartCreate();            // nothing to pick yet: ask for a name
    } else {
        focus_ = 0;
    }
}

void ProfilePicker::SortProfiles() {
    const auto lower = [](std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return s;
    };
    std::sort(profiles_.begin(), profiles_.end(),
              [&](const ProfileSelection& a, const ProfileSelection& b) {
                  return lower(a.name) < lower(b.name);
              });
}

void ProfilePicker::PopulateProfiles() {
    profiles_.clear();
    const auto root = ProfilesRoot();
    std::error_code error;
    for (std::filesystem::directory_iterator it(root, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_directory(error) || error) continue;
        ProfileSelection profile;
        profile.name = it->path().filename().wstring();
        profile.path = it->path();
        profiles_.push_back(std::move(profile));
    }
    SortProfiles();
}

// ---- layout / hit testing --------------------------------------------

void ProfilePicker::ClampScroll() {
    const int count = static_cast<int>(profiles_.size());
    const int rows = (count + kColumns - 1) / kColumns;
    const int content = rows > 0
        ? rows * kTileH + (rows - 1) * kTileGap + kTilePad * 2 : 0;
    const int maxScroll = std::max(0, content - (kGridBottom - kGridTop));
    scrollY_ = std::clamp(scrollY_, 0, maxScroll);
}

void ProfilePicker::ComputeLayout() {
    ClampScroll();
    const int count = static_cast<int>(profiles_.size());
    tileRects_.assign(static_cast<size_t>(count), RECT{});
    for (int i = 0; i < count; ++i) {
        const int row = i / kColumns;
        const int col = i % kColumns;
        const int inRow = std::min(kColumns, count - row * kColumns);
        const int rowWidth = inRow * kTileW + (inRow - 1) * kTileGap;
        const int x = (kClientW - rowWidth) / 2 + col * (kTileW + kTileGap);
        const int y = kGridTop + kTilePad + row * (kTileH + kTileGap) - scrollY_;
        tileRects_[static_cast<size_t>(i)] = RECT{x, y, x + kTileW, y + kTileH};
    }

    const int py = kFooterTop + (kClientH - kFooterTop - kPillH) / 2;
    addRect_ = RECT{kMargin, py, kMargin + 160, py + kPillH};
    editRect_ = RECT{kMargin, py, kMargin + 320, py + kPillH};
    createRect_ = RECT{editRect_.right + 12, py, editRect_.right + 12 + 96, py + kPillH};
    cancelRect_ = RECT{createRect_.right + 8, py, createRect_.right + 8 + 96, py + kPillH};
}

ProfilePicker::Hit ProfilePicker::HitTest(POINT pt, int* tileIndex) const {
    if (tileIndex) *tileIndex = -1;
    if (creating_) {
        if (PtInRect(&createRect_, pt)) return Hit::Create;
        if (PtInRect(&cancelRect_, pt)) return Hit::Cancel;
    } else if (PtInRect(&addRect_, pt)) {
        return Hit::Add;
    }
    if (pt.y >= kGridTop && pt.y < kGridBottom) {
        for (size_t i = 0; i < tileRects_.size(); ++i) {
            if (PtInRect(&tileRects_[i], pt)) {
                if (tileIndex) *tileIndex = static_cast<int>(i);
                return Hit::Tile;
            }
        }
    }
    return Hit::None;
}

void ProfilePicker::UpdateHover(POINT pt) {
    int tile = -1;
    const Hit hit = HitTest(pt, &tile);
    const Hit button = hit == Hit::Tile ? Hit::None : hit;
    if (tile != hover_ || button != hoverButton_) {
        hover_ = tile;
        hoverButton_ = button;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void ProfilePicker::EnsureVisible(int index) {
    if (index < 0 || static_cast<size_t>(index) >= profiles_.size()) return;
    const int viewport = kGridBottom - kGridTop;
    const int top = kTilePad + (index / kColumns) * (kTileH + kTileGap);
    const int bottom = top + kTileH + kTilePad;
    if (top - kTilePad < scrollY_) scrollY_ = top - kTilePad;
    if (bottom > scrollY_ + viewport) scrollY_ = bottom - viewport;
    ComputeLayout();
}

void ProfilePicker::MoveFocus(int dx, int dy) {
    const int count = static_cast<int>(profiles_.size());
    if (count == 0) return;
    int next = focus_ < 0 ? 0 : focus_ + dx + dy * kColumns;
    next = std::clamp(next, 0, count - 1);
    focus_ = next;
    keyboardFocus_ = true;
    EnsureVisible(focus_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- actions ----------------------------------------------------------

void ProfilePicker::StartCreate() {
    creating_ = true;
    hoverButton_ = Hit::None;
    ComputeLayout();
    MoveWindow(nameEdit_, editRect_.left + 18, editRect_.top + (kPillH - 22) / 2,
               (editRect_.right - editRect_.left) - 36, 22, TRUE);
    ShowWindow(nameEdit_, SW_SHOW);
    SetFocus(nameEdit_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ProfilePicker::EndCreate() {
    creating_ = false;
    SetWindowTextW(nameEdit_, L"");
    ShowWindow(nameEdit_, SW_HIDE);
    SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ProfilePicker::CreateProfileFromInput() {
    const int length = GetWindowTextLengthW(nameEdit_);
    if (length <= 0 || length > 64) {
        MessageBoxW(hwnd_, L"Enter a profile name (1 to 64 characters).",
                    L"Vibrium Profiles", MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring name(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(nameEdit_, name.data(), length + 1);
    name.resize(static_cast<size_t>(length));

    if (!IsValidProfileName(name)) {
        MessageBoxW(hwnd_,
            L"That name contains characters Windows cannot use in a folder.",
            L"Vibrium Profiles", MB_OK | MB_ICONWARNING);
        return;
    }

    const auto path = ProfilesRoot() / name;
    std::error_code error;
    if (!std::filesystem::create_directory(path, error)) {
        MessageBoxW(hwnd_,
            error ? L"Could not create the profile folder."
                  : L"A profile with that name already exists.",
            L"Vibrium Profiles", MB_OK | MB_ICONWARNING);
        return;
    }

    profiles_.push_back(ProfileSelection{name, path});
    SortProfiles();
    for (size_t i = 0; i < profiles_.size(); ++i) {
        if (profiles_[i].path == path) focus_ = static_cast<int>(i);
    }
    keyboardFocus_ = true;
    EndCreate();
    EnsureVisible(focus_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ProfilePicker::FinishSelection(int index) {
    if (index < 0 || static_cast<size_t>(index) >= profiles_.size()) return;
    selected_ = profiles_[static_cast<size_t>(index)];
    DestroyWindow(hwnd_);
}

// ---- painting ---------------------------------------------------------

void ProfilePicker::Paint(HDC target) {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int w = rc.right;
    const int h = rc.bottom;

    HDC mem = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, w, h);
    HGDIOBJ oldBitmap = SelectObject(mem, bitmap);
    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        // Background gradient.
        {
            Gdiplus::LinearGradientBrush bg(
                Gdiplus::Point(0, 0), Gdiplus::Point(0, h),
                Col(g_pal.bgTop), Col(g_pal.bgBottom));
            g.FillRectangle(&bg, 0, 0, w, h);
        }

        // Header.
        DrawLabel(g, L"Who\u2019s using Vibrium?",
                  Gdiplus::RectF(0, 38, static_cast<float>(w), 48),
                  32.0f, true, g_pal.text);
        DrawLabel(g, L"Choose a profile to continue, or add a new one.",
                  Gdiplus::RectF(0, 92, static_cast<float>(w), 28),
                  15.0f, false, g_pal.muted);

        // Tiles (clipped to the scrolling viewport).
        const int count = static_cast<int>(profiles_.size());
        g.SetClip(Gdiplus::RectF(0, static_cast<float>(kGridTop),
                                 static_cast<float>(w),
                                 static_cast<float>(kGridBottom - kGridTop)));
        for (int i = 0; i < count; ++i) {
            const RECT& r = tileRects_[static_cast<size_t>(i)];
            if (r.bottom < kGridTop || r.top > kGridBottom) continue;

            const bool hovered = i == hover_;
            const bool focused = keyboardFocus_ && i == focus_;
            const Gdiplus::RectF rect = F(r);
            FillRound(g, rect, 16.0f,
                      Col(hovered || focused ? g_pal.cardHover : g_pal.card));
            StrokeRound(g, rect, 16.0f, Col(g_pal.line), 1.0f);
            if (focused) {
                StrokeRound(g, rect, 16.0f, Col(g_pal.accent), 2.0f);
            }

            const std::wstring& name = profiles_[static_cast<size_t>(i)].name;
            const float cx = rect.X + rect.Width / 2.0f;
            const float top = rect.Y + 26.0f;
            const Gdiplus::RectF circle(cx - kAvatar / 2.0f, top,
                                        static_cast<float>(kAvatar),
                                        static_cast<float>(kAvatar));
            {
                Gdiplus::SolidBrush brush(Col(AvatarColor(name)));
                g.FillEllipse(&brush, circle);
            }
            const std::wstring initial(
                1, name.empty() ? L'?' : static_cast<wchar_t>(std::towupper(name[0])));
            DrawLabel(g, initial, circle, 38.0f, true, RGB(255, 255, 255));

            DrawLabel(g, name,
                      Gdiplus::RectF(rect.X + 12.0f, top + kAvatar + 12.0f,
                                     rect.Width - 24.0f, 30.0f),
                      16.0f, true, g_pal.text);
        }

        if (count == 0) {
            DrawLabel(g, L"No profiles yet",
                      Gdiplus::RectF(0, static_cast<float>(kGridTop + 120),
                                     static_cast<float>(w), 32),
                      22.0f, true, g_pal.text);
            DrawLabel(g, L"Name your first profile below to get started.",
                      Gdiplus::RectF(0, static_cast<float>(kGridTop + 154),
                                     static_cast<float>(w), 24),
                      14.0f, false, g_pal.muted);
        }

        // Scroll indicator.
        const int rows = (count + kColumns - 1) / kColumns;
        const int content = rows * kTileH + (rows - 1) * kTileGap + kTilePad * 2;
        const int viewport = kGridBottom - kGridTop;
        if (rows > 0 && content > viewport) {
            const float trackH = static_cast<float>(viewport);
            const float thumbH = std::max(32.0f, trackH * viewport / content);
            const float range = static_cast<float>(content - viewport);
            const float thumbY = static_cast<float>(kGridTop) +
                (trackH - thumbH) * (scrollY_ / range);
            FillRound(g, Gdiplus::RectF(static_cast<float>(w - 14), thumbY,
                                        5.0f, thumbH),
                      2.5f, Col(g_pal.muted, 140));
        }
        g.ResetClip();

        // Footer.
        {
            Gdiplus::Pen line(Col(g_pal.line), 1.0f);
            g.DrawLine(&line, 0, kFooterTop, w, kFooterTop);
        }

        if (!creating_) {
            const bool hovered = hoverButton_ == Hit::Add;
            const Gdiplus::RectF pill = F(addRect_);
            if (hovered) FillRound(g, pill, kPillH / 2.0f, Col(g_pal.accent, 30));
            StrokeRound(g, pill, kPillH / 2.0f, Col(g_pal.accent), 1.0f);
            DrawLabel(g, L"+  Add profile", pill, 15.0f, true, g_pal.accent);

            DrawLabel(g, L"Each profile keeps its own bookmarks, history and settings.",
                      Gdiplus::RectF(static_cast<float>(addRect_.right + 24),
                                     static_cast<float>(addRect_.top),
                                     static_cast<float>(w - kMargin - addRect_.right - 24),
                                     static_cast<float>(kPillH)),
                      13.0f, false, g_pal.muted, Gdiplus::StringAlignmentFar);
        } else {
            const Gdiplus::RectF field = F(editRect_);
            FillRound(g, field, kPillH / 2.0f, Col(g_pal.card));
            StrokeRound(g, field, kPillH / 2.0f, Col(g_pal.accent), 1.5f);

            const Gdiplus::RectF create = F(createRect_);
            FillRound(g, create, kPillH / 2.0f,
                      Col(g_pal.accent, hoverButton_ == Hit::Create ? 220 : 255));
            DrawLabel(g, L"Create", create, 15.0f, true, g_pal.onAccent);

            const Gdiplus::RectF cancel = F(cancelRect_);
            if (hoverButton_ == Hit::Cancel) {
                FillRound(g, cancel, kPillH / 2.0f, Col(g_pal.cardHover));
            }
            DrawLabel(g, L"Cancel", cancel, 15.0f, true, g_pal.muted);
        }
    }
    BitBlt(target, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
}

// ---- window procedure -------------------------------------------------

LRESULT CALLBACK ProfilePicker::WindowProc(HWND hwnd, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
    ProfilePicker* self = reinterpret_cast<ProfilePicker*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<ProfilePicker*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }

    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_CREATE:
        self->AddControls();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        self->Paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, g_pal.text);
        SetBkColor(dc, g_pal.card);
        return reinterpret_cast<LRESULT>(self->editBrush_);
    }

    case WM_MOUSEMOVE: {
        if (!self->tracking_) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            if (TrackMouseEvent(&track)) self->tracking_ = true;
        }
        self->keyboardFocus_ = false;
        self->UpdateHover(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        return 0;
    }

    case WM_MOUSELEAVE:
        self->tracking_ = false;
        self->hover_ = -1;
        self->hoverButton_ = Hit::None;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        self->scrollY_ -= (delta * 60) / WHEEL_DELTA;
        self->ComputeLayout();
        POINT pt{};
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        self->UpdateHover(pt);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (self->HitTest(pt, nullptr) != Hit::None) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        break;

    case WM_LBUTTONDOWN: {
        int tile = -1;
        const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        switch (self->HitTest(pt, &tile)) {
        case Hit::Tile:   self->FinishSelection(tile); break;
        case Hit::Add:    self->StartCreate(); break;
        case Hit::Create: self->CreateProfileFromInput(); break;
        case Hit::Cancel: self->EndCreate(); break;
        case Hit::None:   break;
        }
        return 0;
    }

    case WM_KEYDOWN:
        switch (wParam) {
        case VK_LEFT:  self->MoveFocus(-1, 0); return 0;
        case VK_RIGHT: self->MoveFocus(1, 0);  return 0;
        case VK_UP:    self->MoveFocus(0, -1); return 0;
        case VK_DOWN:  self->MoveFocus(0, 1);  return 0;
        case VK_HOME:  self->focus_ = -1; self->MoveFocus(0, 0); return 0;
        case VK_END:
            self->focus_ = static_cast<int>(self->profiles_.size()) - 1;
            self->MoveFocus(0, 0);
            return 0;
        case VK_RETURN:
        case VK_SPACE:
            self->FinishSelection(self->focus_);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace vibrium