// Vibrium's built-in settings page, drawn natively (Win32/GDI) like the tab
// bar and address bar. It is a child window of the main window that occupies
// the same area as a tab's browser view, so it behaves like a normal tab
// without involving CEF.
//
// This class is only the frame: the window, the sidebar, double-buffered
// painting and routing of mouse/keyboard input. Every page is its own
// SettingsSection in settings/ (Console, Ad blocker, Appearance, Updates).
//
// To add a page: write a SettingsSection (see settings/settings_section.h) and
// add one `AddSection(...)` line to SettingsPage::Create().
#ifndef VIBRIUM_UI_SETTINGS_PAGE_H_
#define VIBRIUM_UI_SETTINGS_PAGE_H_

#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "settings/settings_section.h"

namespace vibrium {

// What the address bar shows for the settings tab. Typing it opens the page.
inline constexpr wchar_t kSettingsUrl[] = L"vibrium://settings";

// True for "vibrium://settings" (case-insensitive), optionally followed by a
// path, query or fragment.
bool IsSettingsUrl(const std::wstring& url);

class SettingsPage final : private SettingsHost {
 public:
  SettingsPage() = default;
  ~SettingsPage();

  SettingsPage(const SettingsPage&) = delete;
  SettingsPage& operator=(const SettingsPage&) = delete;

  bool Create(HWND parent, HINSTANCE instance);
  void SetOnThemeChanged(std::function<void(COLORREF)> callback) { onThemeChanged_ = std::move(callback); }
  // Called after the user confirms "Update Vibrium Browser". The owner starts
  // the updater and closes the browser.
  void SetOnUpdateRequested(std::function<void()> callback) { onUpdateRequested_ = std::move(callback); }

  // Positions the page over `bounds` (parent client coordinates), shows it and
  // starts the live refresh timer.
  void Show(const RECT& bounds);
  // Hides the page and stops refreshing.
  void Hide();
  // Moves/resizes the page if it is currently shown.
  void Resize(const RECT& bounds);

  bool visible() const { return visible_; }
  HWND handle() const { return hwnd_; }

 private:
  // ---- SettingsHost (what the pages can ask of the window) ----
  HWND window() const override { return hwnd_; }
  HINSTANCE instance() const override { return instance_; }
  const SettingsFonts& fonts() const override { return fonts_; }
  HBRUSH edit_brush() const override { return editBrush_; }
  void Repaint(const RECT* rect) override;
  void StartFlashTimer() override;
  void NotifyAccentChanged(COLORREF color) override;
  void RepaintEverything() override;
  void RequestUpdate() override;

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

  // `icon` is a file in <exe folder>\icons (tinted to the row's text colour).
  void AddSection(std::unique_ptr<SettingsSection> section, const wchar_t* icon);
  SettingsSection* Active() const;
  void SelectSection(int index);
  RECT ContentArea() const;
  int SidebarHit(int x, int y) const;
  RECT SidebarItem(int index) const;

  void CreateResources();
  void Layout();
  void Paint(HDC hdc, const RECT& client);
  void PaintSidebar(HDC hdc);
  void EnsureBackBuffer(HDC hdc, int width, int height);
  void FreeBackBuffer();

  HWND hwnd_ = nullptr;
  HWND parent_ = nullptr;
  HINSTANCE instance_ = nullptr;
  bool visible_ = false;

  SettingsFonts fonts_;
  HBRUSH editBrush_ = nullptr;

  // Off-screen buffer reused between paints (recreated when the window grows).
  HBITMAP backBuffer_ = nullptr;
  int backWidth_ = 0;
  int backHeight_ = 0;

  int width_ = 0;
  int height_ = 0;

  std::vector<std::unique_ptr<SettingsSection>> sections_;
  std::vector<const wchar_t*> sectionIcons_;  // parallel to sections_
  int activeSection_ = 0;

  // Sidebar interaction.
  int sidebarHover_ = -1;
  int sidebarPressed_ = -1;
  bool trackingMouse_ = false;
  bool captured_ = false;  // a page started a drag and owns the mouse

  std::function<void(COLORREF)> onThemeChanged_;
  std::function<void()> onUpdateRequested_;
};

}  // namespace vibrium

#endif  // VIBRIUM_UI_SETTINGS_PAGE_H_