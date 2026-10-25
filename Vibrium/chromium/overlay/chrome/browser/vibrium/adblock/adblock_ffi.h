// Copyright 2026 The Vibrium Authors
// SPDX-License-Identifier: MPL-2.0

#ifndef CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_FFI_H_
#define CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_FFI_H_

#include <cstddef>
#include <cstdint>
#include <string>

namespace vibrium {

enum class BlockingResult {
  kError = -1,
  kAllowed = 0,
  kBlocked = 1,
};

class AdblockFfi {
 public:
  AdblockFfi();
  ~AdblockFfi();

  AdblockFfi(const AdblockFfi&) = delete;
  AdblockFfi& operator=(const AdblockFfi&) = delete;

  bool Initialize(const std::string& rules);

  BlockingResult CheckRequest(
      const std::string& url,
      const std::string& source_url,
      const std::string& request_type);

  bool ValidateFilterResponse(
      const std::string& name,
      const std::string& source_url,
      uint16_t status,
      size_t content_length,
      const uint8_t* body,
      size_t body_len);

  bool is_initialized() const;

 private:
  void* handle_ = nullptr;
};

}  // namespace vibrium

#endif  // CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_FFI_H_
