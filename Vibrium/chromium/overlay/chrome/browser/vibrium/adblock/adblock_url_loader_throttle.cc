 // Copyright 2026 The Vibrium Authors
 // SPDX-License-Identifier: MPL-2.0

#include "chrome/browser/vibrium/adblock/adblock_url_loader_throttle.h"

#include <string>

#include "net/base/net_errors.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "url/origin.h"

namespace vibrium::adblock {
namespace {

const char* GetRequestType(network::mojom::RequestDestination destination) {
  switch (destination) {
    case network::mojom::RequestDestination::kDocument:
    case network::mojom::RequestDestination::kIframe:
    case network::mojom::RequestDestination::kFrame:
      return "document";
    case network::mojom::RequestDestination::kScript:
      return "script";
    case network::mojom::RequestDestination::kStyle:
      return "stylesheet";
    case network::mojom::RequestDestination::kImage:
      return "image";
    case network::mojom::RequestDestination::kFont:
      return "font";
    case network::mojom::RequestDestination::kAudio:
    case network::mojom::RequestDestination::kVideo:
      return "media";
    case network::mojom::RequestDestination::kEmpty:
      return "other";
    default:
      return "other";
  }
}

}  // namespace

AdblockURLLoaderThrottle::AdblockURLLoaderThrottle(AdblockService* service)
    : service_(service) {}

AdblockURLLoaderThrottle::~AdblockURLLoaderThrottle() = default;

void AdblockURLLoaderThrottle::WillStartRequest(
    network::ResourceRequest* request,
    bool* defer) {
  if (!request || !service_ || !service_->is_initialized()) {
    return;
  }

  // Only filter web requests. Browser-internal schemes should not be sent
  // through the network content-blocking engine.
  if (!request->url.SchemeIsHTTPOrHTTPS()) {
    return;
  }

  std::string source_url;
  if (request->request_initiator.has_value()) {
    source_url = request->request_initiator->GetURL().spec();
  }

  const auto result = service_->CheckRequest(
      request->url.spec(),
      source_url,
      GetRequestType(request->destination));

  if (result == ::vibrium::BlockingResult::kBlocked && delegate_) {
    delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT,
                               "Blocked by Vibrium content filtering");
  }

  // Fail open on engine errors: a filtering failure must not break browsing.
  // No asynchronous deferral is performed by this throttle.
  if (defer) {
    *defer = false;
  }
}

}  // namespace vibrium::adblock
