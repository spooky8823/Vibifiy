// Copyright 2026 The Vibrium Authors
// SPDX-License-Identifier: MPL-2.0

#include "chrome/browser/vibrium/adblock/adblock_service_factory.h"

#include <memory>
#include <string>

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/vibrium/adblock/adblock_service.h"
#include "content/public/browser/browser_context.h"

namespace vibrium::adblock {
namespace {

// Temporary bootstrap rules. A proper filter-list manager will replace these
// with validated, updateable lists.
constexpr char kBootstrapFilterRules[] = R"FILTER(
! Vibrium bootstrap rules
||doubleclick.net^
||googlesyndication.com^
||googleadservices.com^
||adservice.google.com^
||adnxs.com^
||adform.net^
||ads.yahoo.com^
||taboola.com^
||outbrain.com^
)FILTER";

}  // namespace

AdblockServiceFactory* AdblockServiceFactory::GetInstance() {
  static base::NoDestructor<AdblockServiceFactory> instance;
  return instance.get();
}

AdblockService* AdblockServiceFactory::GetForProfile(Profile* profile) {
  if (!profile) {
    return nullptr;
  }

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
  return context;
}

std::unique_ptr<KeyedService>
AdblockServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  auto service = std::make_unique<AdblockService>();

  if (!service->Initialize(kBootstrapFilterRules)) {
    return nullptr;
  }

  return service;
}

}  // namespace vibrium::adblock
