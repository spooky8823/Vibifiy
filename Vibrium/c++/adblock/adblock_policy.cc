#include "adblock_policy.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace vibrium {

namespace fs = std::filesystem;

namespace {

constexpr int64_t kRecheckMs = 1000;
constexpr size_t kMaxAllowedSites = 5000;

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string Lower(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return text;
}

std::string TrimAscii(const std::string& text) {
  const char* kSpace = " \t\r\n";
  const size_t first = text.find_first_not_of(kSpace);
  if (first == std::string::npos) return {};
  const size_t last = text.find_last_not_of(kSpace);
  return text.substr(first, last - first + 1);
}

// Part of `url` between "://" and the first '/', '?' or '#', without user
// info and port. Empty if there is no scheme or the host is an IPv6 literal.
std::string AuthorityHost(const std::string& url) {
  const size_t scheme = url.find("://");
  const size_t start = scheme == std::string::npos ? 0 : scheme + 3;
  size_t end = url.find_first_of("/?#", start);
  if (end == std::string::npos) end = url.size();
  std::string authority = url.substr(start, end - start);
  const size_t at = authority.rfind('@');
  if (at != std::string::npos) authority.erase(0, at + 1);
  if (!authority.empty() && authority[0] == '[') return {};
  const size_t colon = authority.find(':');
  if (colon != std::string::npos) authority.erase(colon);
  while (!authority.empty() && authority.back() == '.') authority.pop_back();
  return Lower(authority);
}

bool IsListName(const std::string& name) {
  if (name.empty() || name.size() > 64) return false;
  for (char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '-';
    if (!ok) return false;
  }
  return true;
}

}  // namespace

std::string ExtractHost(const std::string& url) {
  if (url.find("://") == std::string::npos) return {};
  return AuthorityHost(url);
}

bool NormalizeSiteEntry(const std::string& input, std::string* host) {
  std::string text = Lower(TrimAscii(input));
  if (text.empty() || text.size() > 600) return false;

  std::string value = AuthorityHost(text);
  while (value.rfind("*.", 0) == 0) value.erase(0, 2);
  while (!value.empty() && value[0] == '.') value.erase(0, 1);
  while (!value.empty() && value.back() == '.') value.pop_back();

  if (value.empty() || value.size() > 253) return false;
  if (value.find('.') == std::string::npos && value != "localhost") {
    return false;
  }
  size_t label_start = 0;
  for (size_t i = 0; i <= value.size(); ++i) {
    if (i == value.size() || value[i] == '.') {
      const size_t length = i - label_start;
      if (length == 0 || length > 63) return false;
      if (value[label_start] == '-' || value[i - 1] == '-') return false;
      label_start = i + 1;
      continue;
    }
    const char c = value[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '-';
    if (!ok) return false;
  }
  if (host) *host = value;
  return true;
}

std::string PathToUtf8(const fs::path& path) {
#if defined(_WIN32)
  const std::wstring wide = path.wstring();
  if (wide.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                       static_cast<int>(wide.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string utf8(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      utf8.data(), size, nullptr, nullptr);
  return utf8;
#else
  return path.string();
#endif
}

// ---------------------------------------------------------------------------
// Loading and saving
// ---------------------------------------------------------------------------

void AdblockPolicy::Open(const fs::path& file, bool owner) {
  std::lock_guard<std::mutex> lock(mutex_);
  file_ = file;
  owner_ = owner;
  LoadLocked();
  last_check_ms_.store(NowMs());
}

// Caller holds mutex_.
void AdblockPolicy::LoadLocked() {
  bool enabled = true, cosmetic = true, scriptlets = true, privacy = false;
  std::set<std::string> allowed;
  std::set<std::string> disabled;

  std::error_code ec;
  loaded_time_valid_ = false;
  if (!file_.empty() && fs::is_regular_file(file_, ec)) {
    const auto stamp = fs::last_write_time(file_, ec);
    if (!ec) {
      loaded_time_ = stamp;
      loaded_time_valid_ = true;
    }
    std::ifstream in(file_, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    // Filesystems used in CI and on some user devices have coarse timestamp
    // resolution. The policy may be atomically replaced twice with the same
    // mtime, so track content as well as the timestamp.
    loaded_signature_ = std::hash<std::string>{}(contents);
    std::istringstream lines(contents);
    std::string line;
    while (std::getline(lines, line)) {
      line = TrimAscii(line);
      if (line.empty() || line[0] == '#') continue;
      const size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      const std::string key = TrimAscii(line.substr(0, eq));
      const std::string value = TrimAscii(line.substr(eq + 1));
      if (key == "enabled") {
        enabled = value != "0";
      } else if (key == "cosmetic") {
        cosmetic = value != "0";
      } else if (key == "scriptlets") {
        scriptlets = value != "0";
      } else if (key == "privacy_headers") {
        privacy = value == "1";
      } else if (key == "allow") {
        std::string host;
        if (allowed.size() < kMaxAllowedSites &&
            NormalizeSiteEntry(value, &host)) {
          allowed.insert(host);
        }
      } else if (key == "disable_list") {
        if (IsListName(value)) disabled.insert(value);
      }
    }
  }

  enabled_.store(enabled);
  cosmetic_.store(cosmetic);
  scriptlets_.store(scriptlets);
  privacy_headers_.store(privacy);
  allowed_ = std::move(allowed);
  disabled_lists_ = std::move(disabled);
  allow_count_.store(allowed_.size());
  ++revision_;
}

// Caller holds mutex_.
bool AdblockPolicy::SaveLocked() {
  if (!owner_ || file_.empty()) return false;

  std::ostringstream text;
  text << "# Vibrium ad-block settings. Edited by Settings > Ad blocker.\n";
  text << "enabled=" << (enabled_.load() ? 1 : 0) << "\n";
  text << "cosmetic=" << (cosmetic_.load() ? 1 : 0) << "\n";
  text << "scriptlets=" << (scriptlets_.load() ? 1 : 0) << "\n";
  text << "privacy_headers=" << (privacy_headers_.load() ? 1 : 0) << "\n";
  for (const std::string& name : disabled_lists_) {
    text << "disable_list=" << name << "\n";
  }
  for (const std::string& host : allowed_) text << "allow=" << host << "\n";

  std::error_code ec;
  fs::create_directories(file_.parent_path(), ec);
  // Not "*.txt": the filter loader reads every .txt in this directory.
  fs::path temp = file_;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    const std::string data = text.str();
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out.good()) {
      out.close();
      fs::remove(temp, ec);
      return false;
    }
  }
  fs::rename(temp, file_, ec);  // replaces an existing file
  if (ec) {
    fs::remove(temp, ec);
    return false;
  }
  const auto stamp = fs::last_write_time(file_, ec);
  if (!ec) {
    loaded_time_ = stamp;
    loaded_time_valid_ = true;
  }
  return true;
}

void AdblockPolicy::RefreshIfChanged() {
  if (owner_ || file_.empty()) return;
  const int64_t now = NowMs();
  if (now - last_check_ms_.load() < kRecheckMs) return;
  last_check_ms_.store(now);

  std::error_code ec;
  const auto stamp = fs::last_write_time(file_, ec);
  const bool exists = !ec;

  // Compare the bytes too: last_write_time can remain unchanged when a file
  // is replaced within the filesystem's timestamp-resolution window.
  uint64_t signature = 0;
  if (exists) {
    std::ifstream in(file_, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    signature = std::hash<std::string>{}(contents);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (exists == loaded_time_valid_ &&
      (!exists || (stamp == loaded_time_ &&
                   signature == loaded_signature_))) {
    return;
  }
  LoadLocked();
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool AdblockPolicy::IsSiteAllowed(const std::string& page_url) const {
  if (allow_count_.load() == 0) return false;
  const std::string host = ExtractHost(page_url);
  if (host.empty()) return false;

  std::lock_guard<std::mutex> lock(mutex_);
  size_t pos = 0;
  while (true) {
    if (allowed_.count(host.substr(pos)) != 0) return true;
    const size_t dot = host.find('.', pos);
    if (dot == std::string::npos) return false;
    pos = dot + 1;
  }
}

std::vector<std::string> AdblockPolicy::AllowedSites() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::vector<std::string>(allowed_.begin(), allowed_.end());
}

std::vector<std::string> AdblockPolicy::DisabledLists() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::vector<std::string>(disabled_lists_.begin(),
                                  disabled_lists_.end());
}

bool AdblockPolicy::IsListEnabled(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return disabled_lists_.count(name) == 0;
}

// ---------------------------------------------------------------------------
// Changes (owner only)
// ---------------------------------------------------------------------------

bool AdblockPolicy::SetBlockingEnabled(bool value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (enabled_.load() == value) return false;
  enabled_.store(value);
  SaveLocked();
  ++revision_;
  return true;
}

bool AdblockPolicy::SetCosmeticEnabled(bool value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (cosmetic_.load() == value) return false;
  cosmetic_.store(value);
  SaveLocked();
  ++revision_;
  return true;
}

bool AdblockPolicy::SetScriptletsEnabled(bool value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (scriptlets_.load() == value) return false;
  scriptlets_.store(value);
  SaveLocked();
  ++revision_;
  return true;
}

bool AdblockPolicy::SetPrivacyHeaders(bool value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (privacy_headers_.load() == value) return false;
  privacy_headers_.store(value);
  SaveLocked();
  ++revision_;
  return true;
}

void AdblockPolicy::ResetToDefaults() {
  std::lock_guard<std::mutex> lock(mutex_);
  enabled_.store(true);
  cosmetic_.store(true);
  scriptlets_.store(true);
  privacy_headers_.store(false);
  allowed_.clear();
  disabled_lists_.clear();
  allow_count_.store(0);
  SaveLocked();
  ++revision_;
}

bool AdblockPolicy::SetListEnabled(const std::string& name, bool enabled) {
  if (!IsListName(name)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  const bool changed = enabled ? disabled_lists_.erase(name) > 0
                               : disabled_lists_.insert(name).second;
  if (!changed) return false;
  SaveLocked();
  ++revision_;
  return true;
}

bool AdblockPolicy::AddAllowedSite(const std::string& input,
                                   std::string* normalized,
                                   bool* already_present) {
  std::string host;
  if (!NormalizeSiteEntry(input, &host)) return false;
  if (normalized) *normalized = host;

  std::lock_guard<std::mutex> lock(mutex_);
  if (allowed_.count(host) != 0) {
    if (already_present) *already_present = true;
    return true;
  }
  if (allowed_.size() >= kMaxAllowedSites) return false;
  allowed_.insert(host);
  allow_count_.store(allowed_.size());
  SaveLocked();
  ++revision_;
  if (already_present) *already_present = false;
  return true;
}

bool AdblockPolicy::RemoveAllowedSite(const std::string& host) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (allowed_.erase(host) == 0) return false;
  allow_count_.store(allowed_.size());
  SaveLocked();
  ++revision_;
  return true;
}

}  // namespace vibrium
