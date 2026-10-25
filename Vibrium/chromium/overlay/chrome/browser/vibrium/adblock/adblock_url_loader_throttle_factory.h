 // Copyright 2026 The Vibrium Authors
 // SPDX-License-Identifier: MPL-2.0

#ifndef CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_URL_LOADER_THROTTLE_FACTORY_H_
#define CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_URL_LOADER_THROTTLE_FACTORY_H_

#include <memory>

#include "chrome/browser/vibrium/adblock/adblock_url_loader_throttle.h"

namespace vibrium::adblock {

// Creates a request throttle using the caller-owned filtering service.
// The service must outlive the returned throttle.
std::unique_ptr<blink::URLLoaderThrottle> CreateAdblockURLLoaderThrottle(
    AdblockService* service);

}  // namespace vibrium::adblock

#endif  // CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_URL_LOADER_THROTTLE_FACTORY_H_
