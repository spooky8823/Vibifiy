// Copyright 2026 The Vibrium Authors
// SPDX-License-Identifier: MPL-2.0

#include "chrome/browser/vibrium/adblock/adblock_ffi.h"

#include <limits>
#include <string>

#include "vibrium_adblock.h"

namespace vibrium {

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

  handle_ = vibrium_adblock_create_with_rules(rules.c_str());
  return handle_ != nullptr;
}

BlockingResult AdblockFfi::CheckRequest(
    const std::string& url,
    const std::string& source_url,
    const std::string& request_type) {
  if (!handle_) {
    return BlockingResult::kError;
  }

  const int32_t result = vibrium_adblock_check(
      handle_,
      url.c_str(),
      source_url.c_str(),
      request_type.c_str());

  switch (result) {
    case VIBRIUM_ADBLOCK_ALLOWED:
      return BlockingResult::kAllowed;
    case VIBRIUM_ADBLOCK_BLOCKED:
      return BlockingResult::kBlocked;
    default:
      return BlockingResult::kError;
  }
}

bool AdblockFfi::ValidateFilterResponse(
    const std::string& name,
    const std::string& source_url,
    uint16_t status,
    size_t content_length,
    const uint8_t* body,
    size_t body_len) {
  if ((body == nullptr && body_len != 0) ||
      body_len > 10U * 1024U * 1024U) {
    return false;
  }

  const int32_t result = vibrium_adblock_validate_filter_response(
      name.c_str(),
      source_url.c_str(),
      status,
      content_length,
      body,
      body_len);

  return result == 0;
}

bool AdblockFfi::is_initialized() const {
  return handle_ != nullptr;
}

}  // namespace vibrium
