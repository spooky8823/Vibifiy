#ifndef CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_FFI_H_
#define CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_FFI_H_

#include <string>

#include "base/functional/callback_forward.h"

namespace vibrium::adblock {

// Represents the result of checking a network request.
enum class BlockingResult {
  kError = -1,
  kAllowed = 0,
  kBlocked = 1,
};

// Owns a Rust-backed ad-blocking engine.
//
// This class is non-copyable. Its implementation must be linked against
// the vibrium_adblock Rust static library.
class AdblockFfi {
 public:
  AdblockFfi();
  ~AdblockFfi();

  AdblockFfi(const AdblockFfi&) = delete;
  AdblockFfi& operator=(const AdblockFfi&) = delete;

  // Creates an engine from validated filter rules.
  // Returns false if engine creation fails.
  bool Initialize(const std::string& rules);

  // Checks a request. The strings are UTF-8.
  BlockingResult CheckRequest(const std::string& url,
                              const std::string& source_url,
                              const std::string& request_type);

  bool is_initialized() const;

 private:
  void* handle_ = nullptr;
};

}  // namespace vibrium::adblock

#endif  // CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_FFI_H_
