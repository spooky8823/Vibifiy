// "Console" page: live ad-blocking decisions (stat cards, filter chips,
// search, and the scrolling event log).
#ifndef VIBRIUM_SETTINGS_CONSOLE_H_
#define VIBRIUM_SETTINGS_CONSOLE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "settings/settings_section.h"

namespace vibrium {

class ConsoleSection final : public SettingsSection {
 public:
  const wchar_t* Name() const override { return L"Console"; }
  void OnShow(bool page_opened) override;
  void OnHide() override;
  void OnTick() override { Refresh(false); }
  void Layout(const RECT& area) override;
  void Paint(HDC hdc) override;

  void MouseMove(int x, int y) override;
  void MouseLeave() override;
  bool MouseDown(int x, int y) override;
  void MouseUp(int x, int y) override;
  void CaptureLost() override;
  void MouseWheel(int notches) override;
  bool KeyDown(WPARAM key) override;
  bool Command(WPARAM wParam, LPARAM lParam) override;
  int CursorKind() const override;

 protected:
  void OnAttach() override;
  void OnClick(int id) override;

 private:
  struct Row {
    uint64_t timestamp_ms = 0;
    int decision = 0;  // 0 allow, 1 block, 2 error
    std::wstring time;
    std::wstring kind;
    std::wstring url;
    std::wstring source;
    std::wstring lower;  // lower-cased url + source + kind, for searching
  };

  void Refresh(bool force);
  void ApplyFilter();
  bool Matches(const Row& row) const;
  void OnScrolled();
  void PaintLog(HDC hdc);

  std::wstring BuildText() const;
  void CopyToClipboard();
  void ExportToFile();

  HWND searchEdit_ = nullptr;
  RECT area_{};

  // Layout (client coordinates), rebuilt by Layout().
  RECT title_{};
  RECT subtitle_{};
  RECT cards_[5]{};
  RECT notice_{};
  RECT searchBox_{};
  RECT chipsBox_{};
  settings::ScrollView log_;
  std::wstring pauseLabel_;

  // Console state.
  std::vector<Row> rows_;
  std::vector<int> visible_rows_;  // indices into rows_ that pass the filter
  uint64_t blocked_ = 0;
  uint64_t allowed_ = 0;
  uint64_t errors_ = 0;
  size_t recorded_ = 0;
  bool enabled_ = false;       // ad blocking is actually running
  bool listsMissing_ = false;  // no usable filter lists were loaded
  bool paused_ = false;
  int filter_ = 0;  // 0 all, 1 blocked, 2 allowed, 3 errors
  std::wstring query_;
  uint64_t revision_ = ~0ULL;

  // Interaction state.
  int hoverRow_ = -1;
  bool searchFocused_ = false;
  bool stickToBottom_ = true;
};

}  // namespace vibrium

#endif  // VIBRIUM_SETTINGS_CONSOLE_H_
