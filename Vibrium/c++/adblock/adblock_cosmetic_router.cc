#include "adblock_cosmetic_router.h"

#include <utility>

#include "request_policy.h"

namespace vibrium {

CosmeticQueryHandler::CosmeticQueryHandler(
    std::shared_ptr<AdblockEngineHolder> engine)
    : engine_(std::move(engine)) {}

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

  auto css = engine->GenericCss(url, query->classes_json, query->ids_json);
  callback->Success(css ? *css : std::string());
  return true;
}

}  // namespace vibrium
