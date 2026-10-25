#ifndef CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_SERVICE_H_
#define CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_SERVICE_H_

#include <memory>
#include <string>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/vibrium/adblock/adblock_ffi.h"

namespace vibrium::adblock {

// Owns the Rust-backed ad-blocking engine for a browser context.
//
// This is deliberately independent of Profile and KeyedService for now.
// Chromium's keyed-service integration will be added when the browser
// integration layer and its build targets are connected.
class AdblockService {
 public:
  AdblockService();
  ~AdblockService();

  AdblockService(const AdblockService&) = delete;
  AdblockService& operator=(const AdblockService&) = delete;

  // Initializes or replaces the current engine using filter rules.
  // Returns false if the Rust engine cannot be initialized.
  bool Initialize(const std::string& rules);

  // Checks a network request against the initialized engine.
  BlockingResult CheckRequest(const std::string& url,
                              const std::string& source_url,
                              const std::string& request_type);

  bool is_initialized() const;

  base::WeakPtr<AdblockService> GetWeakPtr();

 private:
  std::unique_ptr<AdblockFfi> engine_;
  base::WeakPtrFactory<AdblockService> weak_ptr_factory_{this};
};

}  // namespace vibrium::adblock

#endif  // CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_SERVICE_H_
