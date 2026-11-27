// Browser-side endpoint for the generic-selector lookups made by
// cosmetic_content.js through window.cefQuery.
//
// Why this exists: generic cosmetic filters (`##.ad`, `###banner`) are only
// worth sending for class names and ids that actually occur on the page, and
// only the renderer can see the DOM. The page script reports what it sees, and
// this handler answers with CSS for the matching rules.
//
// Security: the page is untrusted and window.cefQuery is reachable by any
// script on it. So this handler (1) takes the page URL from the browser's own
// frame object, never from the request, (2) accepts only a tiny fixed request
// format and bounds its size, (3) hands the strings to the Rust engine, which
// validates them again, and (4) only ever answers with CSS built from rules in
// the loaded filter lists.

#ifndef VIBRIUM_ADBLOCK_COSMETIC_ROUTER_H_
#define VIBRIUM_ADBLOCK_COSMETIC_ROUTER_H_

#include <memory>

#include "adblock_engine.h"
#include "adblock_policy.h"
#include "include/wrapper/cef_message_router.h"

namespace vibrium {

// The same configuration must be used in the browser and renderer processes.
inline CefMessageRouterConfig CosmeticRouterConfig() {
  return CefMessageRouterConfig();  // window.cefQuery / window.cefQueryCancel
}

class CosmeticQueryHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  // With a `policy`, pages where blocking is off (or cosmetic filtering is
  // switched off, or the site is allowlisted) get an empty answer.
  explicit CosmeticQueryHandler(std::shared_ptr<AdblockEngineHolder> engine,
                                std::shared_ptr<AdblockPolicy> policy = nullptr);

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override;

 private:
  const std::shared_ptr<AdblockEngineHolder> engine_;
  const std::shared_ptr<AdblockPolicy> policy_;
};

}  // namespace vibrium

#endif  // VIBRIUM_ADBLOCK_COSMETIC_ROUTER_H_
