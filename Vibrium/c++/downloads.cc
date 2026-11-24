#include "downloads.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

#include "icon_util.h"
#include "theme.h"

namespace vibrium {
namespace {

constexpr int kPad = 32;
constexpr int kMaxContentWidth = 1000;
constexpr int kHeaderHeight = 84;
constexpr int kOptionsHeight = 84;
constexpr int kRowHeight = 84;
constexpr int kRowGap = 10;

constexpr int kHitClear = 10;
constexpr int kHitToggle = 11;
constexpr int kHitRowBase = 1000;  // + index * 4 + action

enum RowAction { kActionOpen = 0, kActionShow = 1, kActionCancel = 2, kActionRemove = 3 };

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

uint64_t NowMs() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(system_clock::now().time_since_epoch())
          .count());
}

std::wstring FileNameOf(const std::wstring& path) {
  const size_t slash = path.find_last_of(L"\\/");
  return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// Strips characters Windows does not allow in file names.
std::wstring SanitizeFileName(std::wstring name) {
  for (auto& ch : name) {
    if (ch < 32 || wcschr(L"<>:\"/\\|?*", ch)) ch = L'_';
  }
  while (!name.empty() && (name.back() == L' ' || name.back() == L'.')) {
    name.pop_back();
  }
  return name;
}

// "name.ext" -> "name (1).ext" ... until the path is free.
std::wstring UniquePath(const std::wstring& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return path;

  const size_t slash = path.find_last_of(L"\\/");
  const size_t dot = path.find_last_of(L'.');
  const bool hasExt = dot != std::wstring::npos &&
                      (slash == std::wstring::npos || dot > slash);
  const std::wstring stem = hasExt ? path.substr(0, dot) : path;
  const std::wstring ext = hasExt ? path.substr(dot) : L"";

  for (int n = 1; n < 10000; ++n) {
    const std::wstring candidate = stem + L" (" + std::to_wstring(n) + L")" + ext;
    if (!std::filesystem::exists(candidate, ec)) return candidate;
  }
  return path;
}

std::wstring FormatBytes(int64_t bytes) {
  if (bytes < 0) bytes = 0;
  const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
  double value = static_cast<double>(bytes);
  int unit = 0;
  while (value >= 1024.0 && unit < 4) {
    value /= 1024.0;
    ++unit;
  }
  wchar_t buffer[48]{};
  if (unit == 0) {
    swprintf_s(buffer, L"%lld B", static_cast<long long>(bytes));
  } else {
    swprintf_s(buffer, L"%.1f %ls", value, units[unit]);
  }
  return buffer;
}

std::wstring HostOf(const std::wstring& url) {
  const size_t scheme = url.find(L"://");
  if (scheme == std::wstring::npos) return L"";
  const size_t start = scheme + 3;
  const size_t end = url.find_first_of(L"/?#", start);
  return url.substr(start, end == std::wstring::npos ? end : end - start);
}

constexpr wchar_t kRegistryKey[] = L"Software\\Vibrium\\Downloads";
constexpr wchar_t kAskValue[] = L"AskWhere";

}  // namespace

bool IsDownloadsUrl(const std::wstring& url) {
  const size_t length = std::wstring(kDownloadsUrl).size();
  if (url.size() < length) return false;
  if (ToLower(url.substr(0, length)) != kDownloadsUrl) return false;
  if (url.size() == length) return true;
  const wchar_t next = url[length];
  return next == L'/' || next == L'#' || next == L'?';
}

// ---------------------------------------------------------------------------
// DownloadManager
// ---------------------------------------------------------------------------

DownloadManager& DownloadManager::Get() {
  static DownloadManager manager;
  return manager;
}

std::wstring DownloadManager::DefaultFolder() const {
  PWSTR raw = nullptr;
  std::wstring folder;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &raw)) &&
      raw) {
    folder = raw;
  }
  if (raw) CoTaskMemFree(raw);

  if (folder.empty()) {
    if (const wchar_t* profile = _wgetenv(L"USERPROFILE")) {
      folder = std::wstring(profile) + L"\\Downloads";
    }
  }
  return folder;
}

std::wstring DownloadManager::ListPath() const {
  const wchar_t* base = _wgetenv(L"LOCALAPPDATA");
  if (!base) return L"";
  std::error_code ec;
  const std::filesystem::path dir = std::filesystem::path(base) / L"Vibrium";
  std::filesystem::create_directories(dir, ec);
  return (dir / L"downloads.tsv").wstring();
}

void DownloadManager::Load() {
  if (loaded_) return;
  loaded_ = true;

  // Setting.
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, KEY_QUERY_VALUE,
                    &key) == ERROR_SUCCESS) {
    DWORD value = 0, size = sizeof(value), type = 0;
    if (RegQueryValueExW(key, kAskValue, nullptr, &type,
                         reinterpret_cast<BYTE*>(&value),
                         &size) == ERROR_SUCCESS &&
        type == REG_DWORD) {
      askWhere_ = value != 0;
    }
    RegCloseKey(key);
  }

  // Finished downloads from earlier sessions: time <TAB> total <TAB> path <TAB> url
  const std::wstring path = ListPath();
  if (path.empty()) return;
  std::ifstream in{std::filesystem::path(path), std::ios::binary};
  if (!in) return;

  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t a = line.find('\t');
    if (a == std::string::npos) continue;
    const size_t b = line.find('\t', a + 1);
    if (b == std::string::npos) continue;
    const size_t c = line.find('\t', b + 1);
    if (c == std::string::npos) continue;

    DownloadRecord rec;
    rec.id = nextId_++;
    rec.timeMs = std::strtoull(line.substr(0, a).c_str(), nullptr, 10);
    rec.total = std::strtoll(line.substr(a + 1, b - a - 1).c_str(), nullptr, 10);
    rec.received = rec.total;
    rec.path = Utf8ToWide(line.substr(b + 1, c - b - 1));
    rec.url = Utf8ToWide(line.substr(c + 1));
    rec.fileName = FileNameOf(rec.path);
    rec.complete = true;
    rec.percent = 100;
    if (!rec.path.empty()) records_.push_back(std::move(rec));
  }
  RefreshFileState();
}

void DownloadManager::Save() const {
  const std::wstring path = ListPath();
  if (path.empty()) return;
  std::ofstream out{std::filesystem::path(path),
                    std::ios::binary | std::ios::trunc};
  if (!out) return;
  for (const DownloadRecord& rec : records_) {
    if (!rec.complete) continue;
    out << rec.timeMs << '\t' << rec.total << '\t' << WideToUtf8(rec.path)
        << '\t' << WideToUtf8(rec.url) << '\n';
  }
}

void DownloadManager::SetAskWhere(bool value) {
  askWhere_ = value;
  ++revision_;

  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, nullptr, 0,
                      KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
    DWORD dword = value ? 1 : 0;
    RegSetValueExW(key, kAskValue, 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&dword), sizeof(dword));
    RegCloseKey(key);
  }
}

DownloadRecord* DownloadManager::FindByCefId(uint32_t cefId) {
  for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
    if (!it->inProgress) continue;
    if (it->cefId == cefId) return &*it;
    if (std::find(it->aliases.begin(), it->aliases.end(), cefId) !=
        it->aliases.end()) {
      return &*it;
    }
  }
  return nullptr;
}

DownloadRecord* DownloadManager::FindById(uint32_t id) {
  for (DownloadRecord& rec : records_) {
    if (rec.id == id) return &rec;
  }
  return nullptr;
}

bool DownloadManager::HasActive() const {
  for (const DownloadRecord& rec : records_) {
    if (rec.inProgress) return true;
  }
  return false;
}

DownloadRecord* DownloadManager::FindPendingMatch(const std::wstring& url,
                                                  const std::wstring& path) {
  for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
    if (!it->inProgress || it->updated) continue;
    if ((!path.empty() && it->path == path) ||
        (!url.empty() && it->url == url)) {
      return &*it;
    }
  }
  return nullptr;
}


// Our own Save As window, shown *before* the download starts. (With CEF's
// built-in one the data already flows while the window is still open.)
// Returns an empty string if the user cancels.
std::wstring ChooseSavePath(const std::wstring& initialPath) {
  const size_t slash = initialPath.find_last_of(L"\\/");
  const std::wstring dir =
      slash == std::wstring::npos ? L"" : initialPath.substr(0, slash);
  const std::wstring name =
      slash == std::wstring::npos ? initialPath : initialPath.substr(slash + 1);
  const size_t dot = name.find_last_of(L'.');
  const std::wstring ext =
      (dot == std::wstring::npos || dot + 1 >= name.size()) ? L"" : name.substr(dot + 1);

  std::wstring filter;
  if (!ext.empty()) {
    filter += ext + L" file";
    filter.push_back(L'\0');
    filter += L"*." + ext;
    filter.push_back(L'\0');
  }
  filter += L"All files";
  filter.push_back(L'\0');
  filter += L"*.*";
  filter.push_back(L'\0');
  filter.push_back(L'\0');

  std::vector<wchar_t> buffer(32768, L'\0');
  wcsncpy_s(buffer.data(), buffer.size(), name.c_str(), _TRUNCATE);

  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = GetActiveWindow();
  ofn.lpstrFilter = filter.c_str();
  ofn.lpstrFile = buffer.data();
  ofn.nMaxFile = static_cast<DWORD>(buffer.size());
  ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
  ofn.lpstrDefExt = ext.empty() ? nullptr : ext.c_str();
  ofn.lpstrTitle = L"Save As";
  ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT |
              OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
  if (!GetSaveFileNameW(&ofn)) return L"";
  return buffer.data();
}

void DownloadManager::HandleBeforeDownload(
    CefRefPtr<CefDownloadItem> item, const std::wstring& suggestedName,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  std::wstring name = SanitizeFileName(suggestedName);
  if (name.empty()) name = L"download";

  const std::wstring folder = DefaultFolder();
  std::error_code ec;
  if (!folder.empty()) std::filesystem::create_directories(folder, ec);

  std::wstring target = folder.empty() ? name : folder + L"\\" + name;
  // Without the Save As window pick a free name so nothing is overwritten.
  // With it, the window itself asks about overwriting.
  if (!askWhere_) {
    target = UniquePath(target);
  } else {
    // Ask first; nothing is downloaded (or listed) until the user confirms.
    // Returning without callback->Continue() cancels the download.
    const std::wstring chosen = ChooseSavePath(target);
    if (chosen.empty()) return;
    target = chosen;
  }

  DownloadRecord rec;
  rec.id = nextId_++;
  rec.cefId = item ? item->GetId() : 0;
  rec.url = item ? item->GetURL().ToWString() : L"";
  rec.fileName = FileNameOf(target);
  rec.path = target;
  rec.total = item ? item->GetTotalBytes() : -1;
  rec.inProgress = true;
  rec.timeMs = NowMs();
  records_.push_back(std::move(rec));
  ++startCount_;
  ++revision_;

  // The location is already decided, so CEF must not ask again.
  callback->Continue(target, false);
}

void DownloadManager::HandleUpdated(
    CefRefPtr<CefDownloadItem> item,
    CefRefPtr<CefDownloadItemCallback> callback) {
  if (!item) return;
  const uint32_t cefId = item->GetId();
  const std::wstring itemUrl = item->GetURL().ToWString();
  const std::wstring fullPath = item->GetFullPath().ToWString();

  DownloadRecord* rec = FindByCefId(cefId);
  if (!rec) {
    // OnBeforeDownload and OnDownloadUpdated do not always report the same
    // id (and some downloads, e.g. "Save image as", report 0 in one of them).
    // Adopt the record OnBeforeDownload created instead of adding a second
    // row; otherwise the first row stays "downloading" forever.
    rec = FindPendingMatch(itemUrl, fullPath);
    if (rec) rec->cefId = cefId;
  }
  if (!rec && item->IsInProgress() && item->GetTotalBytes() > 0) {
    // Same download reported under a second id (redirects, e.g. GitHub
    // artifacts): fold it into the existing row instead of adding another.
    for (auto it = records_.rbegin(); it != records_.rend(); ++it) {
      if (!it->inProgress || it->total != item->GetTotalBytes()) continue;
      if (!fullPath.empty() && !it->path.empty() && it->path != fullPath) continue;
      it->aliases.push_back(cefId);
      rec = &*it;
      break;
    }
  }
  if (!rec) {
    // Only unknown if the download began without us seeing it start.
    if (!item->IsInProgress()) return;
    DownloadRecord created;
    created.id = nextId_++;
    created.cefId = cefId;
    created.url = itemUrl;
    created.timeMs = NowMs();
    records_.push_back(std::move(created));
    rec = &records_.back();
  }
  rec->updated = true;

  if (!fullPath.empty()) {
    rec->path = fullPath;
    rec->fileName = FileNameOf(fullPath);
  }
  if (rec->fileName.empty()) {
    rec->fileName = SanitizeFileName(item->GetSuggestedFileName().ToWString());
  }

  rec->received = item->GetReceivedBytes();
  rec->total = item->GetTotalBytes();
  rec->percent = item->GetPercentComplete();
  rec->speed = item->GetCurrentSpeed();
  rec->inProgress = item->IsInProgress();
  rec->complete = item->IsComplete();
  rec->canceled = item->IsCanceled();

  if (rec->inProgress && callback) {
    callbacks_[cefId] = callback;
  } else {
    callbacks_.erase(cefId);
  }

  if (rec->complete) {
    rec->timeMs = NowMs();
    rec->percent = 100;
    rec->received = rec->total > 0 ? rec->total : rec->received;
    ++finishCount_;
  }

  // A finished or canceled download must leave no "in progress" twin behind.
  const bool finishedOk = rec->complete && !rec->inProgress;
  if (!rec->inProgress) {
    const uint32_t keepId = rec->id;
    const std::wstring keepPath = rec->path;
    const std::wstring keepUrl = rec->url;
    records_.erase(
        std::remove_if(records_.begin(), records_.end(),
                       [&](const DownloadRecord& r) {
                         if (r.id == keepId || !r.inProgress) return false;
                         if (r.cefId == cefId) return true;
                         if (!keepPath.empty() && r.path == keepPath) return true;
                         return !r.updated && !keepUrl.empty() &&
                                r.url == keepUrl;
                       }),
        records_.end());
  }

  // `rec` may point at a moved element after the erase above: do not use it.
  if (finishedOk) Save();
  ++revision_;
}

void DownloadManager::Cancel(uint32_t id) {
  DownloadRecord* rec = FindById(id);
  if (!rec) return;
  auto it = callbacks_.find(rec->cefId);
  for (uint32_t alias : rec->aliases) {
    auto ait = callbacks_.find(alias);
    if (ait != callbacks_.end() && ait->second) ait->second->Cancel();
  }
  if (it != callbacks_.end() && it->second) {
    it->second->Cancel();
  } else if (rec->inProgress) {
    // No live transfer behind this row (stale entry): just close it.
    rec->inProgress = false;
    rec->canceled = true;
    ++revision_;
  }
}

void DownloadManager::Remove(uint32_t id) {
  DownloadRecord* rec = FindById(id);
  if (!rec) return;
  if (rec->inProgress) Cancel(id);
  const uint32_t cefId = rec->cefId;
  const bool wasInProgress = rec->inProgress;
  records_.erase(std::remove_if(records_.begin(), records_.end(),
                                [id](const DownloadRecord& r) { return r.id == id; }),
                 records_.end());
  if (!wasInProgress) callbacks_.erase(cefId);
  Save();
  ++revision_;
}

void DownloadManager::ClearFinished() {
  records_.erase(std::remove_if(records_.begin(), records_.end(),
                                [](const DownloadRecord& r) { return !r.inProgress; }),
                 records_.end());
  Save();
  ++revision_;
}

void DownloadManager::RefreshFileState() {
  std::error_code ec;
  bool changed = false;
  for (DownloadRecord& rec : records_) {
    if (!rec.complete || rec.path.empty()) continue;
    const bool missing = !std::filesystem::exists(rec.path, ec);
    if (missing != rec.missing) {
      rec.missing = missing;
      changed = true;
    }
  }
  if (changed) ++revision_;
}

// ---------------------------------------------------------------------------
// DownloadsPage
// ---------------------------------------------------------------------------

void DownloadsPage::OnShown() {
  DownloadManager::Get().RefreshFileState();
}

bool DownloadsPage::OnTick() {
  return paintedRevision_ != DownloadManager::Get().revision();
}

void DownloadsPage::Paint(HDC hdc, const RECT& client) {
  DownloadManager& manager = DownloadManager::Get();
  paintedRevision_ = manager.revision();

  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  const COLORREF accent = Theme::Accent();

  const int contentWidth = std::min(width - 2 * kPad, kMaxContentWidth);
  const int left = std::max(kPad, (width - contentWidth) / 2);
  const int right = left + std::max(0, contentWidth);

  // ---- header (fixed) ----
  DrawLabel(hdc, L"Downloads", RECT{left, 28, right, 72}, fontTitle_,
            pageui::kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  DrawButton(hdc, RECT{std::max(left, right - 170), 34, right, 70},
             L"Clear completed", kHitClear, false);

  // ---- scrollable body ----
  const int viewTop = kHeaderHeight;
  const int viewHeight = std::max(0, height - viewTop);

  const auto& records = manager.records();
  const int count = static_cast<int>(records.size());
  const int contentHeight = 8 + kOptionsHeight + 20 +
                            (count ? count * (kRowHeight + kRowGap) : 120) + 16;
  SetScroll(contentHeight, viewHeight,
            RECT{width - 12, viewTop, width - 4, height});

  IntersectClipRect(hdc, 0, viewTop, width, height);
  int y = viewTop + 8 - scrollY();

  // Options card with the "ask where to save" switch.
  {
    const RECT card{left, y, right, y + kOptionsHeight};
    FillRound(hdc, card, 16, pageui::kPanel, pageui::kBorder);

    DrawLabel(hdc, L"Ask where to save each file",
              RECT{left + 24, y + 16, right - 100, y + 40}, fontBold_,
              pageui::kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const std::wstring caption =
        manager.askWhere()
            ? L"A Save As window opens for every download so you can pick the folder."
            : L"Off: files are saved automatically to " + manager.DefaultFolder();
    DrawLabel(hdc, caption, RECT{left + 24, y + 42, right - 100, y + 66},
              fontSmall_, pageui::kMuted,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Switch.
    const RECT track{right - 24 - 48, y + (kOptionsHeight - 26) / 2,
                     right - 24, y + (kOptionsHeight - 26) / 2 + 26};
    AddHit(card, kHitToggle);
    FillRound(hdc, track, 26, manager.askWhere() ? accent : pageui::kPanel3,
              manager.askWhere() ? accent : pageui::kBorder);
    const int knob = 20;
    const int knobLeft = manager.askWhere() ? track.right - 3 - knob : track.left + 3;
    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    HPEN noPen = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ oldBrush = SelectObject(hdc, white);
    HGDIOBJ oldPen = SelectObject(hdc, noPen);
    Ellipse(hdc, knobLeft, track.top + 3, knobLeft + knob, track.top + 3 + knob);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(white);
    DeleteObject(noPen);
  }
  y += kOptionsHeight + 20;

  shownIds_.clear();

  if (records.empty()) {
    DrawLabel(hdc, L"No downloads yet", RECT{0, y + 40, width, y + 70},
              fontBold_, pageui::kMuted, DT_CENTER | DT_TOP | DT_SINGLELINE);
  }

  // Newest first.
  for (size_t n = records.size(); n-- > 0;) {
    const DownloadRecord& rec = records[n];
    const int index = static_cast<int>(shownIds_.size());
    shownIds_.push_back(rec.id);

    if (y + kRowHeight > viewTop && y < height) {
      const RECT card{left, y, right, y + kRowHeight};
      FillRound(hdc, card, 16, pageui::kPanel, pageui::kBorder);

      DrawTintedIcon(hdc, L"download.png",
                     (rec.canceled || rec.missing) ? pageui::kMuted : accent,
                     left + 36, y + kRowHeight / 2, 24);

      // Buttons, right to left.
      int buttonRight = right - 16;
      const int buttonTop = y + (kRowHeight - 32) / 2;
      auto place = [&](int buttonWidth) {
        RECT r{buttonRight - buttonWidth, buttonTop, buttonRight, buttonTop + 32};
        buttonRight -= buttonWidth + 8;
        return r;
      };
      const int base = kHitRowBase + index * 4;

      if (rec.inProgress) {
        DrawButton(hdc, place(84), L"Cancel", base + kActionCancel, false);
      } else {
        // Remove (X) button.
        const RECT remove = place(32);
        const int removeId = base + kActionRemove;
        AddHit(remove, removeId);
        if (hoverId() == removeId) {
          FillRound(hdc, remove, 32, pageui::kPanel3, pageui::kPanel3);
        }
        const int cx = (remove.left + remove.right) / 2;
        const int cy = (remove.top + remove.bottom) / 2;
        HPEN pen = CreatePen(PS_SOLID, 2,
                             hoverId() == removeId ? accent : pageui::kMuted);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, cx - 5, cy - 5, nullptr);
        LineTo(hdc, cx + 6, cy + 6);
        MoveToEx(hdc, cx + 5, cy - 5, nullptr);
        LineTo(hdc, cx - 6, cy + 6);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);

        if (rec.complete && !rec.missing) {
          DrawButton(hdc, place(120), L"Show in folder", base + kActionShow, false);
          DrawButton(hdc, place(72), L"Open", base + kActionOpen, true);
        }
      }
      const int textRight = buttonRight - 4;

      DrawLabel(hdc, rec.fileName.empty() ? rec.url : rec.fileName,
                RECT{left + 68, y + 14, textRight, y + 36}, fontBold_,
                (rec.canceled || rec.missing) ? pageui::kMuted : pageui::kText,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

      std::wstring status;
      if (rec.inProgress) {
        status = FormatBytes(rec.received);
        if (rec.total > 0) status += L" of " + FormatBytes(rec.total);
        if (rec.speed > 0) status += L"  \u2022  " + FormatBytes(rec.speed) + L"/s";
      } else if (rec.canceled) {
        status = L"Canceled";
      } else if (rec.missing) {
        status = L"File was moved or deleted";
      } else {
        status = FormatBytes(rec.total > 0 ? rec.total : rec.received);
        const std::wstring host = HostOf(rec.url);
        if (!host.empty()) status += L"  \u2022  " + host;
      }
      DrawLabel(hdc, status, RECT{left + 68, y + 38, textRight, y + 58},
                fontSmall_, rec.canceled ? pageui::kBad : pageui::kMuted,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

      if (rec.inProgress) {
        const RECT bar{left + 68, y + 64, std::max(left + 68, textRight), y + 68};
        FillRound(hdc, bar, 4, pageui::kPanel3, pageui::kPanel3);
        if (rec.percent > 0) {
          RECT filled = bar;
          filled.right = bar.left + (bar.right - bar.left) * std::min(rec.percent, 100) / 100;
          if (filled.right > filled.left) FillRound(hdc, filled, 4, accent, accent);
        }
      }
    }
    y += kRowHeight + kRowGap;
  }

  SelectClipRgn(hdc, nullptr);
  PaintScrollbar(hdc);
}

void DownloadsPage::OnClick(int id, int /*x*/, int /*y*/) {
  DownloadManager& manager = DownloadManager::Get();

  if (id == kHitToggle) {
    manager.SetAskWhere(!manager.askWhere());
    Repaint();
    return;
  }
  if (id == kHitClear) {
    manager.ClearFinished();
    Repaint();
    return;
  }
  if (id < kHitRowBase) return;

  const size_t index = static_cast<size_t>((id - kHitRowBase) / 4);
  const int action = (id - kHitRowBase) % 4;
  if (index >= shownIds_.size()) return;
  const uint32_t recordId = shownIds_[index];

  // Copy what is needed: the list can change while the action runs.
  const DownloadRecord* found = nullptr;
  for (const DownloadRecord& rec : manager.records()) {
    if (rec.id == recordId) found = &rec;
  }
  if (!found) return;
  const std::wstring path = found->path;

  switch (action) {
    case kActionOpen:
      ShellExecuteW(hwnd_, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      break;
    case kActionShow: {
      const std::wstring args = L"/select,\"" + path + L"\"";
      ShellExecuteW(hwnd_, L"open", L"explorer.exe", args.c_str(), nullptr,
                    SW_SHOWNORMAL);
      break;
    }
    case kActionCancel:
      manager.Cancel(recordId);
      break;
    case kActionRemove:
      manager.Remove(recordId);
      break;
    default:
      break;
  }
  Repaint();
}

// ---------------------------------------------------------------------------
// DownloadPopup
// ---------------------------------------------------------------------------

namespace {

constexpr wchar_t kPopupClass[] = L"VibriumDownloadPopup";
constexpr UINT_PTR kPopupTimer = 1;
constexpr UINT kPopupPollMs = 250;
constexpr uint64_t kPopupLingerMs = 6000;
constexpr int kPopupWidth = 380;
constexpr int kPopupPad = 14;
constexpr int kPopupTitleHeight = 40;
constexpr int kPopupRowHeight = 66;
constexpr int kPopupFooterHeight = 40;
constexpr int kPopupMaxRows = 4;
constexpr int kPopupRadius = 16;
constexpr COLORREF kPopupBg = RGB(0, 0, 0);

constexpr int kPopupHitDismiss = 1;
constexpr int kPopupHitAll = 2;
constexpr int kPopupHitOpenBase = 100;    // + row index
constexpr int kPopupHitCancelBase = 200;  // + row index

inline int PopupX(LPARAM lp) { return static_cast<int>(static_cast<short>(LOWORD(lp))); }
inline int PopupY(LPARAM lp) { return static_cast<int>(static_cast<short>(HIWORD(lp))); }

HFONT PopupFont(int pixels, int weight) {
  return CreateFontW(-pixels, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void PopupRound(HDC hdc, const RECT& rect, int radius, COLORREF fill,
                COLORREF border, int penWidth = 1) {
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = CreatePen(PS_SOLID, penWidth, border);
  HGDIOBJ oldBrush = SelectObject(hdc, brush);
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
  SelectObject(hdc, oldBrush);
  SelectObject(hdc, oldPen);
  DeleteObject(brush);
  DeleteObject(pen);
}

void PopupText(HDC hdc, const std::wstring& text, RECT rect, HFONT font,
               COLORREF color, UINT flags) {
  HGDIOBJ old = SelectObject(hdc, font);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, color);
  DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rect,
            flags | DT_NOPREFIX);
  SelectObject(hdc, old);
}

void PopupCross(HDC hdc, const RECT& box, COLORREF color) {
  const int cx = (box.left + box.right) / 2;
  const int cy = (box.top + box.bottom) / 2;
  HPEN pen = CreatePen(PS_SOLID, 2, color);
  HGDIOBJ old = SelectObject(hdc, pen);
  MoveToEx(hdc, cx - 5, cy - 5, nullptr);
  LineTo(hdc, cx + 6, cy + 6);
  MoveToEx(hdc, cx + 5, cy - 5, nullptr);
  LineTo(hdc, cx - 6, cy + 6);
  SelectObject(hdc, old);
  DeleteObject(pen);
}

std::wstring PopupStatus(const DownloadRecord& rec) {
  if (rec.inProgress) {
    std::wstring status = FormatBytes(rec.received);
    if (rec.total > 0) status += L" of " + FormatBytes(rec.total);
    if (rec.speed > 0) status += L"  \u2022  " + FormatBytes(rec.speed) + L"/s";
    return status;
  }
  if (rec.canceled) return L"Canceled";
  if (rec.missing) return L"File was moved or deleted";
  std::wstring status = L"Downloaded  \u2022  " +
                        FormatBytes(rec.total > 0 ? rec.total : rec.received);
  return status;
}

}  // namespace

DownloadPopup::~DownloadPopup() {
  if (hwnd_) {
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
    if (IsWindow(hwnd_)) {
      KillTimer(hwnd_, kPopupTimer);
      DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
  }
  for (HFONT font : {fontBold_, fontSmall_, fontTitle_}) {
    if (font) DeleteObject(font);
  }
}

bool DownloadPopup::Create(HWND owner, HINSTANCE instance) {
  owner_ = owner;
  instance_ = instance;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance_;
  wc.lpszClassName = kPopupClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  // An *owned* popup always stays above its owner's child windows (including
  // CEF's browser view) and hides when the owner is minimised, without being
  // topmost over other applications.
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kPopupClass,
                          L"", WS_POPUP, 0, 0, kPopupWidth, 100, owner_,
                          nullptr, instance_, this);
  if (!hwnd_) return false;

  fontBold_ = PopupFont(13, FW_SEMIBOLD);
  fontSmall_ = PopupFont(12, FW_NORMAL);
  fontTitle_ = PopupFont(15, FW_SEMIBOLD);

  SetTimer(hwnd_, kPopupTimer, kPopupPollMs, nullptr);
  seenStart_ = DownloadManager::Get().startCount();
  seenFinish_ = DownloadManager::Get().finishCount();
  return true;
}

void DownloadPopup::SetAnchor(POINT topRightScreen) {
  anchor_ = topRightScreen;
  if (visible_) Layout();
}

void DownloadPopup::ShowPanel() {
  if (!hwnd_) return;
  hideAtMs_ = NowMs() + kPopupLingerMs;
  Layout();
  if (!visible_) {
    visible_ = true;
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void DownloadPopup::HidePanel() {
  if (!hwnd_) return;
  visible_ = false;
  hoverId_ = 0;
  ShowWindow(hwnd_, SW_HIDE);
}

void DownloadPopup::Layout() {
  const auto& records = DownloadManager::Get().records();
  const int rows = std::min<int>(kPopupMaxRows, static_cast<int>(records.size()));
  const int height = kPopupTitleHeight + rows * kPopupRowHeight +
                     kPopupFooterHeight + 4;
  const int x = anchor_.x - kPopupWidth;
  const int y = anchor_.y;
  SetWindowPos(hwnd_, nullptr, x, y, kPopupWidth, height,
               SWP_NOACTIVATE | SWP_NOZORDER);
  // Rounded outline for the window itself; the border is drawn in Paint.
  HRGN region = CreateRoundRectRgn(0, 0, kPopupWidth + 1, height + 1,
                                   kPopupRadius, kPopupRadius);
  if (region && !SetWindowRgn(hwnd_, region, TRUE)) DeleteObject(region);
}

void DownloadPopup::OnTimer() {
  DownloadManager& manager = DownloadManager::Get();
  const uint64_t now = NowMs();

  const bool started = manager.startCount() != seenStart_;
  const bool finished = manager.finishCount() != seenFinish_;
  seenStart_ = manager.startCount();
  seenFinish_ = manager.finishCount();
  if (started || finished) {
    ShowPanel();
    return;
  }
  if (!visible_) return;

  // Stay open while something is downloading or the pointer is on the panel.
  if (manager.HasActive() || hoverId_ != 0 || tracking_) {
    hideAtMs_ = now + kPopupLingerMs;
  }
  if (paintedRevision_ != manager.revision()) {
    Layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
  if (now >= hideAtMs_) HidePanel();
}

int DownloadPopup::HitTest(int x, int y) const {
  const POINT pt{x, y};
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (PtInRect(&it->rect, pt)) return it->id;
  }
  return 0;
}

void DownloadPopup::PaintAll(HDC hdc, const RECT& client) {
  DownloadManager& manager = DownloadManager::Get();
  paintedRevision_ = manager.revision();
  hits_.clear();
  shownIds_.clear();

  const COLORREF accent = Theme::Accent();
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;

  // Black panel with an accent-coloured perimeter.
  {
    HBRUSH bg = CreateSolidBrush(kPopupBg);
    FillRect(hdc, &client, bg);
    DeleteObject(bg);
  }
  RECT outline{1, 1, width - 1, height - 1};
  PopupRound(hdc, outline, kPopupRadius, kPopupBg, accent, 2);

  // Title row.
  PopupText(hdc, L"Downloads",
            RECT{kPopupPad + 4, 6, width - 60, kPopupTitleHeight}, fontTitle_,
            pageui::kText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  const RECT dismiss{width - 40, 8, width - 10, 34};
  hits_.push_back({dismiss, kPopupHitDismiss});
  if (hoverId_ == kPopupHitDismiss) {
    PopupRound(hdc, dismiss, 26, pageui::kPanel2, pageui::kPanel2);
  }
  PopupCross(hdc, dismiss, hoverId_ == kPopupHitDismiss ? accent : pageui::kMuted);

  // Rows, newest first.
  const auto& records = manager.records();
  int y = kPopupTitleHeight;
  int shown = 0;
  for (size_t n = records.size(); n-- > 0 && shown < kPopupMaxRows; ++shown) {
    const DownloadRecord& rec = records[n];
    shownIds_.push_back(rec.id);
    const int index = shown;
    const RECT row{kPopupPad, y, width - kPopupPad, y + kPopupRowHeight - 4};
    const int openId = kPopupHitOpenBase + index;
    const bool canOpen = rec.complete && !rec.missing;
    if (canOpen) hits_.push_back({row, openId});
    const bool hot = canOpen && hoverId_ == openId;
    PopupRound(hdc, row, 12, hot ? pageui::kPanel : kPopupBg,
               hot ? accent : pageui::kBorder);

    DrawTintedIcon(hdc, L"download.png",
                   (rec.canceled || rec.missing) ? pageui::kMuted : accent,
                   row.left + 24, (row.top + row.bottom) / 2 - 2, 20);

    int textRight = row.right - 12;
    if (rec.inProgress) {
      const RECT cancel{row.right - 34, row.top + 14, row.right - 8, row.top + 40};
      const int cancelId = kPopupHitCancelBase + index;
      hits_.push_back({cancel, cancelId});
      if (hoverId_ == cancelId) {
        PopupRound(hdc, cancel, 26, pageui::kPanel2, pageui::kPanel2);
      }
      PopupCross(hdc, cancel, hoverId_ == cancelId ? accent : pageui::kMuted);
      textRight = cancel.left - 4;
    }

    PopupText(hdc, rec.fileName.empty() ? rec.url : rec.fileName,
              RECT{row.left + 46, row.top + 8, textRight, row.top + 28},
              fontBold_,
              (rec.canceled || rec.missing) ? pageui::kMuted : pageui::kText,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    PopupText(hdc, PopupStatus(rec),
              RECT{row.left + 46, row.top + 28, textRight, row.top + 46},
              fontSmall_, rec.canceled ? pageui::kBad : pageui::kMuted,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (rec.inProgress) {
      const RECT bar{row.left + 46, row.bottom - 12, std::max<int>(row.left + 46, textRight),
                     row.bottom - 8};
      PopupRound(hdc, bar, 4, pageui::kPanel3, pageui::kPanel3);
      if (rec.percent > 0) {
        RECT filled = bar;
        filled.right = bar.left + (bar.right - bar.left) * std::min(rec.percent, 100) / 100;
        if (filled.right > filled.left) PopupRound(hdc, filled, 4, accent, accent);
      }
    }
    y += kPopupRowHeight;
  }

  // Footer link.
  const RECT footer{kPopupPad, y, width - kPopupPad, y + kPopupFooterHeight - 4};
  hits_.push_back({footer, kPopupHitAll});
  PopupText(hdc, L"Show all downloads", footer, fontBold_,
            hoverId_ == kPopupHitAll ? pageui::kText : accent,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DownloadPopup::OnClick(int id) {
  DownloadManager& manager = DownloadManager::Get();
  if (id == kPopupHitDismiss) {
    HidePanel();
    return;
  }
  if (id == kPopupHitAll) {
    HidePanel();
    if (onShowAll_) onShowAll_();
    return;
  }
  const bool cancel = id >= kPopupHitCancelBase;
  const size_t index = static_cast<size_t>(id - (cancel ? kPopupHitCancelBase : kPopupHitOpenBase));
  if (index >= shownIds_.size()) return;
  const uint32_t recordId = shownIds_[index];

  std::wstring path;
  bool openable = false;
  for (const DownloadRecord& rec : manager.records()) {
    if (rec.id == recordId) {
      path = rec.path;
      openable = rec.complete && !rec.missing;
    }
  }
  if (cancel) {
    manager.Cancel(recordId);
  } else if (openable && !path.empty()) {
    ShellExecuteW(hwnd_, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    HidePanel();
  }
}

LRESULT CALLBACK DownloadPopup::WindowProc(HWND hwnd, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
  DownloadPopup* self = reinterpret_cast<DownloadPopup*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<DownloadPopup*>(create->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
  return self->HandleMessage(message, wParam, lParam);
}

LRESULT DownloadPopup::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;

    case WM_ERASEBKGND:
      return 1;

    case WM_TIMER:
      if (wParam == kPopupTimer) {
        OnTimer();
        return 0;
      }
      break;

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
          SetBkMode(mem, TRANSPARENT);
          PaintAll(mem, client);
          BitBlt(hdc, 0, 0, width, height, mem, 0, 0, SRCCOPY);
          SelectObject(mem, oldBitmap);
        }
        if (bitmap) DeleteObject(bitmap);
        if (mem) DeleteDC(mem);
      }
      EndPaint(hwnd_, &ps);
      return 0;
    }

    case WM_MOUSEMOVE: {
      const int id = HitTest(PopupX(lParam), PopupY(lParam));
      if (!tracking_) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
        if (TrackMouseEvent(&track)) tracking_ = true;
      }
      if (id != hoverId_) {
        hoverId_ = id;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;
    }

    case WM_MOUSELEAVE:
      tracking_ = false;
      hoverId_ = 0;
      hideAtMs_ = NowMs() + kPopupLingerMs;
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;

    case WM_SETCURSOR:
      if (LOWORD(lParam) == HTCLIENT && hoverId_ > 0) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
      break;

    case WM_LBUTTONUP: {
      const int id = HitTest(PopupX(lParam), PopupY(lParam));
      if (id > 0) OnClick(id);
      return 0;
    }

    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wParam, lParam);
}

}  // namespace vibrium