#include "adblock_history.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include "adblock_policy.h"

namespace vibrium {

namespace fs = std::filesystem;

namespace {

constexpr char kMagic[] = "VIBRIUM-ADBLOCK-HISTORY 1";
constexpr int64_t kSaveIntervalMs = 60 * 1000;
constexpr size_t kMaxExampleBytes = 200;

int64_t NowEpochMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int64_t NowSteadyMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string CleanField(std::string text) {
  if (text.size() > kMaxExampleBytes) text.resize(kMaxExampleBytes);
  for (char& c : text) {
    if (c == '\t' || c == '\r' || c == '\n') c = ' ';
  }
  return text;
}

std::vector<std::string> SplitTabs(const std::string& line) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (true) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string::npos) {
      parts.push_back(line.substr(start));
      return parts;
    }
    parts.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
}

uint64_t ToU64(const std::string& text) {
  return std::strtoull(text.c_str(), nullptr, 10);
}

}  // namespace

void AdblockHistory::Start(const fs::path& file) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    file_ = file;
    Load();
    live_ = Stored{};
    const uint64_t now = static_cast<uint64_t>(NowEpochMs());
    live_.meta.id = now;
    live_.meta.start_ms = now;
    live_.meta.end_ms = now;
    live_.meta.live = true;
    last_save_ms_ = NowSteadyMs();
    dirty_ = false;
    save_pending_ = false;
  }
  active_.store(true);
  ++revision_;
}

void AdblockHistory::SetBackgroundRunner(
    std::function<void(std::function<void()>)> runner) {
  std::lock_guard<std::mutex> lock(mutex_);
  runner_ = std::move(runner);
}

// Caller holds mutex_.
void AdblockHistory::Load() {
  past_.clear();
  base_blocked_ = 0;
  base_allowed_ = 0;
  if (file_.empty()) return;

  std::ifstream in(file_, std::ios::binary);
  if (!in) return;
  std::string line;
  if (!std::getline(in, line)) return;
  if (!line.empty() && line.back() == '\r') line.pop_back();
  if (line != kMagic) return;  // unknown file: start fresh, never crash

  Stored* current = nullptr;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::vector<std::string> f = SplitTabs(line);
    if (f.empty()) continue;
    if (f[0] == "T" && f.size() >= 3) {
      base_blocked_ = ToU64(f[1]);
      base_allowed_ = ToU64(f[2]);
    } else if (f[0] == "S" && f.size() >= 6) {
      past_.emplace_back();
      current = &past_.back();
      current->meta.id = ToU64(f[1]);
      current->meta.start_ms = ToU64(f[2]);
      current->meta.end_ms = ToU64(f[3]);
      current->meta.blocked = ToU64(f[4]);
      current->meta.allowed = ToU64(f[5]);
    } else if (f[0] == "H" && f.size() >= 3 && current &&
               current->hosts.size() < kMaxHostsPerSession + 1) {
      HostCount host;
      host.host = f[1];
      host.count = ToU64(f[2]);
      if (f.size() >= 4) host.example = f[3];
      if (!host.host.empty()) current->hosts.push_back(std::move(host));
    }
  }
  while (past_.size() + 1 > kMaxSessions) past_.pop_front();
}

// Caller holds mutex_. Takes file_mutex_ while writing.
bool AdblockHistory::SaveLocked() {
  if (file_.empty()) return false;

  std::ostringstream text;
  text << kMagic << "\n";
  text << "T\t" << (base_blocked_ + live_.meta.blocked) << "\t"
       << (base_allowed_ + live_.meta.allowed) << "\n";
  auto write_session = [&text](const Stored& s) {
    text << "S\t" << s.meta.id << "\t" << s.meta.start_ms << "\t"
         << s.meta.end_ms << "\t" << s.meta.blocked << "\t" << s.meta.allowed
         << "\n";
    for (const HostCount& h : s.hosts) {
      text << "H\t" << CleanField(h.host) << "\t" << h.count << "\t"
           << CleanField(h.example) << "\n";
    }
  };
  for (const Stored& s : past_) write_session(s);
  if (live_.meta.blocked != 0 || live_.meta.allowed != 0) write_session(live_);

  const std::string data = text.str();
  std::lock_guard<std::mutex> file_lock(file_mutex_);
  std::error_code ec;
  fs::create_directories(file_.parent_path(), ec);
  fs::path temp = file_;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out.good()) {
      out.close();
      fs::remove(temp, ec);
      return false;
    }
  }
  fs::rename(temp, file_, ec);
  if (ec) {
    fs::remove(temp, ec);
    return false;
  }
  dirty_ = false;
  return true;
}

bool AdblockHistory::Save() {
  std::lock_guard<std::mutex> lock(mutex_);
  live_.meta.end_ms = static_cast<uint64_t>(NowEpochMs());
  last_save_ms_ = NowSteadyMs();
  return SaveLocked();
}

// Caller holds mutex_.
void AdblockHistory::MaybeScheduleSave(int64_t now_ms) {
  if (file_.empty() || !dirty_ || save_pending_) return;
  if (now_ms - last_save_ms_ < kSaveIntervalMs) return;
  last_save_ms_ = now_ms;
  if (!runner_) {
    SaveLocked();
    return;
  }
  save_pending_ = true;
  runner_([this]() {
    std::lock_guard<std::mutex> lock(mutex_);
    save_pending_ = false;
    if (active_.load()) {
      live_.meta.end_ms = static_cast<uint64_t>(NowEpochMs());
      SaveLocked();
    }
  });
}

void AdblockHistory::Record(AdblockEngine::Decision decision,
                            const std::string& url) {
  if (!active_.load()) return;
  if (decision == AdblockEngine::Decision::kError) return;

  std::string host;
  if (decision == AdblockEngine::Decision::kBlock) {
    host = ExtractHost(url);
    if (host.empty()) host = "(unknown)";
  }

  const int64_t steady = NowSteadyMs();
  std::lock_guard<std::mutex> lock(mutex_);
  live_.meta.end_ms = static_cast<uint64_t>(NowEpochMs());
  if (decision == AdblockEngine::Decision::kAllow) {
    ++live_.meta.allowed;
  } else {
    ++live_.meta.blocked;
    auto found = live_.index.find(host);
    if (found == live_.index.end() &&
        live_.hosts.size() >= kMaxHostsPerSession) {
      host = kOtherHost;
      found = live_.index.find(host);
    }
    if (found == live_.index.end()) {
      HostCount entry;
      entry.host = host;
      entry.count = 1;
      entry.example = CleanField(url);
      live_.hosts.push_back(std::move(entry));
      live_.index.emplace(host, live_.hosts.size() - 1);
    } else {
      ++live_.hosts[found->second].count;
    }
  }
  dirty_ = true;
  ++revision_;
  MaybeScheduleSave(steady);
}

AdblockHistory::Summary AdblockHistory::Summarize() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Summary summary;
  summary.lifetime_blocked = base_blocked_ + live_.meta.blocked;
  summary.lifetime_allowed = base_allowed_ + live_.meta.allowed;
  if (live_.meta.id != 0) summary.sessions.push_back(live_.meta);
  for (auto it = past_.rbegin(); it != past_.rend(); ++it) {
    Session s = it->meta;
    s.live = false;
    summary.sessions.push_back(s);
  }
  return summary;
}

std::vector<AdblockHistory::HostCount> AdblockHistory::HostsFor(
    uint64_t session_id) const {
  std::vector<HostCount> hosts;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (live_.meta.id == session_id) {
      hosts = live_.hosts;
    } else {
      for (const Stored& s : past_) {
        if (s.meta.id == session_id) {
          hosts = s.hosts;
          break;
        }
      }
    }
  }
  std::stable_sort(hosts.begin(), hosts.end(),
                   [](const HostCount& a, const HostCount& b) {
                     if (a.count != b.count) return a.count > b.count;
                     return a.host < b.host;
                   });
  return hosts;
}

void AdblockHistory::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  past_.clear();
  base_blocked_ = 0;
  base_allowed_ = 0;
  ++revision_;
  dirty_ = true;
  SaveLocked();
}

void AdblockHistory::Stop() {
  if (!active_.exchange(false)) return;
  std::lock_guard<std::mutex> lock(mutex_);
  live_.meta.end_ms = static_cast<uint64_t>(NowEpochMs());
  SaveLocked();
}

}  // namespace vibrium
