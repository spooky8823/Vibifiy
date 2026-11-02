// Process-wide ad-blocking facade used by Vibrium's browser window code.
// Owns the engine holder, the decision statistics and the CEF request
// handler, so main.cc only has to (1) initialize it once and (2) return its
// request handler / call ApplyCosmeticFilters from each tab's client.

#ifndef VIBRIUM_ADBLOCK_SERVICE_H_
#define VIBRIUM_ADBLOCK_SERVICE_H_

#include <memory>
#include <string>

#include "adblock_engine.h"
#include "adblock_cosmetic_router.h"
#include "adblock_handler.h"
#include "include/cef_command_line.h"
#include "include/cef_frame.h"
#include "include/cef_process_message.h"
#include "include/cef_request_handler.h"

namespace vibrium {

class AdblockService {
 public:
  static AdblockService& Get();

  // Loads <name>.txt filter lists from `--filter-dir=<dir>` (default:
  // <executable directory>/filters). Call once on the UI thread before the
  // first browser is created. If no usable list is found the browser keeps
  // working without blocking (fail open) and a warning is logged.
  // Returns true if blocking is active.
  bool Initialize(CefRefPtr<CefCommandLine> command_line);

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
  // The directory Initialize() looked in (empty before Initialize()).
  const std::string& filter_dir() const { return filter_dir_; }

  AdblockStats::Snapshot Stats() const { return stats_->Get(); }

 private:
  AdblockService();

  std::shared_ptr<AdblockEngineHolder> engine_;
  std::shared_ptr<AdblockStats> stats_;
  // Declared before the router: the router is destroyed first.
  std::unique_ptr<CosmeticQueryHandler> cosmetic_handler_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  CefRefPtr<CefRequestHandler> request_handler_;
  bool enabled_ = false;
  std::string filter_dir_;
};

}  // namespace vibrium

#endif  // VIBRIUM_ADBLOCK_SERVICE_H_
