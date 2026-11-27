// Process-wide ad-blocking facade used by Vibrium's browser window code.
// Owns the engine holder, the decision statistics and the CEF request
// handler, so main.cc only has to (1) initialize it once and (2) return its
// request handler / call ApplyCosmeticFilters from each tab's client.

#ifndef VIBRIUM_ADBLOCK_SERVICE_H_
#define VIBRIUM_ADBLOCK_SERVICE_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "adblock_engine.h"
#include "adblock_cosmetic_router.h"
#include "adblock_handler.h"
#include "adblock_history.h"
#include "adblock_policy.h"
#include "filter_list_updater.h"
#include "include/cef_command_line.h"
#include "include/cef_frame.h"
#include "include/cef_process_message.h"
#include "include/cef_request_handler.h"

namespace vibrium {

class AdblockService {
 public:
  static AdblockService& Get();

  // Loads <name>.txt filter lists. With `--filter-dir=<dir>` exactly that
  // directory is used and nothing is downloaded (tests, CI). Otherwise lists
  // live in the per-user directory (%LOCALAPPDATA%\\Vibrium\\filters): the
  // shipped <executable directory>/filters are copied in, cached downloads are
  // loaded, and FilterListUpdater refreshes them in the background (disable with
  // `--no-filter-updates`). Call once on the UI thread before the
  // first browser is created. If no usable list is found the browser keeps
  // working without blocking (fail open) and a warning is logged.
  // Returns true if blocking is active.
  bool Initialize(CefRefPtr<CefCommandLine> command_line);

  // Release every CEF-owned reference while CEF is still initialized.
  // Call on the browser UI thread after all browsers have closed.
  void Shutdown();

  // Shared by every tab's CefClient::GetRequestHandler().
  CefRefPtr<CefRequestHandler> request_handler() const {
    return request_handler_;
  }

  // Applies cosmetic filtering to `frame`: scriptlets, hiding CSS, procedural
  // filters and the generic class/id lookup (see cosmetic_content.js). Call
  // from CefLoadHandler::OnLoadStart. UI thread only.
  void ApplyCosmeticFilters(CefRefPtr<CefFrame> frame) const;

  // Forward these from the tab's CefClient / CefLifeSpanHandler so the page
  // script's window.cefQuery calls reach the cosmetic query handler.
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message);
  void OnBrowserClosed(CefRefPtr<CefBrowser> browser);

  // True once usable filter lists were loaded. Use it to tell "blocking is
  // running" apart from "the browser started but blocks nothing".
  bool enabled() const { return enabled_; }
  // Lists are loaded AND the user has not switched ad blocking off.
  bool blocking_active() const { return enabled_ && policy_->blocking_enabled(); }
  // The directory Initialize() looked in (empty before Initialize()).
  const std::string& filter_dir() const { return filter_dir_; }
  // The directory the engine's lists are really read from: filter_dir(), or
  // a staging subdirectory while the user has turned some lists off. Child
  // processes (scriptlets) read their lists from here.
  std::string engine_dir() const;
  // Full path of the policy file that renderers poll (UTF-8).
  std::string policy_file() const;

  // ---- User controls (Settings > Ad blocker). UI thread unless noted. ----

  // Master switch, cosmetic/scriptlet switches and the allowlist.
  AdblockPolicy& policy() { return *policy_; }
  // Lifetime and per-session counts.
  AdblockHistory& history() { return *history_; }

  struct FilterListInfo {
    std::string name;         // file stem, e.g. "easylist"
    std::string title;        // e.g. "EasyList"
    std::string description;  // one short line
    bool enabled = true;      // not turned off by the user
    bool present = false;     // downloaded (or shipped) and on disk
    uint64_t updated_ms = 0;  // file time, ms since the Unix epoch (0 = none)
    uint64_t size_bytes = 0;
  };
  std::vector<FilterListInfo> FilterLists() const;
  // Turns a catalog list on or off and rebuilds the engine in the background.
  // Returns false if nothing changed, or if it would leave no rules at all.
  bool SetFilterListEnabled(const std::string& name, bool enabled);

  // False when lists cannot be refreshed in this session (explicit
  // --filter-dir, --no-filter-updates, or no per-user directory).
  bool can_update_lists() const { return updater_ != nullptr; }
  bool updating_lists() const { return updating_; }
  // Newest filter-list file time (ms since the Unix epoch), 0 if none.
  uint64_t ListsUpdatedMs() const;
  // Checks every list now. `done` runs on the UI thread. Returns false if a
  // check is already running or updates are unavailable.
  bool UpdateListsNow(std::function<void(const UpdateResult&)> done);

  // The user's own rules (EasyList/uBlock syntax), stored next to the lists.
  enum class CustomRulesStatus {
    kSaved,        // validated, written, engine rebuilding
    kCleared,      // text was empty; the rules file was removed
    kInvalid,      // no valid rules in the text; nothing was changed
    kTooLarge,
    kWriteFailed,
    kUnavailable,  // no writable filter directory
  };
  std::string LoadCustomRules() const;
  CustomRulesStatus SaveCustomRules(const std::string& text);

  AdblockStats::Snapshot Stats() const { return stats_->Get(); }
  void ClearStats() { stats_->Clear(); }

 private:
  AdblockService();

  // Builds an engine from the enabled lists (and the custom rules). Thread
  // safe; null if nothing usable could be built.
  std::shared_ptr<AdblockEngine> BuildEngine();
  void ReloadEngineAsync();

  std::shared_ptr<AdblockEngineHolder> engine_;
  std::shared_ptr<AdblockStats> stats_;
  // Declared before the handlers that receive them.
  std::shared_ptr<AdblockPolicy> policy_;
  std::shared_ptr<AdblockHistory> history_;
  // Declared before the router: the router is destroyed first.
  std::unique_ptr<CosmeticQueryHandler> cosmetic_handler_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  CefRefPtr<CefRequestHandler> request_handler_;
  std::atomic<bool> enabled_{false};
  std::atomic<bool> shutting_down_{false};
  bool updating_ = false;  // UI thread only
  std::string filter_dir_;
  mutable std::mutex build_mutex_;  // guards engine_dir_ and engine rebuilds
  std::string engine_dir_;          // empty = filter_dir_
  std::shared_ptr<FilterListUpdater> updater_;
};

}  // namespace vibrium

#endif  // VIBRIUM_ADBLOCK_SERVICE_H_
