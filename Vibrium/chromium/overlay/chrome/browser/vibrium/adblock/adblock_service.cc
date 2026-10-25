#include "chrome/browser/vibrium/adblock/adblock_service.h"

#include <memory>
#include <string>
#include <utility>

namespace vibrium::adblock {

AdblockService::AdblockService() = default;

AdblockService::~AdblockService() = default;

bool AdblockService::Initialize(const std::string& rules) {
  auto engine = std::make_unique<AdblockFfi>();

  if (!engine->Initialize(rules)) {
    return false;
  }

  engine_ = std::move(engine);
  return true;
}

BlockingResult AdblockService::CheckRequest(
    const std::string& url,
    const std::string& source_url,
    const std::string& request_type) {
  if (!engine_ || !engine_->is_initialized()) {
    return BlockingResult::kError;
  }

  return engine_->CheckRequest(url, source_url, request_type);
}

bool AdblockService::is_initialized() const {
  return engine_ && engine_->is_initialized();
}

base::WeakPtr<AdblockService> AdblockService::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

}  // namespace vibrium::adblock
