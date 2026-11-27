// "Ad blocker" page.
//
// Five tabs, picked with the same pill chips the console uses:
//   Overview      master on/off switch, cosmetic/scriptlet switches, lifetime
//                 and live-session counters
//   Filter lists  "Update now", and a switch per list
//   Custom rules  the user's own filters (validated before saving), import
//   Allowlist     "don't block on these sites"
//   Sessions      every browser session with what was blocked in it
//
// All state lives in AdblockService (policy, history, lists); this class only
// shows it and forwards the user's choices.
#ifndef VIBRIUM_SETTINGS_ADBLOCK_H_
#define VIBRIUM_SETTINGS_ADBLOCK_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "adblock/adblock_service.h"
#include "settings/settings_section.h"

namespace vibrium {

class AdblockSection final : public SettingsSection {
 public:
  AdblockSection();
  ~AdblockSection() override;

  const wchar_t* Name() const override { return L"Ad blocker"; }
  void OnShow(bool page_opened) override;
  void OnHide() override;
  void OnTick() override;
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
  HBRUSH CtlColorEdit(HDC dc, HWND edit) override;
  int CursorKind() const override;

 protected:
  void OnAttach() override;
  void OnClick(int id) override;

 private:
  enum Tab { kOverview = 0, kLists, kRules, kAllowlist, kSessions, kTabCount };

  // ---- data
  void ReloadAll();
  void ReloadOverview();
  void ReloadLists();
  void ReloadSites();
  void ReloadSessions();

  // ---- layout / visibility
  void SelectTab(int tab);
  void ApplyTabVisibility();
  void LayoutOverview(const settings::PageFrame& frame, int top);
  void LayoutLists(const settings::PageFrame& frame, int top);
  void LayoutRules(const settings::PageFrame& frame, int top);
  void LayoutAllowlist(const settings::PageFrame& frame, int top);
  void LayoutSessions(const settings::PageFrame& frame, int top);

  // ---- painting
  void PaintChips(HDC hdc);
  void PaintOverview(HDC hdc);
  void PaintLists(HDC hdc);
  void PaintRules(HDC hdc);
  void PaintAllowlist(HDC hdc);
  void PaintSessions(HDC hdc);
  void PaintFooter(HDC hdc, const std::wstring& idleText);

  // ---- actions
  void ToggleMaster();
  void ToggleCosmetic();
  void ToggleScriptlets();
  void UpdateListsNow();
  void ToggleList(int index);
  void SaveRules();
  void ImportRules();
  void RevertRules();
  void LoadRulesIntoEditor();
  void AddSite();
  void RemoveSite(int index);
  void ClearHistory();

  std::wstring EditText(HWND edit) const;
  int RowActionHit(int x, int y) const;  // list rows' own click targets
  RECT ListToggleRect(int row) const;
  RECT SiteRemoveRect(int row) const;
  settings::ScrollView* ViewAt(int x, int y);
  std::vector<settings::ScrollView*> ActiveViews();
  void DoRowAction(int hit);

  std::weak_ptr<bool> AliveToken() const { return alive_; }

  const std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

  RECT area_{};
  RECT title_{};
  RECT subtitle_{};
  RECT chipsBox_{};
  int tab_ = kOverview;
  int lastX_ = 0;
  int lastY_ = 0;

  // Overview
  RECT masterCard_{};
  RECT statCards_[4]{};
  RECT featuresCard_{};
  bool blockingOn_ = true;
  bool cosmeticOn_ = true;
  bool scriptletsOn_ = true;
  bool listsMissing_ = false;
  uint64_t lifetimeBlocked_ = 0;
  uint64_t sessionBlocked_ = 0;
  size_t sessionCount_ = 0;
  uint64_t listsUpdatedMs_ = 0;
  uint64_t overviewSignature_ = ~0ULL;
  std::wstring updatedText_;

  // Filter lists
  RECT listSummary_{};
  RECT updateButton_{};
  settings::ScrollView listView_;
  std::vector<AdblockService::FilterListInfo> lists_;
  bool updating_ = false;

  // Custom rules
  RECT rulesInfo_{};
  RECT rulesPanel_{};
  HWND rulesEdit_ = nullptr;
  HBRUSH rulesBrush_ = nullptr;
  bool rulesLoaded_ = false;
  bool rulesDirty_ = false;
  bool loadingRules_ = false;
  std::wstring rulesStatus_;

  // Allowlist
  RECT allowInfo_{};
  RECT addBox_{};
  HWND addEdit_ = nullptr;
  bool addFocused_ = false;
  settings::ScrollView allowView_;
  std::vector<std::string> sites_;

  // Sessions
  RECT hostHeader_{};
  settings::ScrollView sessionView_;
  settings::ScrollView hostView_;
  std::vector<AdblockHistory::Session> sessions_;
  std::vector<AdblockHistory::HostCount> hosts_;
  uint64_t selectedSession_ = 0;
  uint64_t historyRevision_ = ~0ULL;

  // Pointer state for list rows (they are not in the hit map).
  int hoverRow_ = -1;       // row under the pointer in the primary list
  int hoverHostRow_ = -1;   // sessions tab: row in the domain list
  int pressedRow_ = -1;
  int pressedHit_ = 0;
};

}  // namespace vibrium

#endif  // VIBRIUM_SETTINGS_ADBLOCK_H_
