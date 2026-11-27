#include "settings/settings_privacy.h"

#include "adblock/adblock_service.h"
#include "history.h"
#include "include/cef_cookie.h"

namespace vibrium {

void PrivacySection::BuildRows(std::vector<Row>* rows) {
  AdblockPolicy& policy = AdblockService::Get().policy();

  rows->push_back(Toggle(
      L"Do Not Track and Global Privacy Control",
      L"Ask websites not to track you. Sent with every request.",
      policy.privacy_headers_enabled(), [this]() {
        AdblockPolicy& p = AdblockService::Get().policy();
        p.SetPrivacyHeaders(!p.privacy_headers_enabled());
        Flash(p.privacy_headers_enabled()
                  ? L"Privacy signals are on. They apply to new requests."
                  : L"Privacy signals are off.");
      }));

  rows->push_back(Button(
      L"Clear history", L"Removes every page from your browsing history.",
      L"Clear history", [this]() {
        if (MessageBoxW(host_->window(),
                        L"Remove all pages from your browsing history?",
                        L"Clear history", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
          return;
        }
        HistoryStore::Get().Clear();
        Flash(L"History cleared.");
      }));

  rows->push_back(Button(
      L"Clear cookies", L"Signs you out of websites. Saved passwords are not affected.",
      L"Clear cookies", [this]() {
        if (MessageBoxW(host_->window(),
                        L"Delete all cookies? You will be signed out of websites.",
                        L"Clear cookies", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
          return;
        }
        CefRefPtr<CefCookieManager> manager = CefCookieManager::GetGlobalManager(nullptr);
        if (manager && manager->DeleteCookies(CefString(), CefString(), nullptr)) {
          Flash(L"Cookies cleared.");
        } else {
          Flash(L"Could not clear cookies.");
        }
      }));
}

}  // namespace vibrium
