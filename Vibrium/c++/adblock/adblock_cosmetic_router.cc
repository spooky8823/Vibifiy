#include "adblock_cosmetic_router.h"

#include <utility>

#include "request_policy.h"

namespace vibrium {

CosmeticQueryHandler::CosmeticQueryHandler(
    std::shared_ptr<AdblockEngineHolder> engine,
    std::shared_ptr<AdblockPolicy> policy)
    : engine_(std::move(engine)), policy_(std::move(policy)) {}

bool CosmeticQueryHandler::OnQuery(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   int64_t query_id,
                                   const CefString& request,
                                   bool persistent,
                                   CefRefPtr<Callback> callback) {
  // Not ours: let the router report failure to the page.
  if (persistent || !frame || !frame->IsValid()) {
    return false;
  }

  const std::string text = request.ToString();
  auto query = ParseGenericQuery(text);
  if (!query) {
    return false;
  }

  // The browser's own view of the page, not anything the page claims.
  const std::string url = frame->GetURL().ToString();
  if (!IsFilterableUrl(url)) {
    return false;
  }

  const std::shared_ptr<AdblockEngine> engine = engine_->Get();
  if (!engine) {
    return false;
  }

  // Blocking off, cosmetic filtering off or an allowlisted site: answer with
  // "nothing to hide" so the page script stops asking.
  if (policy_) {
    // The allowlist is about the page in the address bar, not the (possibly
    // third-party) frame that is asking.
    std::string top = url;
    if (browser) {
      if (CefRefPtr<CefFrame> main = browser->GetMainFrame()) {
        const std::string main_url = main->GetURL().ToString();
        if (IsFilterableUrl(main_url)) top = main_url;
      }
    }
    if (!policy_->cosmetic_enabled() || policy_->ShouldBypass(top)) {
      callback->Success(std::string());
      return true;
    }
  }

  auto css = engine->GenericCss(url, query->classes_json, query->ids_json);
  callback->Success(css ? *css : std::string());
  return true;
}

}  // namespace vibrium
