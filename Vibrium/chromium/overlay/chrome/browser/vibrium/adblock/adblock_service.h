// Copyright 2026 The Vibrium Authors
// SPDX-License-Identifier: MPL-2.0

#ifndef CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_SERVICE_H_
#define CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_SERVICE_H_

#include <memory>
#include <string>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/vibrium/adblock/adblock_ffi.h"
#include "components/keyed_service/core/keyed_service.h"

namespace vibrium::adblock {

class AdblockService : public KeyedService {
 public:
  AdblockService();
  ~AdblockService() override;

  AdblockService(const AdblockService&) = delete;
  AdblockService& operator=(const AdblockService&) = delete;

  bool Initialize(const std::string& rules);

  ::vibrium::BlockingResult CheckRequest(
      const std::string& url,
      const std::string& source_url,
      const std::string& request_type);

  bool is_initialized() const;

  base::WeakPtr<AdblockService> GetWeakPtr();

 private:
  std::unique_ptr<::vibrium::AdblockFfi> engine_;
  base::WeakPtrFactory<AdblockService> weak_ptr_factory_{this};
};

}  // namespace vibrium::adblock

#endif  // CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_SERVICE_H_
