// Copyright 2026 The Vibrium Authors
// SPDX-License-Identifier: MPL-2.0

#include <cstdint>
#include <iostream>
#include <string>

#include "vibrium_adblock.h"

namespace {

int failures = 0;

void Expect(bool condition, const char* description) {
  if (condition) {
    std::cout << "[PASS] " << description << '\n';
  } else {
    std::cerr << "[FAIL] " << description << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  // Use a rule with a dedicated hostname so the test is deterministic.
  const char* rules = "||ads.vibrium-test.example^\n";

  void* engine = vibrium_adblock_create_with_rules(rules);

  Expect(engine != nullptr, "Create engine through the Rust C ABI");

  if (engine == nullptr) {
    std::cerr << failures << " test(s) failed.\n";
    return 1;
  }

  // ABI contract: 1 = blocked, 0 = allowed, -1 = error.
  const int blocked = vibrium_adblock_check(
      engine,
      "https://ads.vibrium-test.example/banner.js",
      "https://www.vibrium-test.example/",
      "script");

  Expect(blocked == 1, "Block a URL matching the filter rule");

  const int allowed = vibrium_adblock_check(
      engine,
      "https://cdn.vibrium-test.example/app.js",
      "https://www.vibrium-test.example/",
      "script");

  Expect(allowed == 0, "Allow a URL that does not match the filter rule");

  Expect(vibrium_adblock_abi_version() > 0,
         "Expose a nonzero ABI version");

  vibrium_adblock_destroy(engine);
  engine = nullptr;

  // The Rust ABI explicitly permits destroying a null handle.
  vibrium_adblock_destroy(engine);
  Expect(true, "Destroy engine and safely destroy a null handle");

  if (failures != 0) {
    std::cerr << failures << " test(s) failed.\n";
    return 1;
  }

  std::cout << "All native C++ FFI smoke tests passed.\n";
  return 0;
}
