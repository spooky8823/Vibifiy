 // Copyright 2026 The Vibrium Authors
 // SPDX-License-Identifier: MPL-2.0

#include "chrome/browser/vibrium/adblock/adblock_url_loader_throttle_factory.h"

#include <memory>

namespace vibrium::adblock {

std::unique_ptr<blink::URLLoaderThrottle> CreateAdblockURLLoaderThrottle(
    AdblockService* service) {
  if (!service || !service->is_initialized()) {
    return nullptr;
  }

  return std::make_unique<AdblockURLLoaderThrottle>(service);
}

}  // namespace vibrium::adblock
