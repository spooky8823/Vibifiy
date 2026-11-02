#include "adblock_handler.h"

#include <utility>

#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "include/cef_request.h"
#include "include/cef_response.h"

namespace vibrium {

void AdblockStats::Record(AdblockEngine::Decision decision,
                          const std::string& url) {
  std::lock_guard<std::mutex> lock(mutex_);
  switch (decision) {
    case AdblockEngine::Decision::kBlock:
      ++data_.blocked;
      if (data_.blocked_urls.size() < kMaxRecordedUrls) {
        data_.blocked_urls.push_back(url);
      }
      break;
    case AdblockEngine::Decision::kAllow:
      ++data_.allowed;
      if (data_.allowed_urls.size() < kMaxRecordedUrls) {
        data_.allowed_urls.push_back(url);
      }
      break;
    case AdblockEngine::Decision::kError:
      ++data_.errors;
      break;
  }
}

AdblockStats::Snapshot AdblockStats::Get() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return data_;
}

ResourceKind ToResourceKind(cef_resource_type_t type) {
  switch (type) {
    case RT_MAIN_FRAME:
    case RT_NAVIGATION_PRELOAD_MAIN_FRAME:
      return ResourceKind::kMainFrame;
    case RT_SUB_FRAME:
    case RT_NAVIGATION_PRELOAD_SUB_FRAME:
      return ResourceKind::kSubFrame;
    case RT_STYLESHEET:
      return ResourceKind::kStylesheet;
    case RT_SCRIPT:
      return ResourceKind::kScript;
    case RT_IMAGE:
    case RT_FAVICON:
      return ResourceKind::kImage;
    case RT_FONT_RESOURCE:
      return ResourceKind::kFont;
    case RT_MEDIA:
      return ResourceKind::kMedia;
    case RT_XHR:
      return ResourceKind::kXhr;
    case RT_PING:
      return ResourceKind::kPing;
    case RT_CSP_REPORT:
      return ResourceKind::kCspReport;
    case RT_OBJECT:
    case RT_PLUGIN_RESOURCE:
      return ResourceKind::kObject;
    default:  // RT_SUB_RESOURCE, workers, prefetch, and future values.
      return ResourceKind::kOther;
  }
}

namespace {

// The page that "owns" a request, used by adblock-rust for $third-party and
// $domain. Like Brave, prefer the top-level document over the (possibly
// third-party) frame that issued the request. An empty result makes the engine
// treat the request as third-party, so only fall back to it as a last resort.
std::string ChooseSourceUrl(CefRefPtr<CefBrowser> browser,
                            const std::string& request_initiator,
                            CefRefPtr<CefRequest> request) {
  if (browser) {
    CefRefPtr<CefFrame> main_frame = browser->GetMainFrame();
    if (main_frame) {
      std::string top = main_frame->GetURL().ToString();
      if (IsFilterableUrl(top)) {
        return top;
      }
    }
  }
  if (IsFilterableUrl(request_initiator)) {
    return request_initiator;
  }
  std::string referrer = request->GetReferrerURL().ToString();
  if (IsFilterableUrl(referrer)) {
    return referrer;
  }
  return std::string();
}

}  // namespace

AdblockResourceRequestHandler::AdblockResourceRequestHandler(
    std::shared_ptr<AdblockEngineHolder> engine,
    std::shared_ptr<AdblockStats> stats,
    std::string request_initiator)
    : engine_(std::move(engine)),
      stats_(std::move(stats)),
      request_initiator_(std::move(request_initiator)) {}

AdblockEngine::Decision AdblockResourceRequestHandler::Evaluate(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefRequest> request,
    const std::string& url,
    ResourceKind kind) {
  if (!ShouldEvaluate(kind, url)) {
    return AdblockEngine::Decision::kAllow;
  }
  // Holding the shared_ptr for the whole check keeps the Rust engine alive
  // even if it is replaced concurrently.
  std::shared_ptr<AdblockEngine> engine = engine_ ? engine_->Get() : nullptr;
  if (!engine) {
    return AdblockEngine::Decision::kAllow;
  }

  const AdblockEngine::Decision decision = engine->Check(
      url, ChooseSourceUrl(browser, request_initiator_, request),
      AdblockTypeName(kind));
  if (stats_) {
    stats_->Record(decision, url);
  }
  return decision;
}

CefResourceRequestHandler::ReturnValue
AdblockResourceRequestHandler::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> /*frame*/,  // may be null; the top-level URL is used
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefCallback> /*callback*/) {
  if (!request) {
    return RV_CONTINUE;
  }
  const AdblockEngine::Decision decision =
      Evaluate(browser, request, request->GetURL().ToString(),
               ToResourceKind(request->GetResourceType()));
  // kError deliberately falls through to RV_CONTINUE (fail open).
  return decision == AdblockEngine::Decision::kBlock ? RV_CANCEL : RV_CONTINUE;
}

void AdblockResourceRequestHandler::OnResourceRedirect(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> /*frame*/,
    CefRefPtr<CefRequest> request,
    CefRefPtr<CefResponse> /*response*/,
    CefString& new_url) {
  if (!request) {
    return;
  }
  // A redirect cannot be cancelled from this callback, only retargeted.
  // Retargeting a blocked redirect to about:blank makes the load fail
  // (cross-scheme redirects of subresources are rejected by the network
  // stack) instead of fetching from the blocked host.
  const AdblockEngine::Decision decision =
      Evaluate(browser, request, new_url.ToString(),
               ToResourceKind(request->GetResourceType()));
  if (decision == AdblockEngine::Decision::kBlock) {
    new_url = "about:blank";
  }
}

AdblockRequestHandler::AdblockRequestHandler(
    std::shared_ptr<AdblockEngineHolder> engine,
    std::shared_ptr<AdblockStats> stats,
    CefRefPtr<CefMessageRouterBrowserSide> router)
    : engine_(std::move(engine)),
      stats_(std::move(stats)),
      router_(std::move(router)) {}

bool AdblockRequestHandler::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                           CefRefPtr<CefFrame> frame,
                                           CefRefPtr<CefRequest> /*request*/,
                                           bool /*user_gesture*/,
                                           bool /*is_redirect*/) {
  if (router_) {
    router_->OnBeforeBrowse(browser, frame);
  }
  return false;  // never cancels navigation
}

void AdblockRequestHandler::OnRenderProcessTerminated(
    CefRefPtr<CefBrowser> browser,
    TerminationStatus /*status*/,
    int /*error_code*/,
    const CefString& /*error_string*/) {
  if (router_) {
    router_->OnRenderProcessTerminated(browser);
  }
}

CefRefPtr<CefResourceRequestHandler>
AdblockRequestHandler::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefFrame> /*frame*/,
    CefRefPtr<CefRequest> /*request*/,
    bool /*is_navigation*/,
    bool /*is_download*/,
    const CefString& request_initiator,
    bool& /*disable_default_handling*/) {
  // Per-request instance so the initiator (only available here) reaches
  // OnBeforeResourceLoad. Default handling stays enabled.
  return new AdblockResourceRequestHandler(engine_, stats_,
                                           request_initiator.ToString());
}

}  // namespace vibrium
