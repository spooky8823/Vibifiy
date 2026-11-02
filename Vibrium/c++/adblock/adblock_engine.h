// RAII owner for a Rust adblock engine handle.
//
// Ownership rules (see rust/vibrium-adblock/include/vibrium_adblock.h):
//  * An engine is immutable after creation and may be queried from any thread.
//  * vibrium_adblock_destroy() must not race with a query on the same handle,
//    so engines are only ever shared through std::shared_ptr: every in-flight
//    request holds a reference and the handle is destroyed after the last one.
//  * Replacing the active engine (e.g. after a filter-list update) is done by
//    AdblockEngineHolder::Set(); requests already running keep the old engine.
//
// Failure policy: nothing here throws. Any error is reported as kError, which
// callers must treat as "allow" (fail open); a broken filter engine must never
// make the browser unusable.

#ifndef VIBRIUM_ADBLOCK_ENGINE_H_
#define VIBRIUM_ADBLOCK_ENGINE_H_

#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace vibrium {

class AdblockEngine {
 public:
  enum class Decision { kAllow, kBlock, kError };

  // Returns null if the library ABI does not match, the directory is
  // unreadable, or no usable rules were found.
  static std::shared_ptr<AdblockEngine> FromDirectory(
      const std::string& utf8_directory);
  static std::shared_ptr<AdblockEngine> FromRules(const std::string& rules);

  ~AdblockEngine();
  AdblockEngine(const AdblockEngine&) = delete;
  AdblockEngine& operator=(const AdblockEngine&) = delete;

  // `request_type` is one of the type names accepted by adblock-rust
  // (see request_policy.h). Thread-safe.
  Decision Check(const std::string& url,
                 const std::string& source_url,
                 const char* request_type) const;

  // CSS hiding the elements targeted by cosmetic filters on `page_url`, or
  // nullopt when there is nothing to hide. Thread-safe.
  std::optional<std::string> CosmeticCss(const std::string& page_url) const;

  // JSON for adblock/cosmetic_content.js (hiding CSS, procedural filters and
  // the generichide flag) for `page_url`, or nullopt when the page needs
  // nothing. Thread-safe.
  std::optional<std::string> CosmeticPayload(const std::string& page_url) const;

  // Scriptlet JavaScript that filter lists inject into `page_url`, or nullopt
  // (no matching rule, or no resources.json installed). Thread-safe.
  std::optional<std::string> CosmeticScript(const std::string& page_url) const;

  // CSS for the generic class/id selectors matching what a page reported.
  // `classes_json` / `ids_json` are JSON string arrays from an UNTRUSTED page;
  // the Rust side validates and bounds them. nullopt when nothing matches.
  // Thread-safe.
  std::optional<std::string> GenericCss(const std::string& page_url,
                                        const std::string& classes_json,
                                        const std::string& ids_json) const;

 private:
  explicit AdblockEngine(void* handle);
  static std::shared_ptr<AdblockEngine> Adopt(void* handle);

  void* handle_;
};

// Thread-safe holder for the currently active engine.
class AdblockEngineHolder {
 public:
  void Set(std::shared_ptr<AdblockEngine> engine);
  std::shared_ptr<AdblockEngine> Get() const;

 private:
  mutable std::mutex mutex_;
  std::shared_ptr<AdblockEngine> engine_;
};

}  // namespace vibrium

#endif  // VIBRIUM_ADBLOCK_ENGINE_H_
