// User-controlled ad-blocking switches, shared by the browser process and
// every renderer.
//
// What it holds: the master on/off switch, cosmetic filtering and scriptlets
// on/off, the filter lists the user turned off, and the "don't block on these
// sites" allowlist.
//
// How it is shared: the browser process owns the policy and writes it to a
// small text file (<filter dir>/vibrium-policy.ini, key=value lines). Renderer
// processes open the same file read-only and re-read it when its modification
// time changes (checked at most once a second), so a change made on the
// settings page reaches pages that are already open without any IPC.
//
// Threading: every method is thread-safe. The hot-path getters (flags and
// ShouldBypass) take no lock unless an allowlist exists.
//
// Failure policy: a missing or unreadable file means defaults (everything on,
// empty allowlist). Nothing here throws.
#ifndef VIBRIUM_ADBLOCK_POLICY_H_
#define VIBRIUM_ADBLOCK_POLICY_H_

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace vibrium {

// Lower-cased host of an http(s) URL ("https://User@A.example.com:8080/x" ->
// "a.example.com"); empty when `url` has no host.
std::string ExtractHost(const std::string& url);

// Turns what a person typed ("https://www.Example.com/path", "*.example.com",
// "example.com:8080") into an allowlist entry ("www.example.com" /
// "example.com"). Returns false if the result is not a plausible host name.
bool NormalizeSiteEntry(const std::string& input, std::string* host);

// Path -> UTF-8 that is safe with non-ASCII user names on Windows. (The other
// direction, PathFromUtf8, is declared in scriptlet_engine.h.)
std::string PathToUtf8(const std::filesystem::path& path);

class AdblockPolicy {
 public:
  AdblockPolicy() = default;
  AdblockPolicy(const AdblockPolicy&) = delete;
  AdblockPolicy& operator=(const AdblockPolicy&) = delete;

  // `file` is the policy file. The owner (browser process) writes it on every
  // change; a reader (renderer) only polls it. Loads the current contents.
  void Open(const std::filesystem::path& file, bool owner);
  const std::filesystem::path& file() const { return file_; }

  // Readers only: re-reads the file if it changed. Cheap; call it freely.
  void RefreshIfChanged();

  bool blocking_enabled() const { return enabled_.load(); }
  bool cosmetic_enabled() const { return cosmetic_.load(); }
  bool scriptlets_enabled() const { return scriptlets_.load(); }
  // Send "DNT: 1" and "Sec-GPC: 1" with every request (off by default).
  bool privacy_headers_enabled() const { return privacy_headers_.load(); }

  // True when `page_url` (the top-level page) is on the allowlist, i.e. it is
  // the entry's host or a subdomain of it.
  bool IsSiteAllowed(const std::string& page_url) const;

  // True when nothing should be blocked for `page_url`: blocking is off or the
  // site is allowlisted.
  bool ShouldBypass(const std::string& page_url) const {
    return !blocking_enabled() || IsSiteAllowed(page_url);
  }

  std::vector<std::string> AllowedSites() const;       // sorted
  std::vector<std::string> DisabledLists() const;      // sorted
  bool IsListEnabled(const std::string& name) const;

  // Owner only. Each setter saves the file; they return false when nothing
  // changed (or, for AddAllowedSite, when the entry is invalid).
  bool SetBlockingEnabled(bool value);
  bool SetCosmeticEnabled(bool value);
  bool SetScriptletsEnabled(bool value);
  bool SetPrivacyHeaders(bool value);
  // Everything back to the defaults: blocking, cosmetic filtering and
  // scriptlets on, every list on, empty allowlist, privacy headers off.
  void ResetToDefaults();
  bool SetListEnabled(const std::string& name, bool enabled);
  // `normalized` (optional) receives the stored entry. `already_present` is
  // set when the entry exists already.
  bool AddAllowedSite(const std::string& input, std::string* normalized,
                      bool* already_present = nullptr);
  bool RemoveAllowedSite(const std::string& host);

  // Bumped on every change (including a reader picking up a new file).
  uint64_t revision() const { return revision_.load(); }

 private:
  void LoadLocked();
  bool SaveLocked();

  std::filesystem::path file_;
  bool owner_ = false;

  std::atomic<bool> enabled_{true};
  std::atomic<bool> cosmetic_{true};
  std::atomic<bool> scriptlets_{true};
  std::atomic<bool> privacy_headers_{false};
  std::atomic<size_t> allow_count_{0};
  std::atomic<uint64_t> revision_{0};
  std::atomic<int64_t> last_check_ms_{0};

  mutable std::mutex mutex_;
  std::set<std::string> allowed_;
  std::set<std::string> disabled_lists_;
  std::filesystem::file_time_type loaded_time_{};
  bool loaded_time_valid_ = false;
  uint64_t loaded_signature_ = 0;
};

}  // namespace vibrium

#endif  // VIBRIUM_ADBLOCK_POLICY_H_
