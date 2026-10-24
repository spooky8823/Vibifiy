#include "chrome/browser/vibrium/adblock/adblock_ffi.h"

#include <string>

#include "base/check.h"
#include "base/strings/utf_string_conversions.h"

// Generated/copied C ABI header from the Rust crate.
#include "vibrium_adblock.h"

namespace vibrium::adblock {

AdblockFfi::AdblockFfi() = default;

AdblockFfi::~AdblockFfi() {
  if (handle_) {
    vibrium_adblock_destroy(handle_);
    handle_ = nullptr;
  }
}

bool AdblockFfi::Initialize(const std::string& rules) {
  if (handle_) {
    vibrium_adblock_destroy(handle_);
    handle_ = nullptr;
  }

  if (vibrium_adblock_abi_version() != VIBRIUM_ADBLOCK_ABI_VERSION) {
    return false;
  }

  handle_ = vibrium_adblock_create_with_rules(rules.c_str());
  return handle_ != nullptr;
}

BlockingResult AdblockFfi::CheckRequest(const std::string& url,
                                        const std::string& source_url,
                                        const std::string& request_type) {
  if (!handle_) {
    return BlockingResult::kError;
  }

  const int result = vibrium_adblock_check(
      handle_, url.c_str(), source_url.c_str(), request_type.c_str());

  switch (result) {
    case VIBRIUM_ADBLOCK_BLOCKED:
      return BlockingResult::kBlocked;
    case VIBRIUM_ADBLOCK_ALLOWED:
      return BlockingResult::kAllowed;
    default:
      return BlockingResult::kError;
  }
}

bool AdblockFfi::is_initialized() const {
  return handle_ != nullptr;
}

}  // namespace vibrium::adblock
