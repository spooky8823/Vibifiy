#include "settings_page.h"

#include <algorithm>

#include "settings/settings_adblock.h"
#include "settings/settings_appearance.h"
#include "settings/settings_common.h"
#include "settings/settings_console.h"
#include "settings/settings_updates.h"
#include "theme.h"

namespace vibrium {

using namespace settings;

namespace {

constexpr wchar_t kClassName[] = L"VibriumSettingsPage";

constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT_PTR kFlashTimer = 2;
constexpr UINT kRefreshMs = 500;
constexpr UINT kFlashMs = 3500;

}  // namespace

bool IsSettingsUrl(const std::wstring& url) {
  const size_t length = std::wstring(kSettingsUrl).size();
  if (url.size() < length) return false;
  if (ToLower(url.substr(0, length)) != kSettingsUrl) return false;
  if (url.size() == length) return true;
  const wchar_t next = url[length];
  return next == L'/' || next == L'#' || next == L'?';
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

SettingsPage::~SettingsPage() {
  if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
  sections_.clear();  // pages first: they were attached to this host
  for (HFONT font : {fonts_.ui, fonts_.small, fonts_.label, fonts_.title,
                     fonts_.stat, fonts_.brand, fonts_.mono}) {
    if (font) DeleteObject(font);
  }
  if (editBrush_) DeleteObject(editBrush_);
  FreeBackBuffer();
}

void SettingsPage::AddSection(std::unique_ptr<SettingsSection> section) {
  section->Attach(this);
  sections_.push_back(std::move(section));
}

bool SettingsPage::Create(HWND parent, HINSTANCE instance) {
  parent_ = parent;
  instance_ = instance;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance_;
  wc.lpszClassName = kClassName;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  hwnd_ = CreateWindowExW(0, kClassName, L"",
                          WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                          100, 100, parent_, nullptr, instance_, this);
  if (!hwnd_) return false;

  CreateResources();

  // Sidebar order. Add new pages here.
  AddSection(std::make_unique<ConsoleSection>());
  AddSection(std::make_unique<AdblockSection>());
  AddSection(std::make_unique<AppearanceSection>());
  AddSection(std::make_unique<UpdatesSection>());

  Layout();
  return true;
}

void SettingsPage::CreateResources() {
  fonts_.ui = MakeFont(15, FW_NORMAL, L"Segoe UI");
  fonts_.small = MakeFont(12, FW_SEMIBOLD, L"Segoe UI");
  fonts_.label = MakeFont(13, FW_NORMAL, L"Segoe UI");
  fonts_.title = MakeFont(28, FW_SEMIBOLD, L"Segoe UI");
  fonts_.stat = MakeFont(24, FW_SEMIBOLD, L"Segoe UI");
  fonts_.brand = MakeFont(18, FW_SEMIBOLD, L"Segoe UI");
  fonts_.mono = MakeFont(13, FW_NORMAL, L"Consolas");
  editBrush_ = CreateSolidBrush(kPanel2);
}

void SettingsPage::Show(const RECT& bounds) {
  if (!hwnd_) return;
  MoveWindow(hwnd_, bounds.left, bounds.top, bounds.right - bounds.left,
             bounds.bottom - bounds.top, TRUE);
  ShowWindow(hwnd_, SW_SHOW);
  visible_ = true;
  SetTimer(hwnd_, kRefreshTimer, kRefreshMs, nullptr);
  if (Active()) Active()->OnShow(/*page_opened=*/true);
  SetFocus(hwnd_);
}

void SettingsPage::Hide() {
  if (!hwnd_) return;
  visible_ = false;
  KillTimer(hwnd_, kRefreshTimer);
  KillTimer(hwnd_, kFlashTimer);
  if (Active()) Active()->OnHide();
  ShowWindow(hwnd_, SW_HIDE);
}

void SettingsPage::Resize(const RECT& bounds) {
  if (!hwnd_ || !visible_) return;
  MoveWindow(hwnd_, bounds.left, bounds.top, bounds.right - bounds.left,
             bounds.bottom - bounds.top, TRUE);
}

// ---------------------------------------------------------------------------
// Pages and sidebar
// ---------------------------------------------------------------------------

SettingsSection* SettingsPage::Active() const {
  if (activeSection_ < 0 || activeSection_ >= static_cast<int>(sections_.size())) {
    return nullptr;
  }
  return sections_[static_cast<size_t>(activeSection_)].get();
}

RECT SettingsPage::ContentArea() const {
  return RECT{kSidebarWidth, 0, width_, height_};
}

RECT SettingsPage::SidebarItem(int index) const {
  return RECT{14, 76 + index * 46, kSidebarWidth - 14, 76 + index * 46 + 42};
}

int SettingsPage::SidebarHit(int x, int y) const {
  for (int i = 0; i < static_cast<int>(sections_.size()); ++i) {
    if (InRect(SidebarItem(i), x, y)) return i;
  }
  return -1;
}

void SettingsPage::SelectSection(int index) {
  if (index < 0 || index >= static_cast<int>(sections_.size())) return;
  if (SettingsSection* old = Active()) old->OnHide();
  activeSection_ = index;
  Layout();
  if (SettingsSection* now = Active()) now->OnShow(/*page_opened=*/false);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsPage::Layout() {
  if (!hwnd_ || sections_.empty()) return;
  RECT client{};
  GetClientRect(hwnd_, &client);
  width_ = client.right;
  height_ = client.bottom;
  if (SettingsSection* section = Active()) section->Layout(ContentArea());
}

// ---------------------------------------------------------------------------
// SettingsHost
// ---------------------------------------------------------------------------

void SettingsPage::Repaint(const RECT* rect) {
  if (!hwnd_) return;
  if (rect) {
    InvalidateRect(hwnd_, rect, FALSE);
  } else {
    const RECT content = ContentArea();
    InvalidateRect(hwnd_, &content, FALSE);
  }
}

void SettingsPage::StartFlashTimer() {
  if (hwnd_) SetTimer(hwnd_, kFlashTimer, kFlashMs, nullptr);
}

void SettingsPage::NotifyAccentChanged(COLORREF color) {
  if (onThemeChanged_) onThemeChanged_(color);
}

void SettingsPage::RepaintEverything() {
  InvalidateRect(hwnd_, nullptr, FALSE);
  EnumChildWindows(
      GetParent(hwnd_),
      [](HWND child, LPARAM) -> BOOL {
        InvalidateRect(child, nullptr, TRUE);
        return TRUE;
      },
      0);
}

void SettingsPage::RequestUpdate() {
  if (onUpdateRequested_) onUpdateRequested_();
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void SettingsPage::Paint(HDC hdc, const RECT& client) {
  HBRUSH background = CreateSolidBrush(kBg);
  FillRect(hdc, &client, background);
  DeleteObject(background);

  PaintSidebar(hdc);
  if (SettingsSection* section = Active()) section->Paint(hdc);
}

void SettingsPage::PaintSidebar(HDC hdc) {
  const RECT sidebar{0, 0, kSidebarWidth, height_};
  HBRUSH panel = CreateSolidBrush(kPanel);
  FillRect(hdc, &sidebar, panel);
  DeleteObject(panel);

  HPEN border = CreatePen(PS_SOLID, 1, kBorder);
  HGDIOBJ oldPen = SelectObject(hdc, border);
  MoveToEx(hdc, kSidebarWidth - 1, 0, nullptr);
  LineTo(hdc, kSidebarWidth - 1, height_);
  SelectObject(hdc, oldPen);
  DeleteObject(border);

  FillRound(hdc, RECT{22, 26, 44, 48}, 8, Theme::Accent());
  DrawLabel(hdc, L"Settings", RECT{56, 22, kSidebarWidth - 14, 52}, fonts_.brand,
            kText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

  for (int i = 0; i < static_cast<int>(sections_.size()); ++i) {
    const RECT r = SidebarItem(i);
    const bool active = i == activeSection_;
    const bool hover = i == sidebarHover_;
    if (active) {
      FillRound(hdc, r, 12, Theme::Accent());
    } else if (hover) {
      FillRound(hdc, r, 12, kSoft);
    }
    const COLORREF color = active ? RGB(255, 255, 255) : hover ? kText : kMuted;
    DrawTerminalIcon(hdc, r.left + 14, r.top + 12, color);
    DrawLabel(hdc, sections_[static_cast<size_t>(i)]->Name(),
              RECT{r.left + 44, r.top, r.right - 8, r.bottom}, fonts_.ui, color,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  }
}

void SettingsPage::FreeBackBuffer() {
  if (backBuffer_) DeleteObject(backBuffer_);
  backBuffer_ = nullptr;
  backWidth_ = backHeight_ = 0;
}

// One off-screen bitmap is kept and reused; it only grows when the window
// does (the original created a full-window bitmap on every paint).
void SettingsPage::EnsureBackBuffer(HDC hdc, int width, int height) {
  if (backBuffer_ && backWidth_ >= width && backHeight_ >= height) return;
  FreeBackBuffer();
  backWidth_ = std::max(width, 1);
  backHeight_ = std::max(height, 1);
  backBuffer_ = CreateCompatibleBitmap(hdc, backWidth_, backHeight_);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK SettingsPage::WindowProc(HWND hwnd, UINT message,
                                          WPARAM wParam, LPARAM lParam) {
  SettingsPage* self = reinterpret_cast<SettingsPage*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<SettingsPage*>(create->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
  return self->HandleMessage(message, wParam, lParam);
}

LRESULT SettingsPage::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd_, &ps);
      RECT client{};
      GetClientRect(hwnd_, &client);
      if (client.right > 0 && client.bottom > 0) {
        EnsureBackBuffer(hdc, client.right, client.bottom);
        HDC memory = CreateCompatibleDC(hdc);
        HGDIOBJ oldBitmap = SelectObject(memory, backBuffer_);
        // Only the dirty part is drawn and copied.
        IntersectClipRect(memory, ps.rcPaint.left, ps.rcPaint.top,
                          ps.rcPaint.right, ps.rcPaint.bottom);
        Paint(memory, client);
        BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
               ps.rcPaint.right - ps.rcPaint.left,
               ps.rcPaint.bottom - ps.rcPaint.top, memory, ps.rcPaint.left,
               ps.rcPaint.top, SRCCOPY);
        SelectObject(memory, oldBitmap);
        DeleteDC(memory);
      }
      EndPaint(hwnd_, &ps);
      return 0;
    }

    case WM_DISPLAYCHANGE:
      FreeBackBuffer();  // colour format may have changed
      return 0;

    case WM_SIZE:
      Layout();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;

    case WM_TIMER:
      if (wParam == kRefreshTimer) {
        if (Active()) Active()->OnTick();
      } else if (wParam == kFlashTimer) {
        KillTimer(hwnd_, kFlashTimer);
        if (Active()) Active()->OnFlashExpired();
      }
      return 0;

    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      if (SettingsSection* section = Active()) {
        return reinterpret_cast<LRESULT>(
            section->CtlColorEdit(dc, reinterpret_cast<HWND>(lParam)));
      }
      SetTextColor(dc, kText);
      SetBkColor(dc, kPanel2);
      return reinterpret_cast<LRESULT>(editBrush_);
    }

    case WM_COMMAND:
      if (Active()) Active()->Command(wParam, lParam);
      return 0;

    case WM_MOUSEMOVE: {
      const int x = MouseX(lParam);
      const int y = MouseY(lParam);
      if (captured_) {  // a page is dragging its scrollbar
        if (Active()) Active()->MouseMove(x, y);
        return 0;
      }
      const int sidebar = SidebarHit(x, y);
      if (sidebar != sidebarHover_) {
        sidebarHover_ = sidebar;
        const RECT bar{0, 0, kSidebarWidth, height_};
        InvalidateRect(hwnd_, &bar, FALSE);
      }
      if (Active()) Active()->MouseMove(x, y);
      if (!trackingMouse_) {
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd_;
        if (TrackMouseEvent(&track)) trackingMouse_ = true;
      }
      return 0;
    }

    case WM_MOUSELEAVE:
      trackingMouse_ = false;
      if (sidebarHover_ != -1) {
        sidebarHover_ = -1;
        const RECT bar{0, 0, kSidebarWidth, height_};
        InvalidateRect(hwnd_, &bar, FALSE);
      }
      if (Active()) Active()->MouseLeave();
      return 0;

    case WM_SETCURSOR:
      if (LOWORD(lParam) == HTCLIENT) {
        const int kind = sidebarHover_ != -1 ? 1 : (Active() ? Active()->CursorKind() : 0);
        if (kind != 0) {
          SetCursor(LoadCursorW(nullptr, kind == 2 ? IDC_IBEAM : IDC_HAND));
          return TRUE;
        }
      }
      break;

    case WM_LBUTTONDOWN: {
      SetFocus(hwnd_);
      const int x = MouseX(lParam);
      const int y = MouseY(lParam);
      sidebarPressed_ = SidebarHit(x, y);
      if (sidebarPressed_ != -1) return 0;
      if (Active() && Active()->MouseDown(x, y)) {
        captured_ = true;
        SetCapture(hwnd_);
      }
      return 0;
    }

    case WM_LBUTTONUP: {
      const int x = MouseX(lParam);
      const int y = MouseY(lParam);
      if (sidebarPressed_ != -1) {
        const int pressed = sidebarPressed_;
        sidebarPressed_ = -1;
        if (SidebarHit(x, y) == pressed) SelectSection(pressed);
        return 0;
      }
      if (Active()) Active()->MouseUp(x, y);
      if (captured_) {
        captured_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
      }
      return 0;
    }

    case WM_CAPTURECHANGED:
      captured_ = false;
      if (Active()) Active()->CaptureLost();
      return 0;

    case WM_MOUSEWHEEL: {
      const int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
      if (Active()) Active()->MouseWheel(notches);
      return 0;
    }

    case WM_KEYDOWN:
      if (Active() && Active()->KeyDown(wParam)) return 0;
      return DefWindowProcW(hwnd_, message, wParam, lParam);

    case WM_NCDESTROY: {
      HWND old = hwnd_;
      SetWindowLongPtrW(old, GWLP_USERDATA, 0);
      hwnd_ = nullptr;
      return DefWindowProcW(old, message, wParam, lParam);
    }

    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wParam, lParam);
}

}  // namespace vibrium
