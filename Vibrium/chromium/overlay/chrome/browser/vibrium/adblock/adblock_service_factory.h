// Copyright 2026 The Vibrium Authors
// SPDX-License-Identifier: MPL-2.0

#ifndef CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_SERVICE_FACTORY_H_
#define CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_SERVICE_FACTORY_H_

#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

namespace vibrium::adblock {

class AdblockServiceFactory final
    : public BrowserContextKeyedServiceFactory {
 public:
  static AdblockServiceFactory* GetInstance();
  static AdblockService* GetForProfile(Profile* profile);

  AdblockServiceFactory(const AdblockServiceFactory&) = delete;
  AdblockServiceFactory& operator=(const AdblockServiceFactory&) = delete;

 private:
  friend base::NoDestructor<AdblockServiceFactory>;

  AdblockServiceFactory();
  ~AdblockServiceFactory() override;

  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace vibrium::adblock

#endif  // CHROME_BROWSER_VIBRIUM_ADBLOCK_ADBLOCK_SERVICE_FACTORY_H_
