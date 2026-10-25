// Copyright 2026 The Vibrium Authors
// SPDX-License-Identifier: MPL-2.0

#include "chrome/browser/vibrium/adblock/adblock_service_factory.h"

#include <memory>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/vibrium/adblock/adblock_service.h"
#include "content/public/browser/browser_context.h"

namespace vibrium::adblock {

AdblockServiceFactory* AdblockServiceFactory::GetInstance() {
  static base::NoDestructor<AdblockServiceFactory> instance;
  return instance.get();
}

AdblockService* AdblockServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<AdblockService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

AdblockServiceFactory::AdblockServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "VibriumAdblockService",
          BrowserContextDependencyManager::GetInstance()) {}

AdblockServiceFactory::~AdblockServiceFactory() = default;

content::BrowserContext* AdblockServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  // Keep filtering state separate for each regular profile and avoid sharing
  // it accidentally with unrelated browser contexts.
  return context;
}

std::unique_ptr<KeyedService>
AdblockServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  auto service = std::make_unique<AdblockService>();

  // Initialize the engine with an empty rule set for now. The filter-list
  // manager will supply downloaded and validated rules in a later step.
  if (!service->Initialize("")) {
    return nullptr;
  }

  return service;
}

}  // namespace vibrium::adblock
