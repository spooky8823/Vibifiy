// Vibrium's built-in settings page, drawn natively (Win32/GDI) like the tab
// bar and address bar. It is a child window of the main window that occupies
// the same area as a tab's browser view, so it behaves like a normal tab
// without involving CEF.
//
// Layout: a sidebar of sections on the left (currently only "Console") and the
// selected section on the right. To add a section, add an entry to kSections in
// settings_page.cc and a Paint/hit-test branch next to the console one.
#ifndef VIBRIUM_UI_SETTINGS_PAGE_H_
#define VIBRIUM_UI_SETTINGS_PAGE_H_

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace vibrium {

// What the address bar shows for the settings tab. Typing it opens the page.
inline constexpr wchar_t kSettingsUrl[] = L"vibrium://settings";

// True for "vibrium://settings" (case-insensitive), optionally followed by a
// path, query or fragment.
bool IsSettingsUrl(const std::wstring& url);

class SettingsPage {
 public:
  SettingsPage() = default;
  ~SettingsPage();

  SettingsPage(const SettingsPage&) = delete;
  SettingsPage& operator=(const SettingsPage&) = delete;

  bool Create(HWND parent, HINSTANCE instance);

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
  struct Row {
    uint64_t timestamp_ms = 0;
    int decision = 0;  // AdblockEngine::Decision, stored as int
    std::wstring time;
    std::wstring kind;
    std::wstring url;
    std::wstring source;
    std::wstring lower;  // lower-cased url + source + kind, for searching
  };

  struct Hit {
    RECT rect{};
    int id = 0;
  };

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

  void CreateResources();
  void Layout();
  void Paint(HDC hdc, const RECT& client);
  void PaintSidebar(HDC hdc);
  void PaintConsole(HDC hdc);
  void PaintLog(HDC hdc);

  void Refresh(bool force);
  void ApplyFilter();
  bool Matches(const Row& row) const;
  int ViewHeight() const;
  int MaxScroll() const;
  bool ThumbRect(RECT* rect) const;
  RECT RectOf(int id) const;
  void ClampScroll();
  void ScrollToBottom();
  void OnScrolled();
  int HitTest(int x, int y) const;
  int RowAt(int x, int y) const;
  void OnClick(int id);
  void InvalidateContent();
  void Flash(const std::wstring& message);

  std::wstring BuildText() const;
  void CopyToClipboard();
  void ExportToFile();

  HWND hwnd_ = nullptr;
  HWND parent_ = nullptr;
  HWND searchEdit_ = nullptr;
  HINSTANCE instance_ = nullptr;
  bool visible_ = false;

  HFONT fontUi_ = nullptr;
  HFONT fontSmall_ = nullptr;
  HFONT fontLabel_ = nullptr;
  HFONT fontTitle_ = nullptr;
  HFONT fontStat_ = nullptr;
  HFONT fontBrand_ = nullptr;
  HFONT fontMono_ = nullptr;
  HBRUSH editBrush_ = nullptr;

  // Layout (client coordinates), rebuilt by Layout().
  int width_ = 0;
  int height_ = 0;
  RECT sidebar_{};
  RECT title_{};
  RECT subtitle_{};
  RECT cards_[5]{};
  RECT notice_{};
  RECT searchBox_{};
  RECT chipsBox_{};
  RECT logRect_{};
  RECT footer_{};
  std::vector<Hit> hits_;
  std::wstring pauseLabel_;

  // Console state.
  std::vector<Row> rows_;
  std::vector<int> visible_rows_;  // indices into rows_ that pass the filter
  uint64_t blocked_ = 0;
  uint64_t allowed_ = 0;
  uint64_t errors_ = 0;
  size_t recorded_ = 0;
  bool enabled_ = false;
  bool paused_ = false;
  int filter_ = 0;  // 0 all, 1 blocked, 2 allowed, 3 errors
  std::wstring query_;
  uint64_t revision_ = ~0ULL;

  // Interaction state.
  int activeSection_ = 0;
  int hoverId_ = 0;
  int hoverRow_ = -1;
  int pressedId_ = 0;
  bool searchFocused_ = false;
  bool trackingMouse_ = false;
  bool stickToBottom_ = true;
  bool draggingThumb_ = false;
  int dragStartY_ = 0;
  int dragStartScroll_ = 0;
  int scrollY_ = 0;
  std::wstring flash_;
};

}  // namespace vibrium

#endif  // VIBRIUM_UI_SETTINGS_PAGE_H_
