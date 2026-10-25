 // Copyright 2026 The Vibrium Authors
 // SPDX-License-Identifier: MPL-2.0

#ifndef CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_URL_LOADER_THROTTLE_H_
#define CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_URL_LOADER_THROTTLE_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/vibrium/adblock/adblock_service.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"

namespace network {
struct ResourceRequest;
}

namespace vibrium::adblock {

// Applies Vibrium's Rust-backed filter engine to browser-side requests.
class AdblockURLLoaderThrottle final : public blink::URLLoaderThrottle {
 public:
  explicit AdblockURLLoaderThrottle(AdblockService* service);
  ~AdblockURLLoaderThrottle() override;

  AdblockURLLoaderThrottle(const AdblockURLLoaderThrottle&) = delete;
  AdblockURLLoaderThrottle& operator=(
      const AdblockURLLoaderThrottle&) = delete;

  void WillStartRequest(network::ResourceRequest* request,
                        bool* defer) override;

 private:
  raw_ptr<AdblockService> service_;
};

}  // namespace vibrium::adblock

#endif  // CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_URL_LOADER_THROTTLE_H_
