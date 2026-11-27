// Lifetime and per-session ad-blocking statistics.
//
// A "session" is one run of the browser. Every blocked request is counted
// against the domain it was blocked from, so the settings page can show what
// was blocked in each session; allowed requests are only counted. Totals for
// all sessions ever recorded (the lifetime numbers) survive even after old
// sessions are trimmed from the list.
//
// Storage: UTF-8 text file (see adblock_history.cc). It is written at most once
// a minute while browsing and once more at shutdown, through a temporary file
// and a rename, so a crash never leaves a half-written file behind. At most
// kMaxSessions sessions and kMaxHostsPerSession domains per session are kept.
//
// Threading: Record() is called from CEF IO threads; everything is guarded by
// one mutex and Record() does O(1) work. The reader API returns copies.
#ifndef VIBRIUM_ADBLOCK_HISTORY_H_
#define VIBRIUM_ADBLOCK_HISTORY_H_

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "adblock_engine.h"

namespace vibrium {

class AdblockHistory {
 public:
  static constexpr size_t kMaxSessions = 100;
  static constexpr size_t kMaxHostsPerSession = 300;
  static constexpr const char* kOtherHost = "(other)";

  struct HostCount {
    std::string host;
    uint64_t count = 0;
    std::string example;  // one blocked URL from that host
  };

  struct Session {
    uint64_t id = 0;        // start time; unique per session
    uint64_t start_ms = 0;  // milliseconds since the Unix epoch
    uint64_t end_ms = 0;    // last activity (live session: now-ish)
    uint64_t blocked = 0;
    uint64_t allowed = 0;
    bool live = false;
  };

  struct Summary {
    uint64_t lifetime_blocked = 0;
    uint64_t lifetime_allowed = 0;
    std::vector<Session> sessions;  // newest first; the live one is first
  };

  AdblockHistory() = default;
  AdblockHistory(const AdblockHistory&) = delete;
  AdblockHistory& operator=(const AdblockHistory&) = delete;

  // Loads `file` (empty path = keep everything in memory only) and begins the
  // live session. Call once, on the UI thread, before browsing starts.
  void Start(const std::filesystem::path& file);

  // Optional: run slow work (the periodic save) somewhere other than the
  // calling IO thread. Without it saves happen inline.
  void SetBackgroundRunner(std::function<void(std::function<void()>)> runner);

  // Counts one decision. Cheap, thread-safe, ignored before Start()/after Stop().
  void Record(AdblockEngine::Decision decision, const std::string& url);

  Summary Summarize() const;
  // Domains blocked in `session_id`, most frequent first (empty if unknown).
  std::vector<HostCount> HostsFor(uint64_t session_id) const;

  // Changes on every recorded decision; lets a page notice new data cheaply.
  uint64_t revision() const { return revision_.load(); }

  // Forgets every finished session and resets the lifetime totals to the live
  // session's own numbers.
  void Clear();

  // Final save; later Record() calls are ignored. Safe to call twice.
  void Stop();

  // Writes the file now (no-op without a file). Used by Stop() and tests.
  bool Save();

 private:
  struct Stored {
    Session meta;
    std::vector<HostCount> hosts;
    std::unordered_map<std::string, size_t> index;  // live session only
  };

  void Load();
  bool SaveLocked();
  void MaybeScheduleSave(int64_t now_ms);

  mutable std::mutex mutex_;
  std::mutex file_mutex_;  // serialises writes to disk
  std::filesystem::path file_;
  std::atomic<bool> active_{false};
  std::atomic<uint64_t> revision_{0};
  std::function<void(std::function<void()>)> runner_;

  std::deque<Stored> past_;  // oldest first
  Stored live_;
  uint64_t base_blocked_ = 0;  // lifetime totals before the live session
  uint64_t base_allowed_ = 0;
  bool dirty_ = false;
  bool save_pending_ = false;
  int64_t last_save_ms_ = 0;
};

}  // namespace vibrium

#endif  // VIBRIUM_ADBLOCK_HISTORY_H_
