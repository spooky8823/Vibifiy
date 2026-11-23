#include "downloads.h"

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
    if (it->inProgress && it->cefId == cefId) return &*it;
  }
  return nullptr;
}

DownloadRecord* DownloadManager::FindById(uint32_t id) {
  for (DownloadRecord& rec : records_) {
    if (rec.id == id) return &rec;
  }
  return nullptr;
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
  if (!askWhere_) target = UniquePath(target);

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
  ++revision_;

  // show_dialog = true opens the Save As window, starting at `target`.
  callback->Continue(target, askWhere_);
}

void DownloadManager::HandleUpdated(
    CefRefPtr<CefDownloadItem> item,
    CefRefPtr<CefDownloadItemCallback> callback) {
  if (!item) return;
  const uint32_t cefId = item->GetId();

  DownloadRecord* rec = FindByCefId(cefId);
  if (!rec) {
    // Only unknown if the download began without us seeing it start.
    if (!item->IsInProgress()) return;
    DownloadRecord created;
    created.id = nextId_++;
    created.cefId = cefId;
    created.url = item->GetURL().ToWString();
    created.timeMs = NowMs();
    records_.push_back(std::move(created));
    rec = &records_.back();
  }

  const std::wstring fullPath = item->GetFullPath().ToWString();
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
    Save();
  }
  ++revision_;
}

void DownloadManager::Cancel(uint32_t id) {
  DownloadRecord* rec = FindById(id);
  if (!rec) return;
  auto it = callbacks_.find(rec->cefId);
  if (it != callbacks_.end() && it->second) it->second->Cancel();
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

}  // namespace vibrium