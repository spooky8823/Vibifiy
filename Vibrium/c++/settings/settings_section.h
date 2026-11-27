// The contract between the settings window (SettingsPage) and its pages.
//
// SettingsPage owns the window, the sidebar, double-buffered painting and
// mouse/keyboard routing. Each page (Console, Ad blocker, Appearance,
// Updates) is a SettingsSection: it lays itself out in the area to the right
// of the sidebar, paints into the window's off-screen DC and reacts to input
// that lands in that area.
//
// To add a page: derive from SettingsSection, implement Name/Layout/Paint
// (plus OnClick for buttons), then add one line to SettingsPage::Create().
#ifndef VIBRIUM_SETTINGS_SECTION_H_
#define VIBRIUM_SETTINGS_SECTION_H_

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

#include "settings/settings_common.h"

namespace vibrium {

struct SettingsFonts {
  HFONT ui = nullptr;     // 15px
  HFONT small = nullptr;  // 12px semibold
  HFONT label = nullptr;  // 13px
  HFONT title = nullptr;  // 28px semibold
  HFONT stat = nullptr;   // 24px semibold
  HFONT brand = nullptr;  // 18px semibold
  HFONT mono = nullptr;   // 13px Consolas
};

// What the settings window offers its pages.
class SettingsHost {
 public:
  virtual ~SettingsHost() = default;
  virtual HWND window() const = 0;
  virtual HINSTANCE instance() const = 0;
  virtual const SettingsFonts& fonts() const = 0;
  virtual HBRUSH edit_brush() const = 0;
  // Repaints `rect` (window client coordinates); null = everything right of
  // the sidebar.
  virtual void Repaint(const RECT* rect) = 0;
  // Starts the one-shot timer that ends a page's Flash() message.
  virtual void StartFlashTimer() = 0;
  // Tells the main window the accent colour changed.
  virtual void NotifyAccentChanged(COLORREF color) = 0;
  // Repaints the settings window and every sibling window (after an accent
  // change, so the tab strip and address bar pick the new colour up).
  virtual void RepaintEverything() = 0;
  // Asks the main window to run the updater (after the user confirmed).
  virtual void RequestUpdate() = 0;
};

class SettingsSection {
 public:
  virtual ~SettingsSection() = default;

  void Attach(SettingsHost* host) {
    host_ = host;
    OnAttach();
  }

  virtual const wchar_t* Name() const = 0;

  // The page became visible: the settings tab was opened (page_opened) or
  // this page was picked in the sidebar.
  virtual void OnShow(bool /*page_opened*/) {}
  // Another page was picked, or the settings tab was closed.
  virtual void OnHide() {}
  // About twice a second while visible. Repaint only what changed.
  virtual void OnTick() {}
  // The Flash() message timed out.
  virtual void OnFlashExpired();

  // `area` is the part of the window right of the sidebar, in client
  // coordinates. Called when the page is shown and on every resize.
  virtual void Layout(const RECT& area) = 0;
  virtual void Paint(HDC hdc) = 0;

  // ---- input (window client coordinates) -----------------------------------
  virtual void MouseMove(int x, int y);
  virtual void MouseLeave();
  // Return true to capture the mouse (a drag started).
  virtual bool MouseDown(int x, int y);
  virtual void MouseUp(int x, int y);
  virtual void CaptureLost() {}
  virtual void MouseWheel(int /*notches*/) {}
  virtual bool KeyDown(WPARAM /*virtualKey*/) { return false; }
  // WM_COMMAND from a child control of this page.
  virtual bool Command(WPARAM /*wParam*/, LPARAM /*lParam*/) { return false; }
  // WM_CTLCOLOREDIT for an edit control of this page.
  virtual HBRUSH CtlColorEdit(HDC dc, HWND edit);
  // 0 = arrow, 1 = hand, 2 = text cursor.
  virtual int CursorKind() const { return hoverId_ != 0 ? 1 : 0; }

 protected:
  virtual void OnAttach() {}
  virtual void OnClick(int /*hitId*/) {}

  void Flash(const std::wstring& message);
  void RepaintContent() { host_->Repaint(nullptr); }
  void RepaintRect(const RECT& rect) { host_->Repaint(&rect); }
  const SettingsFonts& fonts() const { return host_->fonts(); }

  SettingsHost* host_ = nullptr;
  settings::HitMap hits_;
  int hoverId_ = 0;
  int pressedId_ = 0;
  int width_ = 0;   // window client size, as of the last Layout()
  int height_ = 0;
  RECT footer_{};   // where Flash() messages are drawn; set by Layout()
  std::wstring flash_;
};

}  // namespace vibrium

#endif  // VIBRIUM_SETTINGS_SECTION_H_
