// CEF request interception backed by the Rust adblock engine.
//
// Flow (all on the CEF browser-process IO thread):
//   CefRequestHandler::GetResourceRequestHandler
//     -> AdblockResourceRequestHandler::OnBeforeResourceLoad / OnResourceRedirect
//        -> AdblockEngine::Check -> RV_CANCEL (blocked) or RV_CONTINUE.
//
// Failure policy: if there is no engine, the URL is not http(s), the engine
// reports an error, or CEF gives us no usable context, the request continues.
// Main-frame navigations are never cancelled.

#ifndef VIBRIUM_ADBLOCK_HANDLER_H_
#define VIBRIUM_ADBLOCK_HANDLER_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "adblock_engine.h"
#include "include/cef_request_handler.h"
#include "include/wrapper/cef_message_router.h"
#include "include/cef_resource_request_handler.h"
#include "request_policy.h"

namespace vibrium {

// Counters plus a bounded record of decisions; used for diagnostics and by
// the end-to-end selftest. Thread-safe.
class AdblockStats {
 public:
  struct Snapshot {
    uint64_t blocked = 0;
    uint64_t allowed = 0;
    uint64_t errors = 0;
    std::vector<std::string> blocked_urls;
    std::vector<std::string> allowed_urls;
  };

  void Record(AdblockEngine::Decision decision, const std::string& url);
  Snapshot Get() const;

 private:
  static constexpr size_t kMaxRecordedUrls = 512;
  mutable std::mutex mutex_;
  Snapshot data_;
};

ResourceKind ToResourceKind(cef_resource_type_t type);

class AdblockResourceRequestHandler : public CefResourceRequestHandler {
 public:
  AdblockResourceRequestHandler(std::shared_ptr<AdblockEngineHolder> engine,
                                std::shared_ptr<AdblockStats> stats,
                                std::string request_initiator);

  ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefRefPtr<CefRequest> request,
                                   CefRefPtr<CefCallback> callback) override;

  void OnResourceRedirect(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefRequest> request,
                          CefRefPtr<CefResponse> response,
                          CefString& new_url) override;

 private:
  AdblockEngine::Decision Evaluate(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefRequest> request,
                                   const std::string& url,
                                   ResourceKind kind);

  const std::shared_ptr<AdblockEngineHolder> engine_;
  const std::shared_ptr<AdblockStats> stats_;
  const std::string request_initiator_;

  IMPLEMENT_REFCOUNTING(AdblockResourceRequestHandler);
  DISALLOW_COPY_AND_ASSIGN(AdblockResourceRequestHandler);
};

class AdblockRequestHandler : public CefRequestHandler {
 public:
  AdblockRequestHandler(std::shared_ptr<AdblockEngineHolder> engine,
                        std::shared_ptr<AdblockStats> stats,
                        CefRefPtr<CefMessageRouterBrowserSide> router);

  // The message router needs to see navigations and renderer crashes to
  // cancel the pending queries of the page that went away.
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override;

  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status,
                                 int error_code,
                                 const CefString& error_string) override;

  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      bool is_navigation,
      bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;

 private:
  const std::shared_ptr<AdblockEngineHolder> engine_;
  const std::shared_ptr<AdblockStats> stats_;
  const CefRefPtr<CefMessageRouterBrowserSide> router_;

  IMPLEMENT_REFCOUNTING(AdblockRequestHandler);
  DISALLOW_COPY_AND_ASSIGN(AdblockRequestHandler);
};

}  // namespace vibrium

#endif  // VIBRIUM_ADBLOCK_HANDLER_H_
