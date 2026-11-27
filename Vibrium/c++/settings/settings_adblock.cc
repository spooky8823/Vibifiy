#include "settings/settings_adblock.h"

#include <commdlg.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <ctime>
#include <fstream>
#include <iterator>

#include "theme.h"

namespace vibrium {

using namespace settings;

namespace {

constexpr int kRulesEditId = 5201;
constexpr int kAddEditId = 5202;
constexpr WORD kEnterPressed = 0x7F01;  // custom EN_* code from EnterEditProc
constexpr size_t kMaxRulesBytes = 1024 * 1024;

// Hit ids.
constexpr int kHitTab = 700;  // + tab index
constexpr int kHitMaster = 710;
constexpr int kHitCosmetic = 711;
constexpr int kHitScriptlets = 712;
constexpr int kHitUpdateNow = 720;
constexpr int kHitRulesSave = 730;
constexpr int kHitRulesImport = 731;
constexpr int kHitRulesRevert = 732;
constexpr int kHitAllowBox = 740;
constexpr int kHitAllowAdd = 741;
constexpr int kHitClearHistory = 750;
// Row actions (not in the hit map): base + row index.
constexpr int kRowToggleList = 1000;
constexpr int kRowRemoveSite = 2000;
constexpr int kRowPickSession = 3000;

constexpr const wchar_t* kTabLabels[5] = {L"Overview", L"Filter lists",
                                          L"Custom rules", L"Allowlist",
                                          L"Sessions"};

constexpr int kListRowHeight = 60;
constexpr int kSiteRowHeight = 44;
constexpr int kSessionRowHeight = 56;
constexpr int kHostRowHeight = 26;

uint64_t NowEpochMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

std::wstring Ago(uint64_t ms) {
  if (ms == 0) return L"Never";
  const uint64_t now = NowEpochMs();
  const uint64_t seconds = now > ms ? (now - ms) / 1000 : 0;
  if (seconds < 45) return L"Just now";
  const uint64_t minutes = seconds / 60;
  if (minutes < 60) return std::to_wstring(std::max<uint64_t>(minutes, 1)) + L" min ago";
  const uint64_t hours = minutes / 60;
  if (hours < 48) return std::to_wstring(hours) + L" h ago";
  return std::to_wstring(hours / 24) + L" days ago";
}

// "Today, 17:20" / "Yesterday, 17:20" / "26 Nov 2026, 17:20".
std::wstring DateLabel(uint64_t ms) {
  static const wchar_t* kMonths[12] = {L"Jan", L"Feb", L"Mar", L"Apr",
                                       L"May", L"Jun", L"Jul", L"Aug",
                                       L"Sep", L"Oct", L"Nov", L"Dec"};
  const std::time_t then = static_cast<std::time_t>(ms / 1000);
  std::tm local{};
  localtime_s(&local, &then);
  const std::time_t nowT = std::time(nullptr);
  std::tm today{};
  localtime_s(&today, &nowT);

  auto midnight = [](std::tm t) {
    t.tm_hour = t.tm_min = t.tm_sec = 0;
    t.tm_isdst = -1;
    return std::mktime(&t);
  };
  const double days =
      std::difftime(midnight(today), midnight(local)) / (24.0 * 3600.0);
  wchar_t clock[16]{};
  swprintf_s(clock, L"%02d:%02d", local.tm_hour, local.tm_min);
  if (days < 0.5) return std::wstring(L"Today, ") + clock;
  if (days < 1.5) return std::wstring(L"Yesterday, ") + clock;
  wchar_t date[48]{};
  swprintf_s(date, L"%d %ls %d, %ls", local.tm_mday, kMonths[local.tm_mon % 12],
             local.tm_year + 1900, clock);
  return date;
}

std::wstring DurationLabel(uint64_t startMs, uint64_t endMs) {
  const uint64_t seconds = endMs > startMs ? (endMs - startMs) / 1000 : 0;
  const uint64_t hours = seconds / 3600;
  const uint64_t minutes = (seconds % 3600) / 60;
  if (hours > 0) return std::to_wstring(hours) + L"h " + std::to_wstring(minutes) + L"m";
  if (minutes > 0) return std::to_wstring(minutes) + L"m " + std::to_wstring(seconds % 60) + L"s";
  return std::to_wstring(seconds) + L"s";
}

std::wstring FormatSize(uint64_t bytes) {
  wchar_t text[32]{};
  if (bytes >= 1024ULL * 1024) {
    swprintf_s(text, L"%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else {
    swprintf_s(text, L"%llu KB",
               static_cast<unsigned long long>(std::max<uint64_t>(bytes / 1024, 1)));
  }
  return text;
}

// Number of lines that are rules (not blank, not ! comments, not [headers]).
int CountRules(const std::wstring& text) {
  int count = 0;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find(L'\n', start);
    if (end == std::wstring::npos) end = text.size();
    const std::wstring line = Trim(text.substr(start, end - start));
    if (!line.empty() && line[0] != L'!' && line[0] != L'[') ++count;
    start = end + 1;
  }
  return count;
}

// Edit controls want CR LF line ends.
std::wstring NormalizeNewlines(const std::wstring& text) {
  std::wstring out;
  out.reserve(text.size() + text.size() / 16);
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == L'\r') continue;
    if (text[i] == L'\n') out += L"\r\n";
    else out += text[i];
  }
  return out;
}

bool StrictUtf8ToWide(const std::string& value, std::wstring* out) {
  out->clear();
  if (value.empty()) return true;
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                       static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0) return false;
  out->assign(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), out->data(), size);
  return true;
}

// Single-line edit that reports Enter to its parent as a WM_COMMAND with
// kEnterPressed, instead of beeping.
LRESULT CALLBACK EnterEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  const WNDPROC original =
      reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_CHAR && wp == VK_RETURN) return 0;
  if (msg == WM_KEYDOWN && wp == VK_RETURN) {
    SendMessageW(GetParent(hwnd), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(hwnd), kEnterPressed),
                 reinterpret_cast<LPARAM>(hwnd));
    return 0;
  }
  return CallWindowProcW(original, hwnd, msg, wp, lp);
}

void SubclassEnter(HWND edit) {
  const LONG_PTR original = SetWindowLongPtrW(
      edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EnterEditProc));
  SetWindowLongPtrW(edit, GWLP_USERDATA, original);
}

// Dark scrollbars for the rules editor (Windows 10+; harmless elsewhere).
void ApplyDarkScrollbar(HWND window) {
  using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
  static SetWindowThemeFn fn = []() -> SetWindowThemeFn {
    HMODULE module = LoadLibraryW(L"uxtheme.dll");
    FARPROC proc = module ? GetProcAddress(module, "SetWindowTheme") : nullptr;
    return proc ? reinterpret_cast<SetWindowThemeFn>(
                      reinterpret_cast<void*>(proc))
                : nullptr;
  }();
  if (fn) fn(window, L"DarkMode_Explorer", nullptr);
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

AdblockSection::AdblockSection() {
  listView_.rowHeight = kListRowHeight;
  allowView_.rowHeight = kSiteRowHeight;
  sessionView_.rowHeight = kSessionRowHeight;
  hostView_.rowHeight = kHostRowHeight;
}

AdblockSection::~AdblockSection() {
  *alive_ = false;  // late callbacks (list update) must not touch us
  if (rulesBrush_) DeleteObject(rulesBrush_);
}

void AdblockSection::OnAttach() {
  HWND parent = host_->window();
  rulesBrush_ = CreateSolidBrush(kLogBg);

  rulesEdit_ = CreateWindowExW(
      0, L"EDIT", L"",
      WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
      0, 0, 100, 100, parent,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRulesEditId)),
      host_->instance(), nullptr);
  if (rulesEdit_) {
    SendMessageW(rulesEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts().mono), TRUE);
    SendMessageW(rulesEdit_, EM_LIMITTEXT, kMaxRulesBytes, 0);
    ApplyDarkScrollbar(rulesEdit_);
  }

  addEdit_ = CreateWindowExW(
      0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 100, 20, parent,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAddEditId)),
      host_->instance(), nullptr);
  if (addEdit_) {
    SendMessageW(addEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts().ui), TRUE);
    SendMessageW(addEdit_, EM_LIMITTEXT, 253, 0);
    SubclassEnter(addEdit_);
  }
}

void AdblockSection::OnShow(bool /*page_opened*/) {
  ReloadAll();
  if (tab_ == kRules && !rulesDirty_) LoadRulesIntoEditor();
  ApplyTabVisibility();
}

void AdblockSection::OnHide() {
  if (rulesEdit_) ShowWindow(rulesEdit_, SW_HIDE);
  if (addEdit_) ShowWindow(addEdit_, SW_HIDE);
  addFocused_ = false;
}

void AdblockSection::OnTick() {
  AdblockService& service = AdblockService::Get();
  if (tab_ == kOverview) {
    // Cheap change detection: counters, switches and the "x min ago" text.
    const uint64_t signature =
        service.history().revision() * 1000003ULL +
        service.policy().revision() * 10007ULL +
        (service.enabled() ? 1ULL : 0ULL);
    const std::wstring ago = Ago(listsUpdatedMs_);
    if (signature != overviewSignature_ || ago != updatedText_) {
      ReloadOverview();
      RepaintContent();
    }
  } else if (tab_ == kSessions) {
    if (service.history().revision() != historyRevision_) {
      ReloadSessions();
      RepaintContent();
    }
  }
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

void AdblockSection::ReloadAll() {
  ReloadOverview();
  ReloadLists();
  ReloadSites();
  ReloadSessions();
  updating_ = AdblockService::Get().updating_lists();
}

void AdblockSection::ReloadOverview() {
  AdblockService& service = AdblockService::Get();
  const AdblockHistory::Summary summary = service.history().Summarize();
  lifetimeBlocked_ = summary.lifetime_blocked;
  sessionBlocked_ = summary.sessions.empty() ? 0 : summary.sessions[0].blocked;
  sessionCount_ = summary.sessions.size();
  listsUpdatedMs_ = service.ListsUpdatedMs();
  blockingOn_ = service.policy().blocking_enabled();
  cosmeticOn_ = service.policy().cosmetic_enabled();
  scriptletsOn_ = service.policy().scriptlets_enabled();
  listsMissing_ = !service.enabled();
  updatedText_ = Ago(listsUpdatedMs_);
  overviewSignature_ = service.history().revision() * 1000003ULL +
                       service.policy().revision() * 10007ULL +
                       (service.enabled() ? 1ULL : 0ULL);
}

void AdblockSection::ReloadLists() {
  lists_ = AdblockService::Get().FilterLists();
  listView_.count = static_cast<int>(lists_.size());
  listView_.Clamp();
}

void AdblockSection::ReloadSites() {
  sites_ = AdblockService::Get().policy().AllowedSites();
  allowView_.count = static_cast<int>(sites_.size());
  allowView_.Clamp();
}

void AdblockSection::ReloadSessions() {
  AdblockHistory& history = AdblockService::Get().history();
  historyRevision_ = history.revision();
  sessions_ = history.Summarize().sessions;
  bool found = false;
  for (const AdblockHistory::Session& s : sessions_) {
    if (s.id == selectedSession_) found = true;
  }
  if (!found) selectedSession_ = sessions_.empty() ? 0 : sessions_[0].id;
  hosts_ = history.HostsFor(selectedSession_);
  sessionView_.count = static_cast<int>(sessions_.size());
  hostView_.count = static_cast<int>(hosts_.size());
  sessionView_.Clamp();
  hostView_.Clamp();
}

std::wstring AdblockSection::EditText(HWND edit) const {
  if (!edit) return {};
  const int length = GetWindowTextLengthW(edit);
  if (length <= 0) return {};
  std::wstring text(static_cast<size_t>(length) + 1, L'\0');
  GetWindowTextW(edit, text.data(), length + 1);
  text.resize(static_cast<size_t>(length));
  return text;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void AdblockSection::SelectTab(int tab) {
  if (tab < 0 || tab >= kTabCount) return;
  tab_ = tab;
  hoverId_ = 0;
  hoverRow_ = hoverHostRow_ = -1;
  pressedRow_ = -1;
  addFocused_ = false;
  ReloadAll();
  if (tab_ == kRules && !rulesDirty_) LoadRulesIntoEditor();
  Layout(area_);
  RepaintContent();
}

void AdblockSection::ApplyTabVisibility() {
  if (rulesEdit_) ShowWindow(rulesEdit_, tab_ == kRules ? SW_SHOW : SW_HIDE);
  if (addEdit_) {
    const bool show = tab_ == kAllowlist &&
                      (addFocused_ || GetWindowTextLengthW(addEdit_) > 0);
    ShowWindow(addEdit_, show ? SW_SHOW : SW_HIDE);
  }
}

void AdblockSection::Layout(const RECT& area) {
  area_ = area;
  width_ = area.right;
  height_ = area.bottom;
  hits_.Clear();

  const PageFrame frame = MakePageFrame(width_);
  title_ = frame.title;
  subtitle_ = frame.subtitle;
  // Same strip the console uses for its footer.
  const int footerTop = std::max(0, height_ - 20 - 24) + 6;
  footer_ = RECT{frame.cx, footerTop, frame.right, footerTop + 20};

  // ---- tab chips
  HDC dc = GetDC(host_->window());
  int textWidth[kTabCount];
  int textTotal = 0;
  for (int i = 0; i < kTabCount; ++i) {
    textWidth[i] = TextWidth(dc, fonts().ui, kTabLabels[i]);
    textTotal += textWidth[i];
  }
  ReleaseDC(host_->window(), dc);
  const int padding =
      std::clamp((frame.cw - 6 - textTotal) / kTabCount, 8, 22);  // per chip
  int chipsTotal = 6;
  int chipWidth[kTabCount];
  for (int i = 0; i < kTabCount; ++i) {
    chipWidth[i] = textWidth[i] + padding;
    chipsTotal += chipWidth[i];
  }
  const int chipsTop = 104;
  const int chipsHeight = 38;
  chipsBox_ = RECT{frame.cx, chipsTop, frame.cx + chipsTotal, chipsTop + chipsHeight};
  int chipX = chipsBox_.left + 3;
  for (int i = 0; i < kTabCount; ++i) {
    hits_.Add(RECT{chipX, chipsBox_.top + 3, chipX + chipWidth[i], chipsBox_.bottom - 3},
              kHitTab + i);
    chipX += chipWidth[i];
  }

  const int top = chipsBox_.bottom + 16;
  switch (tab_) {
    case kOverview: LayoutOverview(frame, top); break;
    case kLists: LayoutLists(frame, top); break;
    case kRules: LayoutRules(frame, top); break;
    case kAllowlist: LayoutAllowlist(frame, top); break;
    case kSessions: LayoutSessions(frame, top); break;
    default: break;
  }
  ApplyTabVisibility();
}

void AdblockSection::LayoutOverview(const PageFrame& frame, int top) {
  const int cx = frame.cx;
  const int right = frame.right;

  masterCard_ = RECT{cx, top, right, top + 88};
  const RECT track{right - 22 - 56, masterCard_.top + 30, right - 22,
                   masterCard_.top + 58};
  hits_.Add(Inflated(track, 10, 12), kHitMaster);

  const int gap = 12;
  const int columns = frame.cw >= 760 ? 4 : 2;
  const int cardWidth = (frame.cw - gap * (columns - 1)) / columns;
  const int cardHeight = 72;
  const int cardsTop = masterCard_.bottom + 12;
  for (int i = 0; i < 4; ++i) {
    const int col = i % columns;
    const int row = i / columns;
    const int left = cx + col * (cardWidth + gap);
    const int y = cardsTop + row * (cardHeight + gap);
    statCards_[i] = RECT{left, y, left + cardWidth, y + cardHeight};
  }
  const int cardRows = (4 + columns - 1) / columns;
  const int featuresTop = cardsTop + cardRows * cardHeight + (cardRows - 1) * gap + 12;
  featuresCard_ = RECT{cx, featuresTop, right, featuresTop + 2 * 64};
  for (int i = 0; i < 2; ++i) {
    const int rowTop = featuresCard_.top + i * 64;
    const RECT t{right - 22 - 48, rowTop + 19, right - 22, rowTop + 45};
    hits_.Add(Inflated(t, 10, 10), kHitCosmetic + i);
  }
}

void AdblockSection::LayoutLists(const PageFrame& frame, int top) {
  HDC dc = GetDC(host_->window());
  const int buttonWidth =
      std::max(TextWidth(dc, fonts().ui, L"Update now"),
               TextWidth(dc, fonts().ui, L"Checking...")) + 40;
  ReleaseDC(host_->window(), dc);

  listSummary_ = RECT{frame.cx, top, frame.right - buttonWidth - 12, top + 38};
  updateButton_ = RECT{frame.right - buttonWidth, top, frame.right, top + 38};
  hits_.Add(updateButton_, kHitUpdateNow);

  const int panelTop = top + 50;
  listView_.rect = RECT{frame.cx, panelTop, frame.right,
                        std::max(panelTop + 80, height_ - 20 - 24)};
  listView_.count = static_cast<int>(lists_.size());
  listView_.Clamp();
}

void AdblockSection::LayoutRules(const PageFrame& frame, int top) {
  rulesInfo_ = RECT{frame.cx, top, frame.right, top + 40};
  const int buttonsHeight = 38;
  const int bottom = std::max(top + 44 + 120, height_ - 100);
  rulesPanel_ = RECT{frame.cx, top + 44, frame.right, bottom};

  HDC dc = GetDC(host_->window());
  auto widthOf = [&](const wchar_t* text) { return TextWidth(dc, fonts().ui, text) + 36; };
  const int saveWidth = widthOf(L"Save & apply");
  const int importWidth = widthOf(L"Import file...");
  const int revertWidth = widthOf(L"Revert");
  ReleaseDC(host_->window(), dc);

  const int y = rulesPanel_.bottom + 12;
  int x = frame.cx;
  hits_.Add(RECT{x, y, x + saveWidth, y + buttonsHeight}, kHitRulesSave);
  x += saveWidth + 8;
  hits_.Add(RECT{x, y, x + importWidth, y + buttonsHeight}, kHitRulesImport);
  x += importWidth + 8;
  hits_.Add(RECT{x, y, x + revertWidth, y + buttonsHeight}, kHitRulesRevert);

  if (rulesEdit_) {
    MoveWindow(rulesEdit_, rulesPanel_.left + 14, rulesPanel_.top + 10,
               std::max(40, static_cast<int>(rulesPanel_.right - rulesPanel_.left) - 28),
               std::max(40, static_cast<int>(rulesPanel_.bottom - rulesPanel_.top) - 20),
               TRUE);
  }
}

void AdblockSection::LayoutAllowlist(const PageFrame& frame, int top) {
  allowInfo_ = RECT{frame.cx, top, frame.right, top + 22};
  const int y = top + 34;
  const int addWidth = 98;
  addBox_ = RECT{frame.cx, y, frame.right - addWidth - 12, y + 38};
  hits_.Add(addBox_, kHitAllowBox);
  hits_.Add(RECT{frame.right - addWidth, y, frame.right, y + 38}, kHitAllowAdd);

  const int panelTop = y + 38 + 14;
  allowView_.rect = RECT{frame.cx, panelTop, frame.right,
                         std::max(panelTop + 80, height_ - 20 - 24)};
  allowView_.count = static_cast<int>(sites_.size());
  allowView_.Clamp();

  if (addEdit_) {
    MoveWindow(addEdit_, addBox_.left + 14, addBox_.top + 9,
               std::max(40, static_cast<int>(addBox_.right - addBox_.left) - 28),
               20, TRUE);
  }
}

void AdblockSection::LayoutSessions(const PageFrame& frame, int top) {
  HDC dc = GetDC(host_->window());
  const int clearWidth = TextWidth(dc, fonts().ui, L"Clear history") + 26;
  ReleaseDC(host_->window(), dc);
  hits_.Add(RECT{frame.right - clearWidth, chipsBox_.top, frame.right, chipsBox_.bottom},
            kHitClearHistory);

  const int bottom = std::max(top + 120, height_ - 20 - 24);
  const int sessionWidth = std::clamp(frame.cw * 38 / 100, 260, 360);
  sessionView_.rect = RECT{frame.cx, top, frame.cx + sessionWidth, bottom};
  const int hostLeft = frame.cx + sessionWidth + 12;
  hostHeader_ = RECT{hostLeft, top, frame.right, top + 30};
  hostView_.rect = RECT{hostLeft, top + 34, frame.right, bottom};
  sessionView_.count = static_cast<int>(sessions_.size());
  hostView_.count = static_cast<int>(hosts_.size());
  sessionView_.Clamp();
  hostView_.Clamp();
}

RECT AdblockSection::ListToggleRect(int row) const {
  const int y = listView_.RowTop(row);
  const int right = listView_.rect.right - kScrollbarWidth - 18;
  return RECT{right - 48, y + (kListRowHeight - 26) / 2, right,
              y + (kListRowHeight - 26) / 2 + 26};
}

RECT AdblockSection::SiteRemoveRect(int row) const {
  const int y = allowView_.RowTop(row);
  const int right = allowView_.rect.right - kScrollbarWidth - 16;
  return RECT{right - 84, y + 8, right, y + kSiteRowHeight - 8};
}

ScrollView* AdblockSection::ViewAt(int x, int y) {
  switch (tab_) {
    case kLists: return &listView_;
    case kAllowlist: return &allowView_;
    case kSessions:
      return x >= hostView_.rect.left && InRect(Inflated(hostView_.rect, 0, 0), x, y)
                 ? &hostView_
                 : &sessionView_;
    default: return nullptr;
  }
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void AdblockSection::Paint(HDC hdc) {
  const SettingsFonts& f = fonts();
  DrawLabel(hdc, L"Ad blocker", title_, f.title, kText,
            DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  DrawLabel(hdc,
            L"Block ads and trackers, manage filter lists and the sites you want left alone.",
            subtitle_, f.ui, kMuted,
            DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  PaintChips(hdc);

  switch (tab_) {
    case kOverview: PaintOverview(hdc); break;
    case kLists: PaintLists(hdc); break;
    case kRules: PaintRules(hdc); break;
    case kAllowlist: PaintAllowlist(hdc); break;
    case kSessions: PaintSessions(hdc); break;
    default: break;
  }
}

void AdblockSection::PaintChips(HDC hdc) {
  const SettingsFonts& f = fonts();
  FillRound(hdc, chipsBox_, 14, kPanel2, kBorderStrong);
  for (int i = 0; i < kTabCount; ++i) {
    const RECT r = hits_.RectOf(kHitTab + i);
    const bool selected = tab_ == i;
    const bool hover = hoverId_ == kHitTab + i;
    if (selected) {
      FillRound(hdc, r, 10, Theme::Accent());
    } else if (hover) {
      FillRound(hdc, r, 10, kSoft);
    }
    DrawLabel(hdc, kTabLabels[i], r, f.ui,
              selected ? RGB(255, 255, 255) : hover ? kText : kMuted,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  }
  if (tab_ == kSessions) {
    const RECT r = hits_.RectOf(kHitClearHistory);
    DrawPill(hdc, r, L"Clear history", f.ui, false, hoverId_ == kHitClearHistory);
  }
}

void AdblockSection::PaintFooter(HDC hdc, const std::wstring& idleText) {
  const bool flashing = !flash_.empty();
  DrawLabel(hdc, flashing ? flash_ : idleText, footer_, fonts().label,
            flashing ? Theme::Accent() : kMuted,
            DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
}

void AdblockSection::PaintOverview(HDC hdc) {
  const SettingsFonts& f = fonts();

  // ---- master switch
  FillRound(hdc, masterCard_, 14, kPanel, kBorder);
  const int toggleRight = masterCard_.right - 22;
  const RECT track{toggleRight - 56, masterCard_.top + 30, toggleRight,
                   masterCard_.top + 58};
  std::wstring description;
  COLORREF descriptionColor = kMuted;
  if (listsMissing_) {
    description = L"No usable filter lists yet, so nothing is being blocked. They download on first run.";
    descriptionColor = kWarn;
  } else if (blockingOn_) {
    description = L"On. Ads and trackers are blocked on every site except the ones on your allowlist.";
  } else {
    description = L"Off. Websites load with all of their ads and trackers.";
    descriptionColor = kWarn;
  }
  DrawLabel(hdc, L"Ad blocking",
            RECT{masterCard_.left + 22, masterCard_.top + 16, track.left - 16,
                 masterCard_.top + 44},
            f.brand, kText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  DrawLabel(hdc, description,
            RECT{masterCard_.left + 22, masterCard_.top + 48, track.left - 16,
                 masterCard_.top + 72},
            f.label, descriptionColor,
            DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  DrawToggle(hdc, track, blockingOn_, hoverId_ == kHitMaster);

  // ---- counters
  const wchar_t* labels[4] = {L"LIFETIME BLOCKED", L"SESSION BLOCKED",
                              L"SESSIONS", L"LISTS UPDATED"};
  const std::wstring values[4] = {FormatCount(lifetimeBlocked_),
                                  FormatCount(sessionBlocked_),
                                  FormatCount(sessionCount_), updatedText_};
  for (int i = 0; i < 4; ++i) {
    const RECT& r = statCards_[i];
    FillRound(hdc, r, 14, kPanel, kBorder);
    DrawLabel(hdc, labels[i], RECT{r.left + 16, r.top + 12, r.right - 10, r.top + 30},
              f.font_small_12px, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    DrawLabel(hdc, values[i], RECT{r.left + 16, r.top + 32, r.right - 10, r.bottom - 8},
              i == 3 ? f.brand : f.stat, kText,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  }

  // ---- cosmetic filtering / scriptlets
  FillRound(hdc, featuresCard_, 14, kPanel, kBorder);
  struct Feature {
    const wchar_t* title;
    const wchar_t* description;
    bool on;
    int hit;
  };
  const Feature features[2] = {
      {L"Cosmetic filtering",
       L"Hide the empty spaces, banners and pop-ups ads leave behind on a page.",
       cosmeticOn_, kHitCosmetic},
      {L"Scriptlets",
       L"Run small protection scripts that stop video ads and anti-adblock walls.",
       scriptletsOn_, kHitScriptlets}};
  for (int i = 0; i < 2; ++i) {
    const int rowTop = featuresCard_.top + i * 64;
    if (i == 1) {
      HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
      HGDIOBJ old = SelectObject(hdc, pen);
      MoveToEx(hdc, featuresCard_.left + 16, rowTop, nullptr);
      LineTo(hdc, featuresCard_.right - 16, rowTop);
      SelectObject(hdc, old);
      DeleteObject(pen);
    }
    const RECT t{featuresCard_.right - 22 - 48, rowTop + 19, featuresCard_.right - 22,
                 rowTop + 45};
    DrawLabel(hdc, features[i].title,
              RECT{featuresCard_.left + 22, rowTop + 10, t.left - 16, rowTop + 34},
              f.ui, kText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    DrawLabel(hdc, features[i].description,
              RECT{featuresCard_.left + 22, rowTop + 34, t.left - 16, rowTop + 56},
              f.label, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    DrawToggle(hdc, t, features[i].on, hoverId_ == features[i].hit);
  }

  PaintFooter(hdc, L"Changes apply to every tab straight away. Reload a page to see cosmetic changes.");
}

void AdblockSection::PaintLists(HDC hdc) {
  const SettingsFonts& f = fonts();

  size_t enabled = 0;
  for (const auto& list : lists_) enabled += list.enabled ? 1 : 0;
  DrawLabel(hdc,
            FormatCount(lists_.size()) + L" lists  -  " + FormatCount(enabled) +
                L" on  -  last updated " + (updatedText_.empty() ? L"never" : ToLower(updatedText_)),
            listSummary_, f.ui, kMuted,
            DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  const bool canUpdate = AdblockService::Get().can_update_lists() && !updating_;
  DrawPill(hdc, updateButton_, updating_ ? L"Checking..." : L"Update now", f.ui,
           true, hoverId_ == kHitUpdateNow && canUpdate, canUpdate);

  FillRound(hdc, listView_.rect, 14, kLogBg, kBorder);
  const int saved = SaveDC(hdc);
  IntersectClipRect(hdc, listView_.rect.left + 1, listView_.rect.top + 4,
                    listView_.rect.right - 1, listView_.rect.bottom - 4);
  if (lists_.empty()) {
    DrawLabel(hdc, L"No filter lists found.",
              RECT{listView_.rect.left + 18, listView_.rect.top + 14,
                   listView_.rect.right - 18, listView_.rect.top + 44},
              f.ui, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  }
  for (int i = listView_.FirstVisible(); i <= listView_.LastVisible(); ++i) {
    const auto& list = lists_[static_cast<size_t>(i)];
    const int y = listView_.RowTop(i);
    if (i == hoverRow_) {
      RECT highlight{listView_.rect.left + 1, y, listView_.rect.right - 1, y + kListRowHeight};
      HBRUSH tint = CreateSolidBrush(kSoft);
      FillRect(hdc, &highlight, tint);
      DeleteObject(tint);
    }
    const RECT toggle = ListToggleRect(i);
    DrawToggle(hdc, toggle, list.enabled, i == hoverRow_);

    std::wstring detail = Utf8ToWide(list.description);
    if (!list.present) {
      detail += L"  -  not downloaded yet";
    } else {
      detail += L"  -  updated " + ToLower(Ago(list.updated_ms)) + L"  -  " +
                FormatSize(list.size_bytes);
    }
    const int textLeft = listView_.rect.left + 18;
    DrawLabel(hdc, Utf8ToWide(list.title),
              RECT{textLeft, y + 8, toggle.left - 16, y + 31}, f.ui,
              list.enabled ? kText : kMuted,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    DrawLabel(hdc, detail, RECT{textLeft, y + 31, toggle.left - 16, y + 52}, f.label,
              kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  }
  RestoreDC(hdc, saved);
  RECT thumb{};
  if (listView_.Thumb(&thumb)) FillRound(hdc, thumb, kScrollbarWidth, Theme::Accent());

  PaintFooter(hdc, AdblockService::Get().can_update_lists()
                       ? L"Lists refresh by themselves every few hours. Turning a list off rebuilds the filters in the background."
                       : L"Lists cannot be refreshed in this session (updates are switched off).");
}

void AdblockSection::PaintRules(HDC hdc) {
  const SettingsFonts& f = fonts();
  DrawLabel(hdc,
            L"Add your own filters, one per line, in the same syntax as EasyList and uBlock Origin. "
            L"Lines starting with ! are comments.",
            rulesInfo_, f.label, kMuted, DT_WORDBREAK | DT_LEFT);

  FillRound(hdc, rulesPanel_, 14, kLogBg, kBorder);

  struct ButtonDef {
    int id;
    const wchar_t* label;
    bool primary;
  };
  const ButtonDef buttons[] = {{kHitRulesSave, L"Save & apply", true},
                               {kHitRulesImport, L"Import file...", false},
                               {kHitRulesRevert, L"Revert", false}};
  for (const ButtonDef& b : buttons) {
    DrawPill(hdc, hits_.RectOf(b.id), b.label, f.ui, b.primary, hoverId_ == b.id);
  }

  const int rules = CountRules(EditText(rulesEdit_));
  std::wstring status;
  if (rulesDirty_) {
    status = L"Unsaved changes  -  " + FormatCount(static_cast<uint64_t>(rules)) +
             (rules == 1 ? L" rule" : L" rules");
  } else if (rules == 0) {
    status = L"No custom rules yet.";
  } else {
    status = FormatCount(static_cast<uint64_t>(rules)) +
             (rules == 1 ? L" custom rule is active" : L" custom rules are active");
  }
  PaintFooter(hdc, status);
}

void AdblockSection::PaintAllowlist(HDC hdc) {
  const SettingsFonts& f = fonts();
  DrawLabel(hdc, L"Ad blocking is switched off on these sites and on their subdomains.",
            allowInfo_, f.label, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

  // ---- add pill (the EDIT control sits on top of it)
  FillRound(hdc, addBox_, 14, kPanel2, addFocused_ ? Theme::Accent() : kBorderStrong,
            addFocused_ ? 2 : 1);
  if (!addEdit_ || !IsWindowVisible(addEdit_)) {
    DrawLabel(hdc, L"Add a site, for example example.com",
              RECT{addBox_.left + 16, addBox_.top, addBox_.right - 14, addBox_.bottom},
              f.ui, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  }
  DrawPill(hdc, hits_.RectOf(kHitAllowAdd), L"Add", f.ui, true, hoverId_ == kHitAllowAdd);

  // ---- list
  FillRound(hdc, allowView_.rect, 14, kLogBg, kBorder);
  const int saved = SaveDC(hdc);
  IntersectClipRect(hdc, allowView_.rect.left + 1, allowView_.rect.top + 4,
                    allowView_.rect.right - 1, allowView_.rect.bottom - 4);
  if (sites_.empty()) {
    DrawLabel(hdc, L"No sites yet. Add one above to stop blocking ads there.",
              RECT{allowView_.rect.left + 18, allowView_.rect.top + 14,
                   allowView_.rect.right - 18, allowView_.rect.top + 44},
              f.ui, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  }
  for (int i = allowView_.FirstVisible(); i <= allowView_.LastVisible(); ++i) {
    const int y = allowView_.RowTop(i);
    if (i == hoverRow_) {
      RECT highlight{allowView_.rect.left + 1, y, allowView_.rect.right - 1, y + kSiteRowHeight};
      HBRUSH tint = CreateSolidBrush(kSoft);
      FillRect(hdc, &highlight, tint);
      DeleteObject(tint);
    }
    const RECT remove = SiteRemoveRect(i);
    const bool overRemove = i == hoverRow_ && InRect(remove, lastX_, lastY_);
    DrawPill(hdc, remove, L"Remove", f.label, false, overRemove);
    DrawLabel(hdc, Utf8ToWide(sites_[static_cast<size_t>(i)]),
              RECT{allowView_.rect.left + 18, y, remove.left - 12, y + kSiteRowHeight},
              f.ui, kText, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  }
  RestoreDC(hdc, saved);
  RECT thumb{};
  if (allowView_.Thumb(&thumb)) FillRound(hdc, thumb, kScrollbarWidth, Theme::Accent());

  PaintFooter(hdc, FormatCount(sites_.size()) + (sites_.size() == 1 ? L" site" : L" sites") +
                       L" on the list");
}

void AdblockSection::PaintSessions(HDC hdc) {
  const SettingsFonts& f = fonts();

  // ---- sessions
  FillRound(hdc, sessionView_.rect, 14, kLogBg, kBorder);
  int saved = SaveDC(hdc);
  IntersectClipRect(hdc, sessionView_.rect.left + 1, sessionView_.rect.top + 4,
                    sessionView_.rect.right - 1, sessionView_.rect.bottom - 4);
  for (int i = sessionView_.FirstVisible(); i <= sessionView_.LastVisible(); ++i) {
    const AdblockHistory::Session& s = sessions_[static_cast<size_t>(i)];
    const int y = sessionView_.RowTop(i);
    const bool selected = s.id == selectedSession_;
    const bool hover = i == hoverRow_;
    const RECT inner{sessionView_.rect.left + 6, y + 3,
                     sessionView_.rect.right - kScrollbarWidth - 14,
                     y + kSessionRowHeight - 3};
    if (selected) {
      FillRound(hdc, inner, 12, Theme::Accent());
    } else if (hover) {
      FillRound(hdc, inner, 12, kSoft);
    }
    const COLORREF primary = selected ? RGB(255, 255, 255) : kText;
    const COLORREF secondary = selected ? RGB(235, 235, 245) : kMuted;
    std::wstring title = DateLabel(s.start_ms);
    if (s.live) title = L"This session  -  " + title;
    DrawLabel(hdc, title, RECT{inner.left + 12, inner.top + 4, inner.right - 8, inner.top + 25},
              f.ui, primary, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    DrawLabel(hdc,
              FormatCount(s.blocked) + L" blocked  -  " + FormatCount(s.allowed) +
                  L" allowed  -  " + DurationLabel(s.start_ms, s.end_ms),
              RECT{inner.left + 12, inner.top + 25, inner.right - 8, inner.top + 46},
              f.label, secondary, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  }
  RestoreDC(hdc, saved);
  RECT thumb{};
  if (sessionView_.Thumb(&thumb)) FillRound(hdc, thumb, kScrollbarWidth, Theme::Accent());

  // ---- blocked domains of the selected session
  uint64_t total = 0;
  for (const auto& h : hosts_) total += h.count;
  DrawLabel(hdc, L"BLOCKED BY DOMAIN",
            RECT{hostHeader_.left + 4, hostHeader_.top, hostHeader_.right, hostHeader_.bottom},
            f.font_small_12px, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  DrawLabel(hdc,
            FormatCount(total) + L" requests  -  " + FormatCount(hosts_.size()) + L" domains",
            RECT{hostHeader_.left, hostHeader_.top, hostHeader_.right - 4, hostHeader_.bottom},
            f.label, kMuted, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);

  FillRound(hdc, hostView_.rect, 14, kLogBg, kBorder);
  saved = SaveDC(hdc);
  IntersectClipRect(hdc, hostView_.rect.left + 1, hostView_.rect.top + 4,
                    hostView_.rect.right - 1, hostView_.rect.bottom - 4);
  if (hosts_.empty()) {
    DrawLabel(hdc, L"Nothing was blocked in this session.",
              RECT{hostView_.rect.left + 18, hostView_.rect.top + 14,
                   hostView_.rect.right - 18, hostView_.rect.top + 44},
              f.ui, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  }
  const int countWidth = 90;
  for (int i = hostView_.FirstVisible(); i <= hostView_.LastVisible(); ++i) {
    const auto& h = hosts_[static_cast<size_t>(i)];
    const int y = hostView_.RowTop(i);
    if (i == hoverHostRow_) {
      RECT highlight{hostView_.rect.left + 1, y, hostView_.rect.right - 1, y + kHostRowHeight};
      HBRUSH tint = CreateSolidBrush(kSoft);
      FillRect(hdc, &highlight, tint);
      DeleteObject(tint);
    }
    const int countRight = hostView_.rect.right - kScrollbarWidth - 14;
    DrawLabel(hdc, Utf8ToWide(h.host),
              RECT{hostView_.rect.left + 16, y, countRight - countWidth - 8, y + kHostRowHeight},
              f.mono, kText, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    DrawLabel(hdc, FormatCount(h.count),
              RECT{countRight - countWidth, y, countRight, y + kHostRowHeight}, f.mono,
              kBad, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
  }
  RestoreDC(hdc, saved);
  if (hostView_.Thumb(&thumb)) FillRound(hdc, thumb, kScrollbarWidth, Theme::Accent());

  // Footer: the example URL of the hovered domain, else a summary.
  std::wstring idle = FormatCount(sessions_.size()) + (sessions_.size() == 1 ? L" session" : L" sessions") +
                      L" recorded. Pick one to see what was blocked in it.";
  if (hoverHostRow_ >= 0 && hoverHostRow_ < static_cast<int>(hosts_.size())) {
    const std::string& example = hosts_[static_cast<size_t>(hoverHostRow_)].example;
    if (!example.empty()) idle = Utf8ToWide(example);
  }
  PaintFooter(hdc, idle);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

std::vector<ScrollView*> AdblockSection::ActiveViews() {
  switch (tab_) {
    case kLists: return {&listView_};
    case kAllowlist: return {&allowView_};
    case kSessions: return {&sessionView_, &hostView_};
    default: return {};
  }
}

int AdblockSection::RowActionHit(int x, int y) const {
  switch (tab_) {
    case kLists: {
      const int row = listView_.RowAt(x, y);
      if (row >= 0 && row < static_cast<int>(lists_.size()) &&
          InRect(Inflated(ListToggleRect(row), 8, 8), x, y)) {
        return kRowToggleList + row;
      }
      break;
    }
    case kAllowlist: {
      const int row = allowView_.RowAt(x, y);
      if (row >= 0 && row < static_cast<int>(sites_.size()) &&
          InRect(SiteRemoveRect(row), x, y)) {
        return kRowRemoveSite + row;
      }
      break;
    }
    case kSessions: {
      const int row = sessionView_.RowAt(x, y);
      if (row >= 0 && row < static_cast<int>(sessions_.size())) {
        return kRowPickSession + row;
      }
      break;
    }
    default:
      break;
  }
  return 0;
}

int AdblockSection::CursorKind() const {
  if (hoverId_ == kHitAllowBox) return 2;
  if (hoverId_ != 0) return 1;
  return RowActionHit(lastX_, lastY_) != 0 ? 1 : 0;
}

void AdblockSection::MouseMove(int x, int y) {
  lastX_ = x;
  lastY_ = y;
  for (ScrollView* view : ActiveViews()) {
    if (view->dragging) {
      if (view->Drag(y)) RepaintRect(view->rect);
      return;
    }
  }

  const int id = hits_.Test(x, y);
  if (id != hoverId_) {
    hoverId_ = id;
    RepaintContent();
  }

  int row = -1;
  int hostRow = -1;
  switch (tab_) {
    case kLists: row = listView_.RowAt(x, y); break;
    case kAllowlist: row = allowView_.RowAt(x, y); break;
    case kSessions:
      row = sessionView_.RowAt(x, y);
      hostRow = hostView_.RowAt(x, y);
      break;
    default: break;
  }
  if (row != hoverRow_ || (tab_ == kAllowlist && row >= 0)) {
    // Allowlist rows hold a button, so moving inside a row matters too.
    hoverRow_ = row;
    if (ScrollView* view = ViewAt(x, y)) {
      RepaintRect(tab_ == kSessions ? sessionView_.rect : view->rect);
    }
  }
  if (hostRow != hoverHostRow_) {
    hoverHostRow_ = hostRow;
    RepaintRect(hostView_.rect);
    RepaintRect(footer_);
  }
}

void AdblockSection::MouseLeave() {
  if (hoverId_ != 0 || hoverRow_ != -1 || hoverHostRow_ != -1) {
    hoverId_ = 0;
    hoverRow_ = -1;
    hoverHostRow_ = -1;
    RepaintContent();
  }
}

bool AdblockSection::MouseDown(int x, int y) {
  for (ScrollView* view : ActiveViews()) {
    if (view->BeginDrag(x, y)) {
      RepaintRect(view->rect);
      return true;
    }
  }
  pressedId_ = hits_.Test(x, y);
  pressedHit_ = RowActionHit(x, y);
  return false;
}

void AdblockSection::MouseUp(int x, int y) {
  for (ScrollView* view : ActiveViews()) {
    if (view->dragging) {
      view->dragging = false;
      RepaintRect(view->rect);
      return;
    }
  }
  const int hit = RowActionHit(x, y);
  const int pressed = pressedHit_;
  pressedHit_ = 0;
  SettingsSection::MouseUp(x, y);
  if (pressed != 0 && pressed == hit) DoRowAction(hit);
}

void AdblockSection::CaptureLost() {
  for (ScrollView* view : {&listView_, &allowView_, &sessionView_, &hostView_}) {
    if (view->dragging) {
      view->dragging = false;
      RepaintRect(view->rect);
    }
  }
}

void AdblockSection::MouseWheel(int notches) {
  if (tab_ == kRules) {
    // The editor scrolls itself when it has focus; otherwise help it along.
    if (rulesEdit_ && InRect(rulesPanel_, lastX_, lastY_)) {
      SendMessageW(rulesEdit_, EM_LINESCROLL, 0, -notches * 3);
    }
    return;
  }
  if (ScrollView* view = ViewAt(lastX_, lastY_)) {
    view->Wheel(notches);
    RepaintRect(view->rect);
  }
}

bool AdblockSection::KeyDown(WPARAM key) {
  ScrollView* view = tab_ == kSessions ? &sessionView_ : ViewAt(lastX_, lastY_);
  if (!view || !view->Key(key)) return false;
  RepaintRect(view->rect);
  return true;
}

bool AdblockSection::Command(WPARAM wParam, LPARAM) {
  const int id = LOWORD(wParam);
  const WORD code = HIWORD(wParam);
  if (id == kRulesEditId) {
    if (code == EN_CHANGE && !loadingRules_) {
      rulesDirty_ = true;
      RepaintRect(footer_);
    }
    return true;
  }
  if (id == kAddEditId) {
    if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
      addFocused_ = code == EN_SETFOCUS;
      if (code == EN_KILLFOCUS && GetWindowTextLengthW(addEdit_) == 0) {
        ShowWindow(addEdit_, SW_HIDE);  // the page paints the hint
      }
      RepaintRect(addBox_);
    } else if (code == kEnterPressed) {
      AddSite();
    }
    return true;
  }
  return false;
}

HBRUSH AdblockSection::CtlColorEdit(HDC dc, HWND edit) {
  if (edit == rulesEdit_) {
    SetTextColor(dc, kText);
    SetBkColor(dc, kLogBg);
    return rulesBrush_;
  }
  return SettingsSection::CtlColorEdit(dc, edit);
}

void AdblockSection::OnClick(int id) {
  if (id >= kHitTab && id < kHitTab + kTabCount) {
    SelectTab(id - kHitTab);
    return;
  }
  switch (id) {
    case kHitMaster: ToggleMaster(); break;
    case kHitCosmetic: ToggleCosmetic(); break;
    case kHitScriptlets: ToggleScriptlets(); break;
    case kHitUpdateNow: UpdateListsNow(); break;
    case kHitRulesSave: SaveRules(); break;
    case kHitRulesImport: ImportRules(); break;
    case kHitRulesRevert: RevertRules(); break;
    case kHitAllowBox:
      if (addEdit_) {
        addFocused_ = true;
        ShowWindow(addEdit_, SW_SHOW);
        SetFocus(addEdit_);
      }
      break;
    case kHitAllowAdd: AddSite(); break;
    case kHitClearHistory: ClearHistory(); break;
    default: break;
  }
}

void AdblockSection::DoRowAction(int hit) {
  if (hit >= kRowPickSession) {
    const size_t index = static_cast<size_t>(hit - kRowPickSession);
    if (index < sessions_.size()) {
      selectedSession_ = sessions_[index].id;
      hosts_ = AdblockService::Get().history().HostsFor(selectedSession_);
      hostView_.count = static_cast<int>(hosts_.size());
      hostView_.scrollY = 0;
      hoverHostRow_ = -1;
      RepaintContent();
    }
  } else if (hit >= kRowRemoveSite) {
    RemoveSite(hit - kRowRemoveSite);
  } else if (hit >= kRowToggleList) {
    ToggleList(hit - kRowToggleList);
  }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void AdblockSection::ToggleMaster() {
  AdblockService& service = AdblockService::Get();
  service.policy().SetBlockingEnabled(!blockingOn_);
  ReloadOverview();
  Flash(blockingOn_ ? L"Ad blocking is on."
                    : L"Ad blocking is off. Pages will load with their ads.");
  RepaintContent();
}

void AdblockSection::ToggleCosmetic() {
  AdblockService::Get().policy().SetCosmeticEnabled(!cosmeticOn_);
  ReloadOverview();
  Flash(cosmeticOn_ ? L"Cosmetic filtering is on. Reload open pages to apply it."
                    : L"Cosmetic filtering is off. Reload open pages to apply it.");
  RepaintContent();
}

void AdblockSection::ToggleScriptlets() {
  AdblockService::Get().policy().SetScriptletsEnabled(!scriptletsOn_);
  ReloadOverview();
  Flash(scriptletsOn_ ? L"Scriptlets are on. Reload open pages to apply them."
                      : L"Scriptlets are off. Reload open pages to apply them.");
  RepaintContent();
}

void AdblockSection::UpdateListsNow() {
  AdblockService& service = AdblockService::Get();
  if (!service.can_update_lists()) {
    Flash(L"Filter lists cannot be updated in this session.");
    return;
  }
  if (updating_) return;

  const std::weak_ptr<bool> alive = AliveToken();
  updating_ = true;  // set first: the callback may run before the call returns
  const bool started = service.UpdateListsNow([this, alive](const UpdateResult& r) {
    const std::shared_ptr<bool> token = alive.lock();
    if (!token || !*token) return;  // the settings page is gone
    updating_ = false;
    std::wstring message;
    if (r.total == 0) {
      message = L"All lists are already up to date.";
    } else if (r.failed == r.total) {
      message = L"Could not reach the filter list servers. Check your connection and try again.";
    } else {
      message = L"Checked " + FormatCount(r.total) + L" lists: " +
                FormatCount(r.updated) + L" updated, " +
                FormatCount(r.unchanged) + L" already up to date";
      if (r.failed > 0) message += L", " + FormatCount(r.failed) + L" failed";
      message += L".";
    }
    ReloadAll();
    Flash(message);
    RepaintContent();
  });
  if (started) {
    Flash(L"Checking filter lists...");
  } else {
    updating_ = false;
    Flash(L"A check is already running.");
  }
  RepaintContent();
}

void AdblockSection::ToggleList(int index) {
  if (index < 0 || index >= static_cast<int>(lists_.size())) return;
  const auto list = lists_[static_cast<size_t>(index)];
  const bool enable = !list.enabled;
  if (!AdblockService::Get().SetFilterListEnabled(list.name, enable)) {
    Flash(L"Keep at least one list (or your custom rules) on, or switch ad blocking off with the main switch.");
    return;
  }
  ReloadLists();
  Flash(Utf8ToWide(list.title) + (enable ? L" is on." : L" is off.") +
        L" The filters are rebuilding in the background.");
  RepaintContent();
}

void AdblockSection::LoadRulesIntoEditor() {
  if (!rulesEdit_) return;
  const std::string text = AdblockService::Get().LoadCustomRules();
  std::wstring wide;
  if (!StrictUtf8ToWide(text, &wide)) wide = Utf8ToWide(text);
  if (!wide.empty() && wide[0] == 0xFEFF) wide.erase(0, 1);
  loadingRules_ = true;
  SetWindowTextW(rulesEdit_, NormalizeNewlines(wide).c_str());
  loadingRules_ = false;
  rulesDirty_ = false;
  rulesLoaded_ = true;
}

void AdblockSection::SaveRules() {
  const std::wstring wide = EditText(rulesEdit_);
  const int rules = CountRules(wide);
  switch (AdblockService::Get().SaveCustomRules(WideToUtf8(wide))) {
    case AdblockService::CustomRulesStatus::kSaved:
      rulesDirty_ = false;
      Flash(L"Saved. " + FormatCount(static_cast<uint64_t>(rules)) +
            (rules == 1 ? L" rule is" : L" rules are") +
            L" being applied to every tab.");
      break;
    case AdblockService::CustomRulesStatus::kCleared:
      rulesDirty_ = false;
      Flash(L"Custom rules cleared.");
      break;
    case AdblockService::CustomRulesStatus::kInvalid:
      Flash(L"Not saved: no valid filter rules were found. Check the syntax and try again.");
      break;
    case AdblockService::CustomRulesStatus::kTooLarge:
      Flash(L"Not saved: custom rules can be at most 1 MB.");
      break;
    case AdblockService::CustomRulesStatus::kWriteFailed:
      Flash(L"Could not write the rules file.");
      break;
    case AdblockService::CustomRulesStatus::kUnavailable:
      Flash(L"Custom rules are not available in this session.");
      break;
  }
  RepaintContent();
}

void AdblockSection::ImportRules() {
  OPENFILENAMEW dialog{};
  wchar_t path[MAX_PATH] = L"";
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = host_->window();
  dialog.lpstrFilter = L"Filter lists (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
  dialog.lpstrFile = path;
  dialog.nMaxFile = MAX_PATH;
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
  if (!GetOpenFileNameW(&dialog)) return;  // cancelled

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    Flash(L"Could not open that file.");
    return;
  }
  std::string bytes((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
  if (bytes.size() > kMaxRulesBytes) {
    Flash(L"That file is larger than 1 MB.");
    return;
  }
  if (bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) bytes.erase(0, 3);
  std::wstring imported;
  if (!StrictUtf8ToWide(bytes, &imported)) {
    Flash(L"That file is not valid UTF-8 text.");
    return;
  }
  imported = NormalizeNewlines(imported);

  std::wstring current = EditText(rulesEdit_);
  if (!current.empty() && current.size() >= 2 &&
      current.compare(current.size() - 2, 2, L"\r\n") != 0) {
    current += L"\r\n";
  }
  current += imported;
  if (current.size() * sizeof(wchar_t) / 2 > kMaxRulesBytes) {
    Flash(L"The combined rules would be larger than 1 MB.");
    return;
  }
  SetWindowTextW(rulesEdit_, current.c_str());
  rulesDirty_ = true;
  const int rules = CountRules(imported);
  Flash(L"Imported " + FormatCount(static_cast<uint64_t>(rules)) +
        (rules == 1 ? L" rule" : L" rules") + L". Press Save & apply to start using them.");
  RepaintContent();
}

void AdblockSection::RevertRules() {
  LoadRulesIntoEditor();
  Flash(L"Reverted to the saved rules.");
  RepaintContent();
}

void AdblockSection::AddSite() {
  const std::wstring input = Trim(EditText(addEdit_));
  if (input.empty()) {
    Flash(L"Type a site first, for example example.com.");
    return;
  }
  std::string host;
  bool already = false;
  if (!AdblockService::Get().policy().AddAllowedSite(WideToUtf8(input), &host, &already)) {
    Flash(L"That is not a valid site. Use a name like example.com.");
    return;
  }
  SetWindowTextW(addEdit_, L"");
  ReloadSites();

  // Scroll the new entry into view (the list is sorted).
  const auto found = std::find(sites_.begin(), sites_.end(), host);
  if (found != sites_.end()) {
    const int rowTop = static_cast<int>(found - sites_.begin()) * kSiteRowHeight;
    if (rowTop < allowView_.scrollY) {
      allowView_.scrollY = rowTop;
    } else if (rowTop + kSiteRowHeight > allowView_.scrollY + allowView_.ViewHeight()) {
      allowView_.scrollY = rowTop + kSiteRowHeight - allowView_.ViewHeight();
    }
    allowView_.Clamp();
  }
  const std::wstring shown = Utf8ToWide(host);
  Flash(already ? shown + L" is already on the list."
                : L"Added " + shown + L". Ads are no longer blocked there.");
  RepaintContent();
}

void AdblockSection::RemoveSite(int index) {
  if (index < 0 || index >= static_cast<int>(sites_.size())) return;
  const std::string host = sites_[static_cast<size_t>(index)];
  AdblockService::Get().policy().RemoveAllowedSite(host);
  ReloadSites();
  hoverRow_ = -1;
  Flash(L"Removed " + Utf8ToWide(host) + L". Ads are blocked there again.");
  RepaintContent();
}

void AdblockSection::ClearHistory() {
  if (MessageBoxW(host_->window(),
                  L"Forget every recorded session and reset the lifetime total?\n\n"
                  L"The session in progress keeps its own count.",
                  L"Clear ad-blocking history", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
    return;
  }
  AdblockService::Get().history().Clear();
  ReloadAll();
  Flash(L"History cleared.");
  RepaintContent();
}

}  // namespace vibrium