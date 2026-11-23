#include "history.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "theme.h"

namespace vibrium {
namespace {

constexpr size_t kMaxEntries = 20000;
constexpr uint64_t kDuplicateWindowMs = 5000;
constexpr int kEditId = 6101;

constexpr int kPad = 32;
constexpr int kMaxContentWidth = 1000;
constexpr int kHeaderHeight = 104;
constexpr int kRowHeight = 52;
constexpr int kDayHeaderHeight = 40;

constexpr int kHitClear = 10;
constexpr int kHitRowBase = 1000;
constexpr int kHitDeleteBase = 1000000;

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

std::wstring ToLower(std::wstring value) {
  for (auto& ch : value) ch = static_cast<wchar_t>(std::towlower(ch));
  return value;
}

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()),
                                       nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(size, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size);
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

// Tabs and line breaks would break the one-line-per-visit file format.
std::wstring OneLine(std::wstring value) {
  for (auto& ch : value) {
    if (ch == L'\t' || ch == L'\r' || ch == L'\n') ch = L' ';
  }
  return value;
}

uint64_t NowMs() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(system_clock::now().time_since_epoch())
          .count());
}

std::tm LocalTime(uint64_t timeMs) {
  const std::time_t seconds = static_cast<std::time_t>(timeMs / 1000);
  std::tm local{};
  localtime_s(&local, &seconds);
  return local;
}

bool SameDay(const std::tm& a, const std::tm& b) {
  return a.tm_year == b.tm_year && a.tm_yday == b.tm_yday;
}

std::wstring DayLabel(uint64_t timeMs) {
  const std::tm day = LocalTime(timeMs);
  const uint64_t now = NowMs();
  if (SameDay(day, LocalTime(now))) return L"Today";
  if (SameDay(day, LocalTime(now - 24ULL * 3600 * 1000))) return L"Yesterday";
  wchar_t buffer[96]{};
  wcsftime(buffer, std::size(buffer), L"%A, %B %d, %Y", &day);
  return buffer;
}

std::wstring TimeLabel(uint64_t timeMs) {
  const std::tm local = LocalTime(timeMs);
  wchar_t buffer[32]{};
  wcsftime(buffer, std::size(buffer), L"%I:%M %p", &local);
  return buffer;
}

struct HeaderLayout {
  int left = 0;
  int right = 0;
  RECT title{};
  RECT search{};
  RECT clear{};
};

HeaderLayout ComputeHeader(int width) {
  HeaderLayout h;
  const int contentWidth = std::min(width - 2 * kPad, kMaxContentWidth);
  h.left = std::max(kPad, (width - contentWidth) / 2);
  h.right = h.left + std::max(0, contentWidth);
  h.title = RECT{h.left, 28, h.right, 72};
  const int searchLeft = std::max(h.left, h.right - 300);
  h.search = RECT{searchLeft, 34, h.right, 70};
  h.clear = RECT{std::max(h.left, searchLeft - 12 - 190), 34,
                 std::max(h.left, searchLeft - 12), 70};
  return h;
}

}  // namespace

// ---------------------------------------------------------------------------
// URL helper
// ---------------------------------------------------------------------------

bool IsHistoryUrl(const std::wstring& url) {
  const size_t length = std::wstring(kHistoryUrl).size();
  if (url.size() < length) return false;
  if (ToLower(url.substr(0, length)) != kHistoryUrl) return false;
  if (url.size() == length) return true;
  const wchar_t next = url[length];
  return next == L'/' || next == L'#' || next == L'?';
}

// ---------------------------------------------------------------------------
// HistoryStore
// ---------------------------------------------------------------------------

HistoryStore& HistoryStore::Get() {
  static HistoryStore store;
  return store;
}

std::wstring HistoryStore::FilePath() const {
  const wchar_t* base = _wgetenv(L"LOCALAPPDATA");
  if (!base) return L"";
  std::error_code ec;
  const std::filesystem::path dir = std::filesystem::path(base) / L"Vibrium";
  std::filesystem::create_directories(dir, ec);
  return (dir / L"history.tsv").wstring();
}

bool HistoryStore::ShouldRecord(const std::wstring& url) {
  const std::wstring lower = ToLower(url.substr(0, 8));
  return lower.rfind(L"http://", 0) == 0 || lower.rfind(L"https://", 0) == 0;
}

void HistoryStore::Load() {
  if (loaded_) return;
  loaded_ = true;

  const std::wstring path = FilePath();
  if (path.empty()) return;

  std::ifstream in{std::filesystem::path(path), std::ios::binary};
  if (!in) return;

  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;

    const size_t first = line.find('\t');
    if (first == std::string::npos) continue;
    const size_t second = line.find('\t', first + 1);

    HistoryEntry entry;
    entry.timeMs = std::strtoull(line.substr(0, first).c_str(), nullptr, 10);
    if (second == std::string::npos) {
      entry.url = Utf8ToWide(line.substr(first + 1));
    } else {
      entry.url = Utf8ToWide(line.substr(first + 1, second - first - 1));
      entry.title = Utf8ToWide(line.substr(second + 1));
    }
    if (entry.url.empty() || entry.timeMs == 0) continue;
    entries_.push_back(std::move(entry));
  }

  if (entries_.size() > kMaxEntries) {
    entries_.erase(entries_.begin(),
                   entries_.begin() + (entries_.size() - kMaxEntries));
    dirty_ = true;
  }
  ++revision_;
}

void HistoryStore::Append(const HistoryEntry& entry) const {
  const std::wstring path = FilePath();
  if (path.empty()) return;
  std::ofstream out{std::filesystem::path(path),
                    std::ios::binary | std::ios::app};
  if (!out) return;
  out << entry.timeMs << '\t' << WideToUtf8(OneLine(entry.url)) << '\t'
      << WideToUtf8(OneLine(entry.title)) << '\n';
}

void HistoryStore::Rewrite() const {
  const std::wstring path = FilePath();
  if (path.empty()) return;
  std::ofstream out{std::filesystem::path(path),
                    std::ios::binary | std::ios::trunc};
  if (!out) return;
  for (const HistoryEntry& entry : entries_) {
    out << entry.timeMs << '\t' << WideToUtf8(OneLine(entry.url)) << '\t'
        << WideToUtf8(OneLine(entry.title)) << '\n';
  }
}

void HistoryStore::AddVisit(const std::wstring& url, const std::wstring& title) {
  if (!ShouldRecord(url)) return;
  const uint64_t now = NowMs();

  if (!entries_.empty()) {
    HistoryEntry& last = entries_.back();
    if (last.url == url && now - last.timeMs < kDuplicateWindowMs) {
      last.timeMs = now;
      if (!title.empty()) last.title = title;
      dirty_ = true;
      ++revision_;
      return;
    }
  }

  HistoryEntry entry;
  entry.timeMs = now;
  entry.url = url;
  entry.title = title;
  Append(entry);
  entries_.push_back(std::move(entry));

  if (entries_.size() > kMaxEntries) {
    entries_.erase(entries_.begin(), entries_.begin() + 1000);
    dirty_ = true;
  }
  ++revision_;
}

void HistoryStore::UpdateTitle(const std::wstring& url,
                               const std::wstring& title) {
  if (title.empty()) return;
  size_t checked = 0;
  for (auto it = entries_.rbegin(); it != entries_.rend() && checked < 20;
       ++it, ++checked) {
    if (it->url == url) {
      if (it->title != title) {
        it->title = title;
        dirty_ = true;
        ++revision_;
      }
      return;
    }
  }
}

void HistoryStore::RemoveAt(size_t index) {
  if (index >= entries_.size()) return;
  entries_.erase(entries_.begin() + index);
  Rewrite();
  dirty_ = false;
  ++revision_;
}

void HistoryStore::Clear() {
  entries_.clear();
  Rewrite();
  dirty_ = false;
  ++revision_;
}

void HistoryStore::Flush() {
  if (!dirty_) return;
  Rewrite();
  dirty_ = false;
}

// ---------------------------------------------------------------------------
// HistoryPage
// ---------------------------------------------------------------------------

HistoryPage::~HistoryPage() {
  if (editBrush_) DeleteObject(editBrush_);
}

void HistoryPage::OnCreated() {
  editBrush_ = CreateSolidBrush(pageui::kPanel2);
  searchEdit_ = CreateWindowExW(
      0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 100, 20,
      hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)),
      instance_, nullptr);
  if (searchEdit_) {
    SendMessageW(searchEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(fontUi_),
                 TRUE);
    SendMessageW(searchEdit_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Search history"));
  }
}

void HistoryPage::OnShown() {
  rebuild_ = true;
}

void HistoryPage::OnResized(int width, int /*height*/) {
  LayoutSearch(width);
}

void HistoryPage::LayoutSearch(int width) {
  if (!searchEdit_) return;
  const HeaderLayout h = ComputeHeader(width);
  searchBox_ = h.search;
  MoveWindow(searchEdit_, h.search.left + 38, h.search.top + 8,
             std::max(20, static_cast<int>(h.search.right - h.search.left) - 38 - 14),
             20, TRUE);
}

bool HistoryPage::OnTick() {
  if (builtRevision_ != HistoryStore::Get().revision()) {
    rebuild_ = true;
    return true;
  }
  return false;
}

bool HistoryPage::OnMessage(UINT message, WPARAM wParam, LPARAM lParam,
                            LRESULT* result) {
  if (message == WM_COMMAND && LOWORD(wParam) == kEditId &&
      HIWORD(wParam) == EN_CHANGE && searchEdit_) {
    wchar_t buffer[256]{};
    GetWindowTextW(searchEdit_, buffer, static_cast<int>(std::size(buffer)));
    query_ = ToLower(buffer);
    rebuild_ = true;
    ScrollToTop();
    *result = 0;
    return true;
  }

  if (message == WM_CTLCOLOREDIT &&
      reinterpret_cast<HWND>(lParam) == searchEdit_) {
    HDC dc = reinterpret_cast<HDC>(wParam);
    SetTextColor(dc, pageui::kText);
    SetBkColor(dc, pageui::kPanel2);
    *result = reinterpret_cast<LRESULT>(editBrush_);
    return true;
  }
  return false;
}

void HistoryPage::Rebuild() {
  rebuild_ = false;
  HistoryStore& store = HistoryStore::Get();
  builtRevision_ = store.revision();
  rows_.clear();

  const auto& entries = store.entries();
  std::tm lastDay{};
  bool haveDay = false;

  for (size_t i = entries.size(); i-- > 0;) {
    const HistoryEntry& entry = entries[i];
    if (!query_.empty()) {
      const std::wstring haystack = ToLower(entry.title + L" " + entry.url);
      if (haystack.find(query_) == std::wstring::npos) continue;
    }

    const std::tm day = LocalTime(entry.timeMs);
    if (!haveDay || !SameDay(day, lastDay)) {
      Row header;
      header.header = true;
      header.text = DayLabel(entry.timeMs);
      rows_.push_back(std::move(header));
      lastDay = day;
      haveDay = true;
    }

    Row row;
    row.entryIndex = i;
    rows_.push_back(std::move(row));
  }
}

void HistoryPage::Paint(HDC hdc, const RECT& client) {
  if (rebuild_) Rebuild();

  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  const HeaderLayout head = ComputeHeader(width);
  const COLORREF accent = Theme::Accent();

  // ---- header (fixed) ----
  DrawLabel(hdc, L"History", head.title, fontTitle_, pageui::kText,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  FillRound(hdc, head.search, 18, pageui::kPanel2, pageui::kBorder);
  {
    // Magnifier icon.
    const int cx = head.search.left + 20;
    const int cy = (head.search.top + head.search.bottom) / 2 - 1;
    HPEN pen = CreatePen(PS_SOLID, 2, pageui::kMuted);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Ellipse(hdc, cx - 6, cy - 6, cx + 5, cy + 5);
    MoveToEx(hdc, cx + 4, cy + 4, nullptr);
    LineTo(hdc, cx + 8, cy + 8);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
  }

  DrawButton(hdc, head.clear, L"Clear browsing history", kHitClear, false);

  // ---- list ----
  const int viewTop = kHeaderHeight;
  const int viewHeight = std::max(0, height - viewTop);

  int contentHeight = 24;
  for (const Row& row : rows_) {
    contentHeight += row.header ? kDayHeaderHeight : kRowHeight;
  }
  SetScroll(contentHeight, viewHeight,
            RECT{width - 12, viewTop, width - 4, height});

  if (rows_.empty()) {
    RECT area{0, viewTop, width, height};
    area.top += 80;
    DrawLabel(hdc,
              query_.empty() ? L"No browsing history yet"
                             : L"No history matches your search",
              area, fontBold_, pageui::kMuted, DT_CENTER | DT_TOP | DT_SINGLELINE);
    return;
  }

  const auto& entries = HistoryStore::Get().entries();
  IntersectClipRect(hdc, 0, viewTop, width, height);

  int y = viewTop - scrollY();
  for (size_t i = 0; i < rows_.size(); ++i) {
    const Row& row = rows_[i];
    const int rowHeight = row.header ? kDayHeaderHeight : kRowHeight;

    if (y + rowHeight > viewTop && y < height) {
      if (row.header) {
        RECT label{head.left + 8, y + 8, head.right, y + rowHeight};
        DrawLabel(hdc, row.text, label, fontBold_, pageui::kText,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      } else if (row.entryIndex < entries.size()) {
        const HistoryEntry& entry = entries[row.entryIndex];
        const RECT rowRect{head.left, y, head.right, y + rowHeight};
        const RECT deleteRect{head.right - 44, y + 10, head.right - 12, y + 42};
        const int rowId = kHitRowBase + static_cast<int>(i);
        const int deleteId = kHitDeleteBase + static_cast<int>(i);

        AddHit(rowRect, rowId);
        AddHit(deleteRect, deleteId);

        const bool hot = hoverId() == rowId || hoverId() == deleteId;
        if (hot) FillRound(hdc, rowRect, 12, pageui::kPanel, pageui::kPanel);

        DrawLabel(hdc, TimeLabel(entry.timeMs),
                  RECT{head.left + 16, y, head.left + 96, y + rowHeight},
                  fontSmall_, pageui::kMuted,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const std::wstring title =
            entry.title.empty() ? entry.url : entry.title;
        DrawLabel(hdc, title,
                  RECT{head.left + 104, y + 7, head.right - 56, y + 27},
                  fontUi_, pageui::kText,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawLabel(hdc, entry.url,
                  RECT{head.left + 104, y + 27, head.right - 56, y + 46},
                  fontSmall_, pageui::kMuted,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (hot) {
          // Round hover circle with an X, to delete this entry.
          if (hoverId() == deleteId) {
            FillRound(hdc, deleteRect, 32, pageui::kPanel3, pageui::kPanel3);
          }
          const int cx = (deleteRect.left + deleteRect.right) / 2;
          const int cy = (deleteRect.top + deleteRect.bottom) / 2;
          HPEN pen = CreatePen(PS_SOLID, 2,
                               hoverId() == deleteId ? accent : pageui::kMuted);
          HGDIOBJ oldPen = SelectObject(hdc, pen);
          MoveToEx(hdc, cx - 5, cy - 5, nullptr);
          LineTo(hdc, cx + 6, cy + 6);
          MoveToEx(hdc, cx + 5, cy - 5, nullptr);
          LineTo(hdc, cx - 6, cy + 6);
          SelectObject(hdc, oldPen);
          DeleteObject(pen);
        }
      }
    }
    y += rowHeight;
  }

  SelectClipRgn(hdc, nullptr);
  PaintScrollbar(hdc);
}

void HistoryPage::OnClick(int id, int /*x*/, int /*y*/) {
  if (id == kHitClear) {
    if (MessageBoxW(hwnd_, L"Clear all browsing history?", L"Vibrium",
                    MB_YESNO | MB_ICONQUESTION) == IDYES) {
      HistoryStore::Get().Clear();
      rebuild_ = true;
      Repaint();
    }
    return;
  }

  if (id >= kHitDeleteBase) {
    const size_t row = static_cast<size_t>(id - kHitDeleteBase);
    if (row < rows_.size() && !rows_[row].header) {
      HistoryStore::Get().RemoveAt(rows_[row].entryIndex);
      rebuild_ = true;
      Repaint();
    }
    return;
  }

  if (id >= kHitRowBase) {
    const size_t row = static_cast<size_t>(id - kHitRowBase);
    const auto& entries = HistoryStore::Get().entries();
    if (row < rows_.size() && !rows_[row].header &&
        rows_[row].entryIndex < entries.size() && onOpenUrl_) {
      const std::wstring url = entries[rows_[row].entryIndex].url;
      onOpenUrl_(url);
    }
  }
}

}  // namespace vibrium