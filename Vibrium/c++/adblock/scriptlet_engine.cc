#include "scriptlet_engine.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <filesystem>
#include <functional>
#include <system_error>
#include <utility>

#include "vibrium_adblock.h"

namespace vibrium {

namespace fs = std::filesystem;

std::filesystem::path PathFromUtf8(const std::string& utf8) {
#if defined(_WIN32)
  if (utf8.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                       static_cast<int>(utf8.size()), nullptr, 0);
  if (size <= 0) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                      wide.data(), size);
  return std::filesystem::path(wide);
#else
  return std::filesystem::path(utf8);
#endif
}

ScriptletEngine::ScriptletEngine(void* handle) : handle_(handle) {}

ScriptletEngine::~ScriptletEngine() {
  vibrium_adblock_scriptlets_destroy(handle_);
}

std::unique_ptr<ScriptletEngine> ScriptletEngine::FromDirectory(
    const std::string& utf8_directory) {
  if (vibrium_adblock_abi_version() != VIBRIUM_ADBLOCK_ABI_VERSION) {
    return nullptr;
  }
  void* handle =
      vibrium_adblock_scriptlets_create_from_directory(utf8_directory.c_str());
  if (!handle) {
    return nullptr;
  }
  // The constructor is private, so make_unique cannot be used.
  return std::unique_ptr<ScriptletEngine>(new ScriptletEngine(handle));
}

std::optional<std::string> ScriptletEngine::ScriptFor(
    const std::string& page_url) const {
  char* owned = vibrium_adblock_scriptlets_script(handle_, page_url.c_str());
  if (!owned) {
    return std::nullopt;
  }
  std::string script(owned);
  vibrium_adblock_string_free(owned);
  return script;
}

uint64_t ScriptletDirectorySignature(const std::string& utf8_directory) {
  const fs::path directory = PathFromUtf8(utf8_directory);
  std::error_code ec;
  uint64_t signature = 0;

  for (fs::directory_iterator it(directory, ec), end; !ec && it != end;
       it.increment(ec)) {
    std::error_code file_ec;
    if (!it->is_regular_file(file_ec) || file_ec) {
      continue;
    }
    const fs::path& path = it->path();
    const std::string name = path.filename().string();
    if (name.empty() || name[0] == '.') {
      continue;  // temp files written by the updater
    }
    if (path.extension() != ".txt" && name != "resources.json") {
      continue;
    }
    const auto size = fs::file_size(path, file_ec);
    const auto written = fs::last_write_time(path, file_ec);
    if (file_ec) {
      continue;
    }
    // Order-independent: sum a per-file hash so directory iteration order does
    // not matter.
    uint64_t file_hash = std::hash<std::string>{}(name);
    file_hash = file_hash * 1099511628211ULL + static_cast<uint64_t>(size);
    file_hash = file_hash * 1099511628211ULL +
                static_cast<uint64_t>(written.time_since_epoch().count());
    signature += file_hash;
  }
  return signature;
}

ScriptletProvider::ScriptletProvider(std::string utf8_directory,
                                     std::chrono::milliseconds recheck_interval)
    : directory_(std::move(utf8_directory)),
      recheck_interval_(recheck_interval) {}

void ScriptletProvider::RefreshIfNeeded() {
  const auto now = std::chrono::steady_clock::now();
  if (loaded_once_ && now < next_check_) {
    return;
  }
  next_check_ = now + recheck_interval_;

  const uint64_t signature = ScriptletDirectorySignature(directory_);
  if (loaded_once_ && signature == signature_) {
    return;
  }
  signature_ = signature;
  loaded_once_ = true;
  // May be null (no resources.json or no scriptlet rule yet). That is retried
  // automatically as soon as the files change.
  engine_ = ScriptletEngine::FromDirectory(directory_);
}

std::optional<std::string> ScriptletProvider::ScriptFor(
    const std::string& page_url) {
  RefreshIfNeeded();
  if (!engine_) {
    return std::nullopt;
  }
  return engine_->ScriptFor(page_url);
}

}  // namespace vibrium
