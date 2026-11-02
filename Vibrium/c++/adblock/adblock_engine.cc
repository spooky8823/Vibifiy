#include "adblock_engine.h"

#include <utility>

#include "vibrium_adblock.h"

namespace vibrium {

AdblockEngine::AdblockEngine(void* handle) : handle_(handle) {}

AdblockEngine::~AdblockEngine() {
  vibrium_adblock_destroy(handle_);
}

std::shared_ptr<AdblockEngine> AdblockEngine::Adopt(void* handle) {
  if (!handle) {
    return nullptr;
  }
  // The constructor is private, so make_shared cannot be used.
  return std::shared_ptr<AdblockEngine>(new AdblockEngine(handle));
}

std::shared_ptr<AdblockEngine> AdblockEngine::FromDirectory(
    const std::string& utf8_directory) {
  if (vibrium_adblock_abi_version() != VIBRIUM_ADBLOCK_ABI_VERSION) {
    return nullptr;
  }
  return Adopt(vibrium_adblock_create_from_directory(utf8_directory.c_str()));
}

std::shared_ptr<AdblockEngine> AdblockEngine::FromRules(
    const std::string& rules) {
  if (vibrium_adblock_abi_version() != VIBRIUM_ADBLOCK_ABI_VERSION) {
    return nullptr;
  }
  return Adopt(vibrium_adblock_create_with_rules(rules.c_str()));
}

AdblockEngine::Decision AdblockEngine::Check(const std::string& url,
                                             const std::string& source_url,
                                             const char* request_type) const {
  switch (vibrium_adblock_check(handle_, url.c_str(), source_url.c_str(),
                                request_type)) {
    case VIBRIUM_ADBLOCK_BLOCKED:
      return Decision::kBlock;
    case VIBRIUM_ADBLOCK_ALLOWED:
      return Decision::kAllow;
    default:
      return Decision::kError;
  }
}

namespace {

// Copies a library-owned string into a std::string and releases it.
std::optional<std::string> TakeString(char* owned) {
  if (!owned) {
    return std::nullopt;
  }
  std::string result(owned);
  vibrium_adblock_string_free(owned);
  return result;
}

}  // namespace

std::optional<std::string> AdblockEngine::CosmeticCss(
    const std::string& page_url) const {
  return TakeString(vibrium_adblock_cosmetic_css(handle_, page_url.c_str()));
}

std::optional<std::string> AdblockEngine::CosmeticPayload(
    const std::string& page_url) const {
  return TakeString(
      vibrium_adblock_cosmetic_payload(handle_, page_url.c_str()));
}

std::optional<std::string> AdblockEngine::CosmeticScript(
    const std::string& page_url) const {
  return TakeString(vibrium_adblock_cosmetic_script(handle_, page_url.c_str()));
}

std::optional<std::string> AdblockEngine::GenericCss(
    const std::string& page_url,
    const std::string& classes_json,
    const std::string& ids_json) const {
  return TakeString(vibrium_adblock_generic_css(
      handle_, page_url.c_str(), classes_json.c_str(), ids_json.c_str()));
}

void AdblockEngineHolder::Set(std::shared_ptr<AdblockEngine> engine) {
  std::shared_ptr<AdblockEngine> previous;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    previous = std::exchange(engine_, std::move(engine));
  }
  // `previous` is released outside the lock; the Rust engine is destroyed
  // only when the last in-flight request drops its reference.
}

std::shared_ptr<AdblockEngine> AdblockEngineHolder::Get() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return engine_;
}

}  // namespace vibrium
