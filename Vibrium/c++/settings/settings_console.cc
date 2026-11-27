#include "settings/settings_console.h"

#include <commdlg.h>

#include <algorithm>
#include <cstring>
#include <fstream>

#include "adblock/adblock_service.h"
#include "theme.h"

namespace vibrium {

using namespace settings;

namespace {

constexpr int kEditId = 5101;

// Hit ids.
constexpr int kHitChip = 200;  // + filter (0..3)
constexpr int kHitPause = 300;
constexpr int kHitCopy = 301;
constexpr int kHitExport = 302;
constexpr int kHitClear = 303;
constexpr int kHitSearch = 400;

constexpr const wchar_t* kChipLabels[4] = {L"All", L"Blocked", L"Allowed",
                                           L"Errors"};

// Decision codes stored in Row::decision.
constexpr int kAllowCode = 0;
constexpr int kBlockCode = 1;
constexpr int kErrorCode = 2;

const wchar_t* DecisionLabel(int decision) {
  switch (decision) {
    case kBlockCode:
      return L"BLOCKED";
    case kErrorCode:
      return L"ERROR";
    default:
      return L"ALLOWED";
  }
}

COLORREF DecisionColor(int decision) {
  switch (decision) {
    case kBlockCode:
      return kBad;
    case kErrorCode:
      return kWarn;
    default:
      return kGood;
  }
}

std::wstring RowLine(const std::wstring& time, int decision,
                     const std::wstring& kind, const std::wstring& url,
                     const std::wstring& source) {
  std::wstring line = L"[" + time + L"] [" + DecisionLabel(decision) + L"] [" +
                      kind + L"] " + url;
  if (!source.empty()) line += L"  (from " + source + L")";
  return line;
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

void ConsoleSection::OnAttach() {
  searchEdit_ = CreateWindowExW(
      0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 100, 20,
      host_->window(), reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)),
      host_->instance(), nullptr);
  if (searchEdit_) {
    SendMessageW(searchEdit_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(fonts().font_body_15px), TRUE);
  }
}

void ConsoleSection::OnShow(bool page_opened) {
  // A search typed earlier is still applied, so keep it visible.
  if (searchEdit_ && GetWindowTextLengthW(searchEdit_) > 0) {
    ShowWindow(searchEdit_, SW_SHOW);
  }
  Refresh(page_opened || !paused_);
}

void ConsoleSection::OnHide() {
  if (searchEdit_) ShowWindow(searchEdit_, SW_HIDE);
  searchFocused_ = false;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void ConsoleSection::OnScrolled() {
  log_.Clamp();
  stickToBottom_ = log_.AtBottom();
}

void ConsoleSection::Layout(const RECT& area) {
  area_ = area;
  width_ = area.right;
  height_ = area.bottom;
  hits_.Clear();

  const PageFrame frame = MakePageFrame(width_);
  const int cx = frame.cx;
  const int cw = frame.cw;
  const int right = frame.right;
  title_ = frame.title;
  subtitle_ = frame.subtitle;

  // Cards flow into 5, 3 or 2 columns so labels never get truncated.
  const int cardsTop = 104;
  const int cardHeight = 72;
  const int gap = 12;
  const int columns = cw >= 728 ? 5 : cw >= 480 ? 3 : 2;
  const int cardWidth = (cw - gap * (columns - 1)) / columns;
  for (int i = 0; i < 5; ++i) {
    const int col = i % columns;
    const int row = i / columns;
    const int left = cx + col * (cardWidth + gap);
    const int top = cardsTop + row * (cardHeight + gap);
    cards_[i] = RECT{left, top, left + cardWidth, top + cardHeight};
  }
  const int cardRows = (5 + columns - 1) / columns;

  int y = cardsTop + cardRows * cardHeight + (cardRows - 1) * gap;
  notice_ = RECT{};
  if (listsMissing_) {
    notice_ = RECT{cx, y + 12, right, y + 12 + 42};
    y = notice_.bottom;
  }

  const int toolbarTop = y + 16;
  const int toolbarHeight = 38;
  const int rowGap = 10;

  HDC dc = GetDC(host_->window());
  auto buttonWidth = [&](const std::wstring& text) {
    return TextWidth(dc, fonts().font_body_15px, text) + 26;
  };
  pauseLabel_ = paused_ ? L"Resume" : L"Pause";
  const int pauseWidth = std::max(buttonWidth(L"Pause"), buttonWidth(L"Resume"));
  const int copyWidth = buttonWidth(L"Copy");
  const int exportWidth = buttonWidth(L"Export");
  const int clearWidth = buttonWidth(L"Clear");
  int chipWidth[4];
  int chipsTotal = 6;
  for (int i = 0; i < 4; ++i) {
    chipWidth[i] = TextWidth(dc, fonts().font_body_15px, kChipLabels[i]) + 22;
    chipsTotal += chipWidth[i];
  }
  ReleaseDC(host_->window(), dc);

  const int buttonsTotal = pauseWidth + copyWidth + exportWidth + clearWidth + 8 * 3;

  // Toolbar rows: everything on one row when it fits (search needs >= 180px),
  // otherwise the search pill gets its own row, then the chips, then buttons.
  const bool oneRow = cw >= buttonsTotal + 8 + chipsTotal + 10 + 180;
  const bool chipsAndButtonsShareRow = cw >= chipsTotal + 16 + buttonsTotal;

  int searchTop = toolbarTop;
  int chipsTop = toolbarTop;
  int buttonsTop = toolbarTop;
  int rows = 1;
  if (!oneRow) {
    chipsTop = toolbarTop + toolbarHeight + rowGap;
    rows = 2;
    if (chipsAndButtonsShareRow) {
      buttonsTop = chipsTop;
    } else {
      buttonsTop = chipsTop + toolbarHeight + rowGap;
      rows = 3;
    }
  }
  const int toolbarBottom = toolbarTop + rows * toolbarHeight + (rows - 1) * rowGap;

  int x = right;
  auto place = [&](int width, int id) {
    RECT r{x - width, buttonsTop, x, buttonsTop + toolbarHeight};
    hits_.Add(r, id);
    x = r.left - 8;
  };
  place(clearWidth, kHitClear);
  place(exportWidth, kHitExport);
  place(copyWidth, kHitCopy);
  place(pauseWidth, kHitPause);

  if (oneRow) {
    chipsBox_ = RECT{x - chipsTotal, chipsTop, x, chipsTop + toolbarHeight};
  } else {
    chipsBox_ = RECT{cx, chipsTop, cx + chipsTotal, chipsTop + toolbarHeight};
  }
  int chipX = chipsBox_.left + 3;
  for (int i = 0; i < 4; ++i) {
    hits_.Add(RECT{chipX, chipsBox_.top + 3, chipX + chipWidth[i],
                   chipsBox_.bottom - 3},
              kHitChip + i);
    chipX += chipWidth[i];
  }

  const int searchRight = oneRow ? static_cast<int>(chipsBox_.left) - 10 : right;
  searchBox_ = RECT{cx, searchTop, std::max(searchRight, cx + 150),
                    searchTop + toolbarHeight};
  hits_.Add(searchBox_, kHitSearch);

  const int logTop = toolbarBottom + 14;
  log_.rect = RECT{cx, logTop, right, std::max(logTop + 80, height_ - 20 - 24)};
  log_.rowHeight = kRowHeight;
  log_.count = static_cast<int>(visible_rows_.size());
  footer_ = RECT{cx, log_.rect.bottom + 6, right, log_.rect.bottom + 26};

  if (searchEdit_) {
    MoveWindow(searchEdit_, searchBox_.left + 14, searchBox_.top + 9,
               std::max(40, static_cast<int>(searchBox_.right - searchBox_.left) - 28),
               20, TRUE);
  }
  log_.Clamp();
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

bool ConsoleSection::Matches(const Row& row) const {
  if (filter_ == 1 && row.decision != kBlockCode) return false;
  if (filter_ == 2 && row.decision != kAllowCode) return false;
  if (filter_ == 3 && row.decision != kErrorCode) return false;
  if (!query_.empty() && row.lower.find(query_) == std::wstring::npos) {
    return false;
  }
  return true;
}

void ConsoleSection::ApplyFilter() {
  visible_rows_.clear();
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (Matches(rows_[i])) visible_rows_.push_back(static_cast<int>(i));
  }
  log_.count = static_cast<int>(visible_rows_.size());
  hoverRow_ = -1;
  log_.Clamp();
}

void ConsoleSection::Refresh(bool force) {
  if (paused_ && !force) return;

  AdblockService& service = AdblockService::Get();
  const AdblockStats::Snapshot snapshot = service.Stats();
  const bool activeNow = service.blocking_active();
  const bool missingNow = !service.enabled();
  bool dirty = force;

  if (activeNow != enabled_) {
    enabled_ = activeNow;
    dirty = true;
  }
  if (missingNow != listsMissing_) {
    listsMissing_ = missingNow;
    Layout(area_);  // the "not blocking" notice changes the layout
    dirty = true;
  }

  blocked_ = snapshot.blocked;
  allowed_ = snapshot.allowed;
  errors_ = snapshot.errors;
  recorded_ = snapshot.events.size();

  const uint64_t revision =
      snapshot.blocked * 1000003ULL + snapshot.allowed * 10007ULL +
      snapshot.errors * 101ULL + snapshot.events.size() +
      (snapshot.events.empty() ? 0 : snapshot.events.back().timestamp_ms);

  if (force || revision != revision_) {
    revision_ = revision;
    dirty = true;
    rows_.clear();
    rows_.reserve(snapshot.events.size());
    for (const AdblockStats::Event& event : snapshot.events) {
      Row row;
      row.timestamp_ms = event.timestamp_ms;
      row.decision = event.decision == AdblockEngine::Decision::kBlock
                         ? kBlockCode
                         : event.decision == AdblockEngine::Decision::kError
                               ? kErrorCode
                               : kAllowCode;
      row.time = Timestamp(event.timestamp_ms);
      row.kind = Utf8ToWide(event.resource_type);
      row.url = Utf8ToWide(event.url);
      row.source = Utf8ToWide(event.source_url);
      row.lower = ToLower(row.url + L"\n" + row.source + L"\n" + row.kind);
      rows_.push_back(std::move(row));
    }
    ApplyFilter();
    if (stickToBottom_) log_.ScrollToBottom();
  }

  // Nothing changed since the last tick: leave the pixels alone.
  if (dirty) RepaintContent();
}

std::wstring ConsoleSection::BuildText() const {
  std::wstring text;
  for (int index : visible_rows_) {
    const Row& row = rows_[static_cast<size_t>(index)];
    text += RowLine(row.time, row.decision, row.kind, row.url, row.source);
    text += L"\r\n";
  }
  return text;
}

void ConsoleSection::CopyToClipboard() {
  if (visible_rows_.empty()) {
    Flash(L"Nothing to copy.");
    return;
  }
  const std::wstring text = BuildText();
  if (!OpenClipboard(host_->window())) {
    Flash(L"Could not open the clipboard.");
    return;
  }
  EmptyClipboard();
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
  bool ok = false;
  if (memory) {
    void* data = GlobalLock(memory);
    if (data) {
      std::memcpy(data, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
      GlobalUnlock(memory);
      ok = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    }
    if (!ok) GlobalFree(memory);  // the clipboard owns it only on success
  }
  CloseClipboard();
  Flash(ok ? L"Copied " + FormatCount(visible_rows_.size()) + L" event(s) to the clipboard."
           : L"Could not copy to the clipboard.");
}

void ConsoleSection::ExportToFile() {
  if (visible_rows_.empty()) {
    Flash(L"Nothing to export.");
    return;
  }
  OPENFILENAMEW dialog{};
  wchar_t path[MAX_PATH] = L"vibrium-console.log";
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = host_->window();
  dialog.lpstrFilter =
      L"Log files (*.log)\0*.log\0Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
  dialog.lpstrFile = path;
  dialog.nMaxFile = MAX_PATH;
  dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  dialog.lpstrDefExt = L"log";
  if (!GetSaveFileNameW(&dialog)) return;  // cancelled

  const std::string utf8 = WideToUtf8(BuildText());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    Flash(L"Could not write the file.");
    return;
  }
  file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
  Flash(L"Exported " + FormatCount(visible_rows_.size()) + L" event(s).");
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

int ConsoleSection::CursorKind() const {
  if (hoverId_ == kHitSearch) return 2;
  return hoverId_ != 0 ? 1 : 0;
}

void ConsoleSection::MouseMove(int x, int y) {
  if (log_.dragging) {
    if (log_.Drag(y)) {
      OnScrolled();
      RepaintRect(log_.rect);
    }
    return;
  }
  const int id = hits_.Test(x, y);
  const int row = log_.RowAt(x, y);
  if (id != hoverId_) {
    hoverId_ = id;
    RepaintContent();
  }
  if (row != hoverRow_) {
    hoverRow_ = row;
    RepaintRect(log_.rect);
    RepaintRect(footer_);
  }
}

void ConsoleSection::MouseLeave() {
  if (hoverId_ != 0 || hoverRow_ != -1) {
    hoverId_ = 0;
    hoverRow_ = -1;
    RepaintContent();
  }
}

bool ConsoleSection::MouseDown(int x, int y) {
  if (log_.BeginDrag(x, y)) {
    RepaintRect(log_.rect);
    return true;
  }
  pressedId_ = hits_.Test(x, y);
  return false;
}

void ConsoleSection::MouseUp(int x, int y) {
  if (log_.dragging) {
    log_.dragging = false;
    RepaintRect(log_.rect);
    return;
  }
  SettingsSection::MouseUp(x, y);
}

void ConsoleSection::CaptureLost() {
  if (log_.dragging) {
    log_.dragging = false;
    RepaintRect(log_.rect);
  }
}

void ConsoleSection::MouseWheel(int notches) {
  log_.Wheel(notches);
  OnScrolled();
  RepaintRect(log_.rect);
}

bool ConsoleSection::KeyDown(WPARAM key) {
  if (!log_.Key(key)) return false;
  OnScrolled();
  RepaintRect(log_.rect);
  return true;
}

bool ConsoleSection::Command(WPARAM wParam, LPARAM) {
  if (LOWORD(wParam) != kEditId) return false;
  const WORD code = HIWORD(wParam);
  if (code == EN_CHANGE) {
    const int length = GetWindowTextLengthW(searchEdit_);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) GetWindowTextW(searchEdit_, text.data(), length + 1);
    text.resize(static_cast<size_t>(std::max(length, 0)));
    query_ = ToLower(Trim(text));
    ApplyFilter();
    if (stickToBottom_) log_.ScrollToBottom();
    RepaintContent();
  } else if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
    searchFocused_ = code == EN_SETFOCUS;
    if (code == EN_KILLFOCUS && GetWindowTextLengthW(searchEdit_) == 0) {
      ShowWindow(searchEdit_, SW_HIDE);  // the page paints the hint
    }
    RepaintRect(searchBox_);
  }
  return true;
}

void ConsoleSection::OnClick(int id) {
  if (id >= kHitChip && id < kHitChip + 4) {
    filter_ = id - kHitChip;
    ApplyFilter();
    if (stickToBottom_) log_.ScrollToBottom();
    RepaintContent();
    return;
  }
  switch (id) {
    case kHitSearch:
      if (searchEdit_) {
        ShowWindow(searchEdit_, SW_SHOW);
        SetFocus(searchEdit_);
      }
      break;
    case kHitPause:
      paused_ = !paused_;
      Layout(area_);
      if (!paused_) Refresh(true);
      RepaintContent();
      break;
    case kHitCopy:
      CopyToClipboard();
      break;
    case kHitExport:
      ExportToFile();
      break;
    case kHitClear:
      AdblockService::Get().ClearStats();
      Refresh(true);
      Flash(L"Console cleared.");
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void ConsoleSection::Paint(HDC hdc) {
  const SettingsFonts& f = fonts();
  DrawLabel(hdc, L"Console", title_, f.title, kText,
            DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  DrawLabel(hdc, L"Live ad-blocking decisions for the pages you open.",
            subtitle_, f.font_body_15px, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

  // ---- stat cards
  const wchar_t* labels[5] = {L"AD BLOCKING", L"BLOCKED", L"ALLOWED", L"ERRORS",
                              L"RECENT EVENTS"};
  const std::wstring values[5] = {enabled_ ? L"Enabled" : L"Disabled",
                                  FormatCount(blocked_), FormatCount(allowed_),
                                  FormatCount(errors_), FormatCount(recorded_)};
  for (int i = 0; i < 5; ++i) {
    const RECT& r = cards_[i];
    FillRound(hdc, r, 14, kPanel, kBorder);
    DrawLabel(hdc, labels[i], RECT{r.left + 16, r.top + 12, r.right - 10, r.top + 30},
              f.font_small_12px, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    RECT valueRect{r.left + 16, r.top + 32, r.right - 10, r.bottom - 8};
    if (i == 0) {
      const COLORREF dot = enabled_ ? kGood : kWarn;
      FillRound(hdc, RECT{valueRect.left, valueRect.top + 11, valueRect.left + 10,
                          valueRect.top + 21},
                10, dot);
      valueRect.left += 20;
      DrawLabel(hdc, values[i], valueRect, f.brand, kText,
                DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    } else {
      DrawLabel(hdc, values[i], valueRect, f.stat, kText,
                DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    }
  }

  if (listsMissing_) {
    FillRound(hdc, notice_, 12, RGB(44, 36, 40), RGB(110, 90, 50));
    const std::wstring warning =
        L"No usable filter lists were found, so ads are not being blocked. "
        L"Lists download on first run; check your connection and restart Vibrium.";
    RECT inner{notice_.left + 14, notice_.top + 4, notice_.right - 14,
               notice_.bottom - 4};
    // DT_VCENTER ignores wrapped text, so measure and offset by hand.
    RECT measured = inner;
    HGDIOBJ previousFont = SelectObject(hdc, f.label);
    DrawTextW(hdc, warning.c_str(), static_cast<int>(warning.size()), &measured,
              DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(hdc, previousFont);
    const int textHeight = measured.bottom - measured.top;
    inner.top += std::max(0, (static_cast<int>(inner.bottom - inner.top) - textHeight) / 2);
    DrawLabel(hdc, warning, inner, f.label, kWarn, DT_WORDBREAK | DT_LEFT);
  }

  // ---- search pill (the EDIT control sits on top of it)
  FillRound(hdc, searchBox_, 14, kPanel2, searchFocused_ ? Theme::Accent() : kBorderStrong,
            searchFocused_ ? 2 : 1);
  if (!searchEdit_ || !IsWindowVisible(searchEdit_)) {
    DrawLabel(hdc, L"Search URL, source page, or resource type...",
              RECT{searchBox_.left + 16, searchBox_.top, searchBox_.right - 14,
                   searchBox_.bottom},
              f.font_body_15px, kMuted,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  }

  // ---- filter chips
  FillRound(hdc, chipsBox_, 14, kPanel2, kBorderStrong);
  for (int i = 0; i < 4; ++i) {
    const RECT r = hits_.RectOf(kHitChip + i);
    const bool selected = filter_ == i;
    const bool hover = hoverId_ == kHitChip + i;
    if (selected) {
      FillRound(hdc, r, 10, Theme::Accent());
    } else if (hover) {
      FillRound(hdc, r, 10, kSoft);
    }
    DrawLabel(hdc, kChipLabels[i], r, f.font_body_15px,
              selected ? RGB(255, 255, 255) : hover ? kText : kMuted,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  }

  // ---- buttons
  struct ButtonDef {
    int id;
    std::wstring label;
    bool primary;
    bool active;
  };
  const ButtonDef buttons[] = {{kHitPause, pauseLabel_, false, paused_},
                               {kHitCopy, L"Copy", false, false},
                               {kHitExport, L"Export", false, false},
                               {kHitClear, L"Clear", true, false}};
  for (const ButtonDef& button : buttons) {
    const RECT r = hits_.RectOf(button.id);
    const bool hover = hoverId_ == button.id;
    COLORREF fill = kPanel3;
    COLORREF border = kBorderStrong;
    if (button.primary) {
      fill = hover ? Theme::Mix(Theme::Accent(), RGB(255,255,255), 14) : Theme::Accent();
      border = fill;
    } else if (button.active) {
      fill = kSoft;
      border = Theme::Accent();
    } else if (hover) {
      fill = kSoftStrong;
      border = Theme::Accent();
    }
    FillRound(hdc, r, 14, fill, border);
    DrawLabel(hdc, button.label, r, f.font_body_15px, RGB(255, 255, 255),
              DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  }

  PaintLog(hdc);

  // ---- footer: flash message, hovered row, or summary
  std::wstring footer;
  if (!flash_.empty()) {
    footer = flash_;
  } else if (hoverRow_ >= 0 && hoverRow_ < static_cast<int>(visible_rows_.size())) {
    const Row& row = rows_[static_cast<size_t>(visible_rows_[static_cast<size_t>(hoverRow_)])];
    footer = row.url;
    if (!row.source.empty()) footer += L"   (from " + row.source + L")";
  } else {
    footer = FormatCount(visible_rows_.size()) + L" matching event" +
             (visible_rows_.size() == 1 ? L"" : L"s") + L" of " +
             FormatCount(rows_.size()) + L" recorded";
    if (paused_) footer += L"   -   paused, the list is not updating";
  }
  DrawLabel(hdc, footer, footer_, f.label,
            flash_.empty() ? kMuted : Theme::Accent(),
            DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
}

void ConsoleSection::PaintLog(HDC hdc) {
  const SettingsFonts& f = fonts();
  FillRound(hdc, log_.rect, 14, kLogBg, kBorder);

  const int saved = SaveDC(hdc);
  IntersectClipRect(hdc, log_.rect.left + 1, log_.rect.top + 4,
                    log_.rect.right - 1, log_.rect.bottom - 4);

  if (visible_rows_.empty()) {
    DrawLabel(hdc,
              rows_.empty()
                  ? L"No ad-blocking decisions recorded yet. Browse a website to generate events."
                  : L"No events match the current search.",
              RECT{log_.rect.left + 18, log_.rect.top + 14, log_.rect.right - 18,
                   log_.rect.top + 44},
              f.font_body_15px, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  } else {
    const int count = static_cast<int>(visible_rows_.size());
    const int first = std::max(0, log_.scrollY / kRowHeight);
    const int last = std::min(count - 1, (log_.scrollY + log_.ViewHeight()) / kRowHeight);
    const int base = log_.rect.left;
    const int urlLeft = base + 292;
    const int urlRight = log_.rect.right - kScrollbarWidth - 12;

    for (int i = first; i <= last; ++i) {
      const Row& row = rows_[static_cast<size_t>(visible_rows_[static_cast<size_t>(i)])];
      const int y = log_.rect.top + 4 + i * kRowHeight - log_.scrollY;
      if (i == hoverRow_) {
        RECT highlight{log_.rect.left + 1, y, log_.rect.right - 1, y + kRowHeight};
        HBRUSH tint = CreateSolidBrush(kSoft);
        FillRect(hdc, &highlight, tint);
        DeleteObject(tint);
      }
      const UINT flags = DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS;
      DrawLabel(hdc, row.time, RECT{base + 14, y, base + 112, y + kRowHeight},
                f.mono, kMuted, flags);
      DrawLabel(hdc, DecisionLabel(row.decision),
                RECT{base + 118, y, base + 198, y + kRowHeight}, f.mono,
                DecisionColor(row.decision), flags);
      DrawLabel(hdc, row.kind, RECT{base + 204, y, base + 286, y + kRowHeight},
                f.mono, kMuted, flags);

      if (urlRight > urlLeft) {
        DrawLabel(hdc, row.url, RECT{urlLeft, y, urlRight, y + kRowHeight},
                  f.mono, kText, flags);
        if (!row.source.empty()) {
          const int used = TextWidth(hdc, f.mono, row.url);
          const int sourceLeft = urlLeft + used + 10;
          if (sourceLeft + 60 < urlRight) {
            DrawLabel(hdc, L"(from " + row.source + L")",
                      RECT{sourceLeft, y, urlRight, y + kRowHeight}, f.mono,
                      kMuted, flags);
          }
        }
      }
    }
  }

  RestoreDC(hdc, saved);

  RECT thumb{};
  if (log_.Thumb(&thumb)) {
    FillRound(hdc, thumb, kScrollbarWidth, Theme::Accent());
  }
}

}  // namespace vibrium
