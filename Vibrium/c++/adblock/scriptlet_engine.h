// Scriptlet injection that runs before a page's own scripts.
//
// A scriptlet (uBlock Origin's term) is a small piece of JavaScript that
// neutralises a page's ad logic, for example by deleting the `adPlacements`
// field from YouTube's player data. It only works if it is installed before the
// page's scripts run, and the browser process cannot do that: it can only ask
// the renderer to run code later, asynchronously (frame->ExecuteJavaScript).
//
// So the renderer process builds its own, tiny engine that knows only the
// scriptlet rules (a few thousand rules, ~7 ms and ~7 MB, against hundreds of
// thousands of rules for the network engine) and looks the script up
// synchronously in CefRenderProcessHandler::OnContextCreated.
#ifndef VIBRIUM_ADBLOCK_SCRIPTLET_ENGINE_H_
#define VIBRIUM_ADBLOCK_SCRIPTLET_ENGINE_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vibrium {

// Converts a UTF-8 string (how paths cross the Rust FFI) to a filesystem path.
// On Windows a plain std::filesystem::path(std::string) would use the ANSI code
// page and break profile paths containing non-ASCII characters.
std::filesystem::path PathFromUtf8(const std::string& utf8);

// Thin owner of the Rust scriptlet engine handle.
class ScriptletEngine {
 public:
  // Builds the engine from the filter lists and resources.json in
  // `utf8_directory`. Returns null when there is no resources.json or no
  // scriptlet rule yet (normal on the very first run, before the first list
  // download), or on error.
  static std::unique_ptr<ScriptletEngine> FromDirectory(
      const std::string& utf8_directory);

  ~ScriptletEngine();
  ScriptletEngine(const ScriptletEngine&) = delete;
  ScriptletEngine& operator=(const ScriptletEngine&) = delete;

  // The JavaScript to run in `page_url` before the page's own scripts, or
  // nullopt when no scriptlet applies. Only http(s) URLs qualify.
  std::optional<std::string> ScriptFor(const std::string& page_url) const;

 private:
  explicit ScriptletEngine(void* handle);

  void* handle_;
};

// A fingerprint of the files the scriptlet engine reads (the *.txt lists and
// resources.json in `directory`): it changes whenever one of them is added,
// removed or rewritten. Hidden files and *.etag sidecars are ignored.
uint64_t ScriptletDirectorySignature(const std::string& utf8_directory);

// Keeps a ScriptletEngine up to date for a long-lived renderer process.
//
// The renderer may start before the first list download has finished, and the
// updater replaces lists while the browser runs, so a one-time load would leave
// scriptlets off (first run) or stale. This re-checks the directory
// fingerprint, at most once per `recheck_interval`, and rebuilds the engine
// when it changed. Not thread-safe: use it from the renderer's main thread.
class ScriptletProvider {
 public:
  explicit ScriptletProvider(
      std::string utf8_directory,
      std::chrono::milliseconds recheck_interval = std::chrono::seconds(3));

  std::optional<std::string> ScriptFor(const std::string& page_url);

 private:
  void RefreshIfNeeded();

  std::string directory_;
  std::chrono::milliseconds recheck_interval_;
  std::chrono::steady_clock::time_point next_check_{};
  uint64_t signature_ = 0;
  bool loaded_once_ = false;
  std::unique_ptr<ScriptletEngine> engine_;
};

}  // namespace vibrium

#endif  // VIBRIUM_ADBLOCK_SCRIPTLET_ENGINE_H_
