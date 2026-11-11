#include "ui/settings_page.h"

#include <commdlg.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <ctime>
#include <fstream>
#include <string>

#include "adblock/adblock_service.h"

namespace vibrium {
namespace {

constexpr wchar_t kClassName[] = L"VibriumSettingsPage";

constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT_PTR kFlashTimer = 2;
constexpr UINT kRefreshMs = 500;
constexpr UINT kFlashMs = 3500;
constexpr int kEditId = 5101;

// Same purple palette as the tab bar and address bar.
constexpr COLORREF kBg = RGB(20, 16, 31);
constexpr COLORREF kPanel = RGB(27, 22, 48);
constexpr COLORREF kPanel2 = RGB(35, 29, 51);
constexpr COLORREF kPanel3 = RGB(43, 35, 64);
constexpr COLORREF kLogBg = RGB(16, 13, 26);
constexpr COLORREF kBorder = RGB(53, 43, 82);
constexpr COLORREF kBorderStrong = RGB(77, 61, 112);
constexpr COLORREF kAccent = RGB(111, 76, 220);
constexpr COLORREF kAccentHover = RGB(132, 99, 242);
constexpr COLORREF kFocus = RGB(151, 111, 255);
constexpr COLORREF kSoft = RGB(46, 38, 78);       // hover tint
constexpr COLORREF kSoftStrong = RGB(53, 43, 82);
constexpr COLORREF kText = RGB(241, 237, 250);
constexpr COLORREF kMuted = RGB(157, 148, 177);
constexpr COLORREF kGood = RGB(95, 211, 154);
constexpr COLORREF kBad = RGB(255, 122, 144);
constexpr COLORREF kWarn = RGB(242, 184, 75);

// Hit ids.
constexpr int kHitNone = 0;
constexpr int kHitSection = 100;  // + section index
constexpr int kHitChip = 200;     // + filter (0..3)
constexpr int kHitPause = 300;
constexpr int kHitCopy = 301;
constexpr int kHitExport = 302;
constexpr int kHitClear = 303;
constexpr int kHitSearch = 400;

struct SectionDef {
  const wchar_t* name;
};
// To add a settings page: add an entry here, then handle its index in
// SettingsPage::Paint / OnClick (like the console).
constexpr SectionDef kSections[] = {{L"Console"}};
constexpr int kSectionCount =
    static_cast<int>(sizeof(kSections) / sizeof(kSections[0]));

constexpr const wchar_t* kChipLabels[4] = {L"All", L"Blocked", L"Allowed",
                                           L"Errors"};

constexpr int kSidebarWidth = 240;
constexpr int kPad = 32;
constexpr int kRowHeight = 22;
constexpr int kScrollbarWidth = 8;

// Decision codes stored in Row::decision.
constexpr int kAllowCode = 0;
constexpr int kBlockCode = 1;
constexpr int kErrorCode = 2;

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       value.data(),
                                       static_cast<int>(value.size()),
                                       nullptr, 0);
  if (size <= 0) return L"[unprintable URL]";
  std::wstring result(size, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), size);
  return result;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::wstring Timestamp(uint64_t milliseconds) {
  const std::time_t seconds = static_cast<std::time_t>(milliseconds / 1000);
  std::tm local{};
  localtime_s(&local, &seconds);
  wchar_t buffer[32]{};
  swprintf_s(buffer, L"%02d:%02d:%02d.%03llu", local.tm_hour, local.tm_min,
             local.tm_sec, static_cast<unsigned long long>(milliseconds % 1000));
  return buffer;
}

std::wstring FormatCount(uint64_t value) {
  std::wstring digits = std::to_wstring(value);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) {
    digits.insert(static_cast<size_t>(i), L",");
  }
  return digits;
}

std::wstring ToLower(std::wstring text) {
  if (!text.empty()) {
    CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
  }
  return text;
}

std::wstring Trim(const std::wstring& value) {
  const size_t first = value.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos) return L"";
  const size_t last = value.find_last_not_of(L" \t\r\n");
  return value.substr(first, last - first + 1);
}

bool InRect(const RECT& rect, int x, int y) {
  const POINT point{x, y};
  return PtInRect(&rect, point) != FALSE;
}

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

// ---- GDI helpers ----------------------------------------------------------

void FillRound(HDC hdc, const RECT& rect, int radius, COLORREF fill,
               COLORREF border = CLR_INVALID, int borderWidth = 1) {
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = border == CLR_INVALID
                 ? static_cast<HPEN>(GetStockObject(NULL_PEN))
                 : CreatePen(PS_SOLID, borderWidth, border);
  HGDIOBJ oldBrush = SelectObject(hdc, brush);
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
  SelectObject(hdc, oldBrush);
  SelectObject(hdc, oldPen);
  DeleteObject(brush);
  if (border != CLR_INVALID) DeleteObject(pen);
}

void DrawLabel(HDC hdc, const std::wstring& text, RECT rect, HFONT font,
               COLORREF color, UINT flags) {
  HGDIOBJ oldFont = SelectObject(hdc, font);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, color);
  DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rect,
            flags | DT_NOPREFIX);
  SelectObject(hdc, oldFont);
}

int TextWidth(HDC hdc, HFONT font, const std::wstring& text) {
  HGDIOBJ oldFont = SelectObject(hdc, font);
  SIZE size{};
  GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()),
                        &size);
  SelectObject(hdc, oldFont);
  return size.cx;
}

HFONT MakeFont(int pixelHeight, int weight, const wchar_t* face) {
  return CreateFontW(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

// ">_" terminal glyph, 18x18, top-left at (x, y).
void DrawTerminalIcon(HDC hdc, int x, int y, COLORREF color) {
  LOGBRUSH brush{};
  brush.lbStyle = BS_SOLID;
  brush.lbColor = color;
  HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND |
                              PS_JOIN_ROUND,
                          2, &brush, 0, nullptr);
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  const POINT chevron[] = {{x + 3, y + 4}, {x + 9, y + 9}, {x + 3, y + 14}};
  Polyline(hdc, chevron, 3);
  MoveToEx(hdc, x + 11, y + 15, nullptr);
  LineTo(hdc, x + 16, y + 15);
  SelectObject(hdc, oldPen);
  DeleteObject(pen);
}

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
  for (HFONT font : {fontUi_, fontSmall_, fontLabel_, fontTitle_, fontStat_,
                     fontBrand_, fontMono_}) {
    if (font) DeleteObject(font);
  }
  if (editBrush_) DeleteObject(editBrush_);
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
  Layout();
  return true;
}

void SettingsPage::CreateResources() {
  fontUi_ = MakeFont(15, FW_NORMAL, L"Segoe UI");
  fontSmall_ = MakeFont(12, FW_SEMIBOLD, L"Segoe UI");
  fontLabel_ = MakeFont(13, FW_NORMAL, L"Segoe UI");
  fontTitle_ = MakeFont(28, FW_SEMIBOLD, L"Segoe UI");
  fontStat_ = MakeFont(24, FW_SEMIBOLD, L"Segoe UI");
  fontBrand_ = MakeFont(18, FW_SEMIBOLD, L"Segoe UI");
  fontMono_ = MakeFont(13, FW_NORMAL, L"Consolas");
  editBrush_ = CreateSolidBrush(kPanel2);

  searchEdit_ = CreateWindowExW(
      0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 100, 20,
      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)), instance_,
      nullptr);
  if (searchEdit_) {
    SendMessageW(searchEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi_),
                 TRUE);
  }
}

void SettingsPage::Show(const RECT& bounds) {
  if (!hwnd_) return;
  MoveWindow(hwnd_, bounds.left, bounds.top, bounds.right - bounds.left,
             bounds.bottom - bounds.top, TRUE);
  ShowWindow(hwnd_, SW_SHOW);
  visible_ = true;
  SetTimer(hwnd_, kRefreshTimer, kRefreshMs, nullptr);
  Refresh(true);
  SetFocus(hwnd_);
}

void SettingsPage::Hide() {
  if (!hwnd_) return;
  visible_ = false;
  KillTimer(hwnd_, kRefreshTimer);
  KillTimer(hwnd_, kFlashTimer);
  ShowWindow(hwnd_, SW_HIDE);
}

void SettingsPage::Resize(const RECT& bounds) {
  if (!hwnd_ || !visible_) return;
  MoveWindow(hwnd_, bounds.left, bounds.top, bounds.right - bounds.left,
             bounds.bottom - bounds.top, TRUE);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

RECT SettingsPage::RectOf(int id) const {
  for (const Hit& hit : hits_) {
    if (hit.id == id) return hit.rect;
  }
  return RECT{};
}

int SettingsPage::ViewHeight() const {
  return std::max(0, static_cast<int>(logRect_.bottom - logRect_.top) - 8);
}

int SettingsPage::MaxScroll() const {
  const int content = static_cast<int>(visible_rows_.size()) * kRowHeight;
  return std::max(0, content - ViewHeight());
}

void SettingsPage::ClampScroll() {
  scrollY_ = std::clamp(scrollY_, 0, MaxScroll());
}

void SettingsPage::ScrollToBottom() { scrollY_ = MaxScroll(); }

// Call after the user moves the scroll position: remembers whether the view
// is pinned to the newest event.
void SettingsPage::OnScrolled() {
  ClampScroll();
  stickToBottom_ = scrollY_ >= MaxScroll() - 4;
}

void SettingsPage::Layout() {
  if (!hwnd_) return;
  RECT client{};
  GetClientRect(hwnd_, &client);
  width_ = client.right;
  height_ = client.bottom;
  hits_.clear();

  sidebar_ = RECT{0, 0, kSidebarWidth, height_};
  for (int i = 0; i < kSectionCount; ++i) {
    hits_.push_back({RECT{14, 76 + i * 46, kSidebarWidth - 14, 76 + i * 46 + 42},
                     kHitSection + i});
  }

  const int cx = kSidebarWidth + kPad;
  const int cw = std::max(360, width_ - cx - kPad);
  const int right = cx + cw;

  title_ = RECT{cx, 24, right, 62};
  subtitle_ = RECT{cx, 62, right, 84};

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
  if (!enabled_) {
    notice_ = RECT{cx, y + 12, right, y + 12 + 42};
    y = notice_.bottom;
  }

  const int toolbarTop = y + 16;
  const int toolbarHeight = 38;
  const int rowGap = 10;

  HDC dc = GetDC(hwnd_);
  auto buttonWidth = [&](const std::wstring& text) {
    return TextWidth(dc, fontUi_, text) + 26;
  };
  pauseLabel_ = paused_ ? L"Resume" : L"Pause";
  const int pauseWidth = std::max(buttonWidth(L"Pause"), buttonWidth(L"Resume"));
  const int copyWidth = buttonWidth(L"Copy");
  const int exportWidth = buttonWidth(L"Export");
  const int clearWidth = buttonWidth(L"Clear");
  int chipWidth[4];
  int chipsTotal = 6;
  for (int i = 0; i < 4; ++i) {
    chipWidth[i] = TextWidth(dc, fontUi_, kChipLabels[i]) + 22;
    chipsTotal += chipWidth[i];
  }
  ReleaseDC(hwnd_, dc);

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
    hits_.push_back({r, id});
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
    hits_.push_back({RECT{chipX, chipsBox_.top + 3, chipX + chipWidth[i],
                          chipsBox_.bottom - 3},
                     kHitChip + i});
    chipX += chipWidth[i];
  }

  const int searchRight = oneRow ? static_cast<int>(chipsBox_.left) - 10 : right;
  searchBox_ = RECT{cx, searchTop, std::max(searchRight, cx + 150),
                    searchTop + toolbarHeight};
  hits_.push_back({searchBox_, kHitSearch});

  const int logTop = toolbarBottom + 14;
  logRect_ = RECT{cx, logTop, right, std::max(logTop + 80, height_ - 20 - 24)};
  footer_ = RECT{cx, logRect_.bottom + 6, right, logRect_.bottom + 26};

  if (searchEdit_) {
    MoveWindow(searchEdit_, searchBox_.left + 14, searchBox_.top + 9,
               std::max(40, static_cast<int>(searchBox_.right - searchBox_.left) - 28),
               20, TRUE);
  }
  ClampScroll();
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

bool SettingsPage::Matches(const Row& row) const {
  if (filter_ == 1 && row.decision != kBlockCode) return false;
  if (filter_ == 2 && row.decision != kAllowCode) return false;
  if (filter_ == 3 && row.decision != kErrorCode) return false;
  if (!query_.empty() && row.lower.find(query_) == std::wstring::npos) {
    return false;
  }
  return true;
}

void SettingsPage::ApplyFilter() {
  visible_rows_.clear();
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (Matches(rows_[i])) visible_rows_.push_back(static_cast<int>(i));
  }
  hoverRow_ = -1;
  ClampScroll();
}

void SettingsPage::Refresh(bool force) {
  if (!hwnd_) return;
  if (paused_ && !force) return;

  const AdblockStats::Snapshot snapshot = AdblockService::Get().Stats();
  const bool enabledNow = AdblockService::Get().enabled();

  if (enabledNow != enabled_) {
    enabled_ = enabledNow;
    Layout();  // the "not blocking" notice changes the layout
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
    if (stickToBottom_) ScrollToBottom();
  }

  InvalidateContent();
}

void SettingsPage::InvalidateContent() {
  if (!hwnd_) return;
  RECT content{kSidebarWidth, 0, width_, height_};
  InvalidateRect(hwnd_, &content, FALSE);
}

void SettingsPage::Flash(const std::wstring& message) {
  flash_ = message;
  SetTimer(hwnd_, kFlashTimer, kFlashMs, nullptr);
  InvalidateRect(hwnd_, &footer_, FALSE);
}

std::wstring SettingsPage::BuildText() const {
  std::wstring text;
  for (int index : visible_rows_) {
    const Row& row = rows_[static_cast<size_t>(index)];
    text += RowLine(row.time, row.decision, row.kind, row.url, row.source);
    text += L"\r\n";
  }
  return text;
}

void SettingsPage::CopyToClipboard() {
  if (visible_rows_.empty()) {
    Flash(L"Nothing to copy.");
    return;
  }
  const std::wstring text = BuildText();
  if (!OpenClipboard(hwnd_)) {
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

void SettingsPage::ExportToFile() {
  if (visible_rows_.empty()) {
    Flash(L"Nothing to export.");
    return;
  }
  OPENFILENAMEW dialog{};
  wchar_t path[MAX_PATH] = L"vibrium-console.log";
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = hwnd_;
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

int SettingsPage::HitTest(int x, int y) const {
  for (const Hit& hit : hits_) {
    if (InRect(hit.rect, x, y)) return hit.id;
  }
  return kHitNone;
}

int SettingsPage::RowAt(int x, int y) const {
  RECT area = logRect_;
  area.right -= kScrollbarWidth + 8;
  if (!InRect(area, x, y)) return -1;
  const int local = y - (logRect_.top + 4) + scrollY_;
  if (local < 0) return -1;
  const int index = local / kRowHeight;
  return index < static_cast<int>(visible_rows_.size()) ? index : -1;
}

bool SettingsPage::ThumbRect(RECT* rect) const {
  const int viewHeight = ViewHeight();
  const int content = static_cast<int>(visible_rows_.size()) * kRowHeight;
  if (viewHeight <= 0 || content <= viewHeight) return false;
  const int thumbHeight = std::max(32, viewHeight * viewHeight / content);
  const int travel = viewHeight - thumbHeight;
  const int maxScroll = content - viewHeight;
  const int top = logRect_.top + 4 + (maxScroll > 0 ? travel * scrollY_ / maxScroll : 0);
  *rect = RECT{logRect_.right - kScrollbarWidth - 4, top,
               logRect_.right - 4, top + thumbHeight};
  return true;
}

void SettingsPage::OnClick(int id) {
  if (id >= kHitSection && id < kHitSection + kSectionCount) {
    activeSection_ = id - kHitSection;
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  if (id >= kHitChip && id < kHitChip + 4) {
    filter_ = id - kHitChip;
    ApplyFilter();
    if (stickToBottom_) ScrollToBottom();
    InvalidateContent();
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
      Layout();
      if (!paused_) Refresh(true);
      InvalidateContent();
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

void SettingsPage::Paint(HDC hdc, const RECT& client) {
  HBRUSH background = CreateSolidBrush(kBg);
  FillRect(hdc, &client, background);
  DeleteObject(background);

  PaintSidebar(hdc);
  if (activeSection_ == 0) PaintConsole(hdc);
}

void SettingsPage::PaintSidebar(HDC hdc) {
  HBRUSH panel = CreateSolidBrush(kPanel);
  FillRect(hdc, &sidebar_, panel);
  DeleteObject(panel);

  HPEN border = CreatePen(PS_SOLID, 1, kBorder);
  HGDIOBJ oldPen = SelectObject(hdc, border);
  MoveToEx(hdc, kSidebarWidth - 1, 0, nullptr);
  LineTo(hdc, kSidebarWidth - 1, height_);
  SelectObject(hdc, oldPen);
  DeleteObject(border);

  FillRound(hdc, RECT{22, 26, 44, 48}, 8, kAccent);
  DrawLabel(hdc, L"Settings", RECT{56, 22, kSidebarWidth - 14, 52}, fontBrand_,
            kText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

  for (int i = 0; i < kSectionCount; ++i) {
    const RECT r = RectOf(kHitSection + i);
    const bool active = i == activeSection_;
    const bool hover = hoverId_ == kHitSection + i;
    if (active) {
      FillRound(hdc, r, 12, kAccent);
    } else if (hover) {
      FillRound(hdc, r, 12, kSoft);
    }
    const COLORREF color = active ? RGB(255, 255, 255) : hover ? kText : kMuted;
    DrawTerminalIcon(hdc, r.left + 14, r.top + 12, color);
    DrawLabel(hdc, kSections[i].name,
              RECT{r.left + 44, r.top, r.right - 8, r.bottom}, fontUi_, color,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  }
}

void SettingsPage::PaintConsole(HDC hdc) {
  DrawLabel(hdc, L"Console", title_, fontTitle_, kText,
            DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  DrawLabel(hdc, L"Live ad-blocking decisions for the pages you open.",
            subtitle_, fontUi_, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

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
              fontSmall_, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    RECT valueRect{r.left + 16, r.top + 32, r.right - 10, r.bottom - 8};
    if (i == 0) {
      const COLORREF dot = enabled_ ? kGood : kWarn;
      FillRound(hdc, RECT{valueRect.left, valueRect.top + 11, valueRect.left + 10,
                          valueRect.top + 21},
                10, dot);
      valueRect.left += 20;
      DrawLabel(hdc, values[i], valueRect, fontBrand_, kText,
                DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    } else {
      DrawLabel(hdc, values[i], valueRect, fontStat_, kText,
                DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    }
  }

  if (!enabled_) {
    FillRound(hdc, notice_, 12, RGB(44, 36, 40), RGB(110, 90, 50));
    const std::wstring warning =
        L"No usable filter lists were found, so ads are not being blocked. "
        L"Lists download on first run; check your connection and restart Vibrium.";
    RECT inner{notice_.left + 14, notice_.top + 4, notice_.right - 14,
               notice_.bottom - 4};
    // DT_VCENTER ignores wrapped text, so measure and offset by hand.
    RECT measured = inner;
    HGDIOBJ previousFont = SelectObject(hdc, fontLabel_);
    DrawTextW(hdc, warning.c_str(), static_cast<int>(warning.size()), &measured,
              DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    SelectObject(hdc, previousFont);
    const int textHeight = measured.bottom - measured.top;
    inner.top += std::max(0, (static_cast<int>(inner.bottom - inner.top) - textHeight) / 2);
    DrawLabel(hdc, warning, inner, fontLabel_, kWarn, DT_WORDBREAK | DT_LEFT);
  }

  // ---- search pill (the EDIT control sits on top of it)
  FillRound(hdc, searchBox_, 14, kPanel2, searchFocused_ ? kFocus : kBorderStrong,
            searchFocused_ ? 2 : 1);
  if (!searchEdit_ || !IsWindowVisible(searchEdit_)) {
    DrawLabel(hdc, L"Search URL, source page, or resource type...",
              RECT{searchBox_.left + 16, searchBox_.top, searchBox_.right - 14,
                   searchBox_.bottom},
              fontUi_, kMuted,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  }

  // ---- filter chips
  FillRound(hdc, chipsBox_, 14, kPanel2, kBorderStrong);
  for (int i = 0; i < 4; ++i) {
    const RECT r = RectOf(kHitChip + i);
    const bool selected = filter_ == i;
    const bool hover = hoverId_ == kHitChip + i;
    if (selected) {
      FillRound(hdc, r, 10, kAccent);
    } else if (hover) {
      FillRound(hdc, r, 10, kSoft);
    }
    DrawLabel(hdc, kChipLabels[i], r, fontUi_,
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
    const RECT r = RectOf(button.id);
    const bool hover = hoverId_ == button.id;
    COLORREF fill = kPanel3;
    COLORREF border = kBorderStrong;
    if (button.primary) {
      fill = hover ? kAccentHover : kAccent;
      border = fill;
    } else if (button.active) {
      fill = kSoft;
      border = kFocus;
    } else if (hover) {
      fill = kSoftStrong;
      border = kFocus;
    }
    FillRound(hdc, r, 14, fill, border);
    DrawLabel(hdc, button.label, r, fontUi_, RGB(255, 255, 255),
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
  DrawLabel(hdc, footer, footer_, fontLabel_,
            flash_.empty() ? kMuted : kFocus,
            DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
}

void SettingsPage::PaintLog(HDC hdc) {
  FillRound(hdc, logRect_, 14, kLogBg, kBorder);

  const int saved = SaveDC(hdc);
  IntersectClipRect(hdc, logRect_.left + 1, logRect_.top + 4,
                    logRect_.right - 1, logRect_.bottom - 4);

  if (visible_rows_.empty()) {
    DrawLabel(hdc,
              rows_.empty()
                  ? L"No ad-blocking decisions recorded yet. Browse a website to generate events."
                  : L"No events match the current search.",
              RECT{logRect_.left + 18, logRect_.top + 14, logRect_.right - 18,
                   logRect_.top + 44},
              fontUi_, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  } else {
    const int count = static_cast<int>(visible_rows_.size());
    const int first = std::max(0, scrollY_ / kRowHeight);
    const int last = std::min(count - 1, (scrollY_ + ViewHeight()) / kRowHeight);
    const int base = logRect_.left;
    const int urlLeft = base + 292;
    const int urlRight = logRect_.right - kScrollbarWidth - 12;

    for (int i = first; i <= last; ++i) {
      const Row& row = rows_[static_cast<size_t>(visible_rows_[static_cast<size_t>(i)])];
      const int y = logRect_.top + 4 + i * kRowHeight - scrollY_;
      if (i == hoverRow_) {
        RECT highlight{logRect_.left + 1, y, logRect_.right - 1, y + kRowHeight};
        HBRUSH tint = CreateSolidBrush(RGB(33, 27, 54));
        FillRect(hdc, &highlight, tint);
        DeleteObject(tint);
      }
      const UINT flags = DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS;
      DrawLabel(hdc, row.time, RECT{base + 14, y, base + 112, y + kRowHeight},
                fontMono_, kMuted, flags);
      DrawLabel(hdc, DecisionLabel(row.decision),
                RECT{base + 118, y, base + 198, y + kRowHeight}, fontMono_,
                DecisionColor(row.decision), flags);
      DrawLabel(hdc, row.kind, RECT{base + 204, y, base + 286, y + kRowHeight},
                fontMono_, kMuted, flags);

      if (urlRight > urlLeft) {
        DrawLabel(hdc, row.url, RECT{urlLeft, y, urlRight, y + kRowHeight},
                  fontMono_, kText, flags);
        if (!row.source.empty()) {
          const int used = TextWidth(hdc, fontMono_, row.url);
          const int sourceLeft = urlLeft + used + 10;
          if (sourceLeft + 60 < urlRight) {
            DrawLabel(hdc, L"(from " + row.source + L")",
                      RECT{sourceLeft, y, urlRight, y + kRowHeight}, fontMono_,
                      kMuted, flags);
          }
        }
      }
    }
  }

  RestoreDC(hdc, saved);

  RECT thumb{};
  if (ThumbRect(&thumb)) {
    const bool hot = draggingThumb_;
    FillRound(hdc, thumb, kScrollbarWidth, hot ? kFocus : RGB(85, 66, 138));
  }
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
        HDC memory = CreateCompatibleDC(hdc);
        HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
        HGDIOBJ oldBitmap = SelectObject(memory, bitmap);
        Paint(memory, client);
        BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
               ps.rcPaint.right - ps.rcPaint.left,
               ps.rcPaint.bottom - ps.rcPaint.top, memory, ps.rcPaint.left,
               ps.rcPaint.top, SRCCOPY);
        SelectObject(memory, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memory);
      }
      EndPaint(hwnd_, &ps);
      return 0;
    }

    case WM_SIZE:
      Layout();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;

    case WM_TIMER:
      if (wParam == kRefreshTimer) {
        Refresh(false);
      } else if (wParam == kFlashTimer) {
        KillTimer(hwnd_, kFlashTimer);
        flash_.clear();
        InvalidateRect(hwnd_, &footer_, FALSE);
      }
      return 0;

    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      SetTextColor(dc, kText);
      SetBkColor(dc, kPanel2);
      return reinterpret_cast<LRESULT>(editBrush_);
    }

    case WM_COMMAND:
      if (LOWORD(wParam) == kEditId) {
        const WORD code = HIWORD(wParam);
        if (code == EN_CHANGE) {
          const int length = GetWindowTextLengthW(searchEdit_);
          std::wstring text(static_cast<size_t>(length) + 1, L'\0');
          if (length > 0) GetWindowTextW(searchEdit_, text.data(), length + 1);
          text.resize(static_cast<size_t>(std::max(length, 0)));
          query_ = ToLower(Trim(text));
          ApplyFilter();
          if (stickToBottom_) ScrollToBottom();
          InvalidateContent();
        } else if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
          searchFocused_ = code == EN_SETFOCUS;
          if (code == EN_KILLFOCUS && GetWindowTextLengthW(searchEdit_) == 0) {
            ShowWindow(searchEdit_, SW_HIDE);  // the page paints the hint
          }
          InvalidateRect(hwnd_, &searchBox_, FALSE);
        }
      }
      return 0;

    case WM_MOUSEMOVE: {
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));

      if (draggingThumb_) {
        RECT thumb{};
        const int viewHeight = ViewHeight();
        const int content = static_cast<int>(visible_rows_.size()) * kRowHeight;
        if (ThumbRect(&thumb) && content > viewHeight) {
          const int thumbHeight = thumb.bottom - thumb.top;
          const int travel = viewHeight - thumbHeight;
          if (travel > 0) {
            scrollY_ = dragStartScroll_ +
                       (y - dragStartY_) * (content - viewHeight) / travel;
            OnScrolled();
            InvalidateRect(hwnd_, &logRect_, FALSE);
          }
        }
        return 0;
      }

      const int id = HitTest(x, y);
      const int row = RowAt(x, y);
      if (id != hoverId_) {
        hoverId_ = id;
        InvalidateContent();
        InvalidateRect(hwnd_, &sidebar_, FALSE);
      }
      if (row != hoverRow_) {
        hoverRow_ = row;
        InvalidateRect(hwnd_, &logRect_, FALSE);
        InvalidateRect(hwnd_, &footer_, FALSE);
      }
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
      if (hoverId_ != kHitNone || hoverRow_ != -1) {
        hoverId_ = kHitNone;
        hoverRow_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;

    case WM_SETCURSOR:
      if (LOWORD(lParam) == HTCLIENT && hoverId_ != kHitNone) {
        SetCursor(LoadCursorW(nullptr, hoverId_ == kHitSearch ? IDC_IBEAM
                                                              : IDC_HAND));
        return TRUE;
      }
      break;

    case WM_LBUTTONDOWN: {
      SetFocus(hwnd_);
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      RECT thumb{};
      if (ThumbRect(&thumb) && InRect(thumb, x, y)) {
        draggingThumb_ = true;
        dragStartY_ = y;
        dragStartScroll_ = scrollY_;
        SetCapture(hwnd_);
        InvalidateRect(hwnd_, &logRect_, FALSE);
        return 0;
      }
      pressedId_ = HitTest(x, y);
      return 0;
    }

    case WM_LBUTTONUP: {
      if (draggingThumb_) {
        draggingThumb_ = false;
        ReleaseCapture();
        InvalidateRect(hwnd_, &logRect_, FALSE);
        return 0;
      }
      const int x = static_cast<short>(LOWORD(lParam));
      const int y = static_cast<short>(HIWORD(lParam));
      const int id = HitTest(x, y);
      if (pressedId_ != kHitNone && pressedId_ == id) OnClick(id);
      pressedId_ = kHitNone;
      return 0;
    }

    case WM_CAPTURECHANGED:
      if (draggingThumb_) {
        draggingThumb_ = false;
        InvalidateRect(hwnd_, &logRect_, FALSE);
      }
      return 0;

    case WM_MOUSEWHEEL: {
      const int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
      scrollY_ -= notches * 3 * kRowHeight;
      OnScrolled();
      InvalidateRect(hwnd_, &logRect_, FALSE);
      return 0;
    }

    case WM_KEYDOWN: {
      const int page = std::max(kRowHeight, ViewHeight() - kRowHeight);
      switch (wParam) {
        case VK_UP:    scrollY_ -= kRowHeight; break;
        case VK_DOWN:  scrollY_ += kRowHeight; break;
        case VK_PRIOR: scrollY_ -= page; break;
        case VK_NEXT:  scrollY_ += page; break;
        case VK_HOME:  scrollY_ = 0; break;
        case VK_END:   scrollY_ = MaxScroll(); break;
        default:
          return DefWindowProcW(hwnd_, message, wParam, lParam);
      }
      OnScrolled();
      InvalidateRect(hwnd_, &logRect_, FALSE);
      return 0;
    }

    case WM_NCDESTROY: {
      HWND old = hwnd_;
      SetWindowLongPtrW(old, GWLP_USERDATA, 0);
      hwnd_ = nullptr;
      searchEdit_ = nullptr;
      return DefWindowProcW(old, message, wParam, lParam);
    }

    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wParam, lParam);
}

}  // namespace vibrium
