// Browsing history: a persistent log of visited pages plus the native
// vibrium://history page that shows it.
#ifndef VIBRIUM_HISTORY_H_
#define VIBRIUM_HISTORY_H_

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "native_page.h"

namespace vibrium {

// What the address bar shows for the history tab. Typing it opens the page.
inline constexpr wchar_t kHistoryUrl[] = L"vibrium://history";

// True for "vibrium://history" (case-insensitive), optionally followed by a
// path, query or fragment.
bool IsHistoryUrl(const std::wstring& url);

struct HistoryEntry {
  uint64_t timeMs = 0;  // milliseconds since the Unix epoch (UTC)
  std::wstring url;
  std::wstring title;
};

// The history log. Stored as UTF-8 text, one visit per line
// (time <TAB> url <TAB> title), in %LOCALAPPDATA%\Vibrium\history.tsv.
// Used from the browser's UI thread only.
class HistoryStore {
 public:
  static HistoryStore& Get();

  // Reads the log from disk. Call once at startup.
  void Load();

  // Only ordinary web pages (http/https) are recorded.
  static bool ShouldRecord(const std::wstring& url);

  // Logs a visit. A repeat of the previous URL within a few seconds (reloads,
  // redirects) updates that entry instead of adding a new one.
  void AddVisit(const std::wstring& url, const std::wstring& title);
  // Fills in the title of the most recent visit to `url`.
  void UpdateTitle(const std::wstring& url, const std::wstring& title);

  void RemoveAt(size_t index);
  void Clear();
  // Writes pending title edits to disk. Call periodically and at exit.
  void Flush();

  // Oldest first.
  const std::vector<HistoryEntry>& entries() const { return entries_; }
  // Bumped on every change, so the page knows when to refresh.
  uint64_t revision() const { return revision_; }

 private:
  HistoryStore() = default;

  std::wstring FilePath() const;
  void Append(const HistoryEntry& entry) const;
  void Rewrite() const;

  std::vector<HistoryEntry> entries_;
  uint64_t revision_ = 1;
  bool loaded_ = false;
  bool dirty_ = false;
};

// The vibrium://history page: visits grouped by day, searchable, with
// per-entry delete and "Clear browsing history".
class HistoryPage final : public NativePage {
 public:
  ~HistoryPage() override;

  bool Create(HWND parent, HINSTANCE instance) {
    return NativePage::Create(parent, instance, L"VibriumHistoryPage");
  }
  // Called with the URL of an entry the user clicks.
  void SetOnOpenUrl(std::function<void(const std::wstring&)> callback) {
    onOpenUrl_ = std::move(callback);
  }

 protected:
  void OnCreated() override;
  void OnShown() override;
  void OnResized(int width, int height) override;
  bool OnTick() override;
  void Paint(HDC hdc, const RECT& client) override;
  void OnClick(int id, int x, int y) override;
  bool OnMessage(UINT message, WPARAM wParam, LPARAM lParam,
                 LRESULT* result) override;

 private:
  struct Row {
    bool header = false;
    std::wstring text;     // day label for headers
    size_t entryIndex = 0; // index into HistoryStore::entries()
  };

  void Rebuild();
  void LayoutSearch(int width);

  std::vector<Row> rows_;
  uint64_t builtRevision_ = 0;
  bool rebuild_ = true;
  std::wstring query_;  // lower-cased

  HWND searchEdit_ = nullptr;
  HBRUSH editBrush_ = nullptr;
  RECT searchBox_{};

  std::function<void(const std::wstring&)> onOpenUrl_;
};

}  // namespace vibrium

#endif  // VIBRIUM_HISTORY_H_