#include "native_page.h"

#include <algorithm>

#include "theme.h"

namespace vibrium {
namespace {

constexpr UINT_PTR kTickTimer = 1;
constexpr UINT kTickMs = 400;
constexpr int kWheelStep = 64;

// Signed mouse coordinates without windowsx.h (its macros collide with CEF).
inline int MouseX(LPARAM lp) { return static_cast<int>(static_cast<short>(LOWORD(lp))); }
inline int MouseY(LPARAM lp) { return static_cast<int>(static_cast<short>(HIWORD(lp))); }

HFONT MakeFont(int pixels, int weight, const wchar_t* face) {
  return CreateFontW(-pixels, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

}  // namespace

NativePage::~NativePage() {
  if (hwnd_) {
    // Stop routing messages to this (partly destroyed) object.
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
    if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  for (HFONT font : {fontUi_, fontBold_, fontSmall_, fontButton_, fontTitle_}) {
    if (font) DeleteObject(font);
  }
}

bool NativePage::Create(HWND parent, HINSTANCE instance,
                        const wchar_t* className) {
  parent_ = parent;
  instance_ = instance;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance_;
  wc.lpszClassName = className;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  hwnd_ = CreateWindowExW(0, className, L"",
                          WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0,
                          100, 100, parent_, nullptr, instance_, this);
  if (!hwnd_) return false;

  fontUi_ = MakeFont(15, FW_NORMAL, L"Segoe UI");
  fontBold_ = MakeFont(15, FW_SEMIBOLD, L"Segoe UI");
  fontSmall_ = MakeFont(12, FW_NORMAL, L"Segoe UI");
  fontButton_ = MakeFont(13, FW_SEMIBOLD, L"Segoe UI");
  fontTitle_ = MakeFont(28, FW_SEMIBOLD, L"Segoe UI");

  OnCreated();
  return true;
}

void NativePage::Show(const RECT& bounds) {
  if (!hwnd_) return;
  const bool wasVisible = visible_;
  MoveWindow(hwnd_, bounds.left, bounds.top, bounds.right - bounds.left,
             bounds.bottom - bounds.top, TRUE);
  ShowWindow(hwnd_, SW_SHOW);
  visible_ = true;
  SetTimer(hwnd_, kTickTimer, kTickMs, nullptr);
  if (!wasVisible) OnShown();
  InvalidateRect(hwnd_, nullptr, FALSE);
  SetFocus(hwnd_);
}

void NativePage::Hide() {
  if (!hwnd_) return;
  visible_ = false;
  KillTimer(hwnd_, kTickTimer);
  ShowWindow(hwnd_, SW_HIDE);
}

void NativePage::Resize(const RECT& bounds) {
  if (!hwnd_ || !visible_) return;
  MoveWindow(hwnd_, bounds.left, bounds.top, bounds.right - bounds.left,
             bounds.bottom - bounds.top, TRUE);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void NativePage::AddHit(const RECT& rect, int id) {
  hits_.push_back(Hit{rect, id});
}

int NativePage::HitTest(int x, int y) const {
  const POINT pt{x, y};
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (PtInRect(&it->rect, pt)) return it->id;
  }
  return 0;
}

void NativePage::Repaint() {
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void NativePage::ScrollToTop() {
  scrollY_ = 0;
  Repaint();
}

void NativePage::SetScroll(int contentHeight, int viewHeight,
                           const RECT& track) {
  contentHeight_ = contentHeight;
  viewHeight_ = viewHeight;
  track_ = track;
  ClampScroll();
}

int NativePage::MaxScroll() const {
  return std::max(0, contentHeight_ - viewHeight_);
}

void NativePage::ClampScroll() {
  scrollY_ = std::max(0, std::min(scrollY_, MaxScroll()));
}

bool NativePage::ThumbRect(RECT* rect) const {
  if (viewHeight_ <= 0 || contentHeight_ <= viewHeight_) return false;
  const int trackHeight = track_.bottom - track_.top;
  if (trackHeight <= 0) return false;
  const int thumbHeight =
      std::max(32, trackHeight * viewHeight_ / contentHeight_);
  const int range = std::max(1, trackHeight - thumbHeight);
  const int maxScroll = MaxScroll();
  const int top = track_.top + (maxScroll ? range * scrollY_ / maxScroll : 0);
  *rect = RECT{track_.left, top, track_.right, top + thumbHeight};
  return true;
}

void NativePage::PaintScrollbar(HDC hdc) const {
  RECT thumb{};
  if (!ThumbRect(&thumb)) return;
  const bool active = draggingThumb_ || (mouseX_ >= track_.left &&
                                         mouseX_ < track_.right);
  FillRound(hdc, thumb, thumb.right - thumb.left,
            active ? pageui::kMuted : pageui::kBorder,
            active ? pageui::kMuted : pageui::kBorder);
}

void NativePage::Fill(HDC hdc, const RECT& rect, COLORREF color) {
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(hdc, &rect, brush);
  DeleteObject(brush);
}

void NativePage::FillRound(HDC hdc, const RECT& rect, int radius,
                           COLORREF fill, COLORREF border) {
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = CreatePen(PS_SOLID, 1, border);
  HGDIOBJ oldBrush = SelectObject(hdc, brush);
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
  SelectObject(hdc, oldBrush);
  SelectObject(hdc, oldPen);
  DeleteObject(brush);
  DeleteObject(pen);
}

void NativePage::DrawLabel(HDC hdc, const std::wstring& text, RECT rect,
                           HFONT font, COLORREF color, UINT flags) const {
  HGDIOBJ oldFont = SelectObject(hdc, font);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, color);
  DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rect,
            flags | DT_NOPREFIX);
  SelectObject(hdc, oldFont);
}

void NativePage::DrawButton(HDC hdc, const RECT& rect,
                            const std::wstring& label, int hitId,
                            bool primary) {
  AddHit(rect, hitId);
  const bool hot = hoverId_ == hitId;
  const bool down = hot && pressedId_ == hitId;
  const COLORREF accent = Theme::Accent();

  COLORREF fill = primary ? (hot ? Theme::Mix(accent, RGB(255, 255, 255), 15)
                                 : accent)
                          : (hot ? pageui::kPanel3 : pageui::kPanel2);
  if (down) fill = Theme::Mix(fill, RGB(0, 0, 0), 20);

  FillRound(hdc, rect, rect.bottom - rect.top, fill,
            primary ? fill : pageui::kBorder);

  COLORREF textColor = pageui::kText;
  if (primary) {
    const int luminance = (GetRValue(accent) * 299 + GetGValue(accent) * 587 +
                           GetBValue(accent) * 114) / 1000;
    textColor = luminance > 150 ? RGB(0, 0, 0) : RGB(255, 255, 255);
  }
  DrawLabel(hdc, label, rect, fontButton_, textColor,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

LRESULT CALLBACK NativePage::WindowProc(HWND hwnd, UINT message, WPARAM wParam,
                                        LPARAM lParam) {
  NativePage* self = reinterpret_cast<NativePage*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<NativePage*>(create->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
  return self->HandleMessage(message, wParam, lParam);
}

LRESULT NativePage::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  LRESULT handled = 0;
  if (OnMessage(message, wParam, lParam, &handled)) return handled;

  switch (message) {
    case WM_ERASEBKGND:
      return 1;

    case WM_SIZE:
      OnResized(static_cast<int>(LOWORD(lParam)),
                static_cast<int>(HIWORD(lParam)));
      Repaint();
      return 0;

    case WM_PAINT: {
      PAINTSTRUCT ps{};
      HDC hdc = BeginPaint(hwnd_, &ps);
      RECT client{};
      GetClientRect(hwnd_, &client);
      const int width = client.right - client.left;
      const int height = client.bottom - client.top;
      if (width > 0 && height > 0) {
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
        if (mem && bitmap) {
          HGDIOBJ oldBitmap = SelectObject(mem, bitmap);
          hits_.clear();
          Fill(mem, client, pageui::kBg);
          SetBkMode(mem, TRANSPARENT);
          Paint(mem, client);
          BitBlt(hdc, 0, 0, width, height, mem, 0, 0, SRCCOPY);
          SelectObject(mem, oldBitmap);
        }
        if (bitmap) DeleteObject(bitmap);
        if (mem) DeleteDC(mem);
      }
      EndPaint(hwnd_, &ps);
      return 0;
    }

    case WM_TIMER:
      if (wParam == kTickTimer) {
        if (OnTick()) Repaint();
        return 0;
      }
      break;

    case WM_MOUSEWHEEL: {
      const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
      scrollY_ -= delta * kWheelStep / WHEEL_DELTA;
      ClampScroll();
      Repaint();
      return 0;
    }

    case WM_MOUSEMOVE: {
      mouseX_ = MouseX(lParam);
      mouseY_ = MouseY(lParam);
      if (draggingThumb_) {
        const int trackHeight = track_.bottom - track_.top;
        RECT thumb{};
        if (ThumbRect(&thumb)) {
          const int range =
              std::max(1, static_cast<int>(trackHeight - (thumb.bottom - thumb.top)));
          scrollY_ = dragStartScroll_ +
                     (mouseY_ - dragStartY_) * MaxScroll() / range;
          ClampScroll();
        }
      } else {
        hoverId_ = HitTest(mouseX_, mouseY_);
      }
      if (!trackingMouse_) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
        if (TrackMouseEvent(&track)) trackingMouse_ = true;
      }
      Repaint();
      return 0;
    }

    case WM_MOUSELEAVE:
      trackingMouse_ = false;
      mouseX_ = mouseY_ = -1;
      hoverId_ = 0;
      pressedId_ = 0;
      Repaint();
      return 0;

    case WM_SETCURSOR:
      if (LOWORD(lParam) == HTCLIENT && hoverId_ > 0) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
      break;

    case WM_LBUTTONDOWN: {
      SetFocus(hwnd_);
      const int x = MouseX(lParam);
      const int y = MouseY(lParam);
      RECT thumb{};
      const POINT pt{x, y};
      if (ThumbRect(&thumb) && PtInRect(&thumb, pt)) {
        draggingThumb_ = true;
        dragStartY_ = y;
        dragStartScroll_ = scrollY_;
        SetCapture(hwnd_);
      } else if (ThumbRect(&thumb) && PtInRect(&track_, pt)) {
        scrollY_ += (y < thumb.top ? -viewHeight_ : viewHeight_);
        ClampScroll();
      } else {
        pressedId_ = HitTest(x, y);
      }
      Repaint();
      return 0;
    }

    case WM_LBUTTONUP: {
      const int x = MouseX(lParam);
      const int y = MouseY(lParam);
      if (draggingThumb_) {
        draggingThumb_ = false;
        ReleaseCapture();
      } else {
        const int id = HitTest(x, y);
        const int pressed = pressedId_;
        pressedId_ = 0;
        if (id > 0 && id == pressed) OnClick(id, x, y);
      }
      Repaint();
      return 0;
    }

    case WM_CAPTURECHANGED:
      draggingThumb_ = false;
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wParam, lParam);
}

}  // namespace vibrium