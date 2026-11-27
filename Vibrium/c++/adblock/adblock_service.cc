#include "adblock_service.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <utility>

#include "filter_list_catalog.h"
#include "include/base/cef_logging.h"
#include "include/cef_task.h"
#include "request_policy.h"
#include "scriptlet_engine.h"  // PathFromUtf8

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

namespace {

std::string ToUtf8(const std::filesystem::path& path) {
  const auto u8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// Per-user, writable directory for downloaded lists. Empty if unavailable.
std::filesystem::path UserFilterDirectory() {
#if defined(_WIN32)
  if (const wchar_t* base = _wgetenv(L"LOCALAPPDATA")) {
    return std::filesystem::path(base) / L"Vibrium" / L"filters";
  }
#endif
  return {};
}

// Copies the shipped lists (the baseline) into `user_dir`, replacing a copy
// that differs, so an upgraded baseline reaches users who already have a
// user directory. Failures are non-fatal.
void SeedShippedLists(const std::filesystem::path& shipped,
                      const std::filesystem::path& user_dir) {
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(shipped, ec)) {
    const auto& p = entry.path();
    if (!entry.is_regular_file(ec) ||
        (p.extension() != ".txt" && p.filename() != "resources.json")) {
      continue;
    }
    const auto dest = user_dir / p.filename();
    if (std::filesystem::exists(dest, ec) &&
        std::filesystem::file_size(dest, ec) == entry.file_size(ec)) {
      continue;
    }
    std::filesystem::copy_file(
        p, dest, std::filesystem::copy_options::overwrite_existing, ec);
  }
}

constexpr char kPolicyFileName[] = "vibrium-policy.ini";
constexpr char kCustomRulesName[] = "custom-rules";
constexpr size_t kMaxCustomRulesBytes = 1024 * 1024;

class LambdaTask : public CefTask {
 public:
  explicit LambdaTask(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Execute() override { fn_(); }

 private:
  std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(LambdaTask);
};

void PostBackground(std::function<void()> fn) {
  CefPostTask(TID_FILE_BACKGROUND, new LambdaTask(std::move(fn)));
}

struct ListText {
  const char* name;
  const char* title;
  const char* description;
};
// Human names for the lists in filter_list_catalog.h. A list that is not
// here is shown under its file name.
constexpr ListText kListText[] = {
    {"easylist", "EasyList", "Ads on most websites"},
    {"easyprivacy", "EasyPrivacy", "Trackers and analytics"},
    {"ubo-filters", "uBlock filters", "Extra ad and annoyance rules"},
    {"ubo-badware", "uBlock badware", "Malware and scam sites"},
    {"ubo-privacy", "uBlock privacy", "More tracker protection"},
    {"ubo-unbreak", "uBlock unbreak", "Fixes for sites other lists break"},
    {"ubo-quick-fixes", "uBlock quick fixes", "Fast fixes for new ads"},
    {"hagezi-light", "HaGeZi Light", "Large domain blocklist"},
};

// Milliseconds since the Unix epoch for a file time.
uint64_t FileTimeToEpochMs(std::filesystem::file_time_type stamp) {
  const auto age = std::filesystem::file_time_type::clock::now() - stamp;
  const auto when =
      std::chrono::system_clock::now() -
      std::chrono::duration_cast<std::chrono::system_clock::duration>(age);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      when.time_since_epoch())
                      .count();
  return ms > 0 ? static_cast<uint64_t>(ms) : 0;
}

bool IsRulesFile(const std::filesystem::path& p) {
  return p.extension() == ".txt" || p.filename() == "resources.json";
}

std::filesystem::path ExecutableDirectory() {
#if defined(_WIN32)
  std::wstring buffer(32768, L'\0');
  const DWORD n =
      ::GetModuleFileNameW(nullptr, buffer.data(),
                           static_cast<DWORD>(buffer.size()));
  if (n > 0 && n < buffer.size()) {
    buffer.resize(n);
    return std::filesystem::path(buffer).parent_path();
  }
#endif
  return std::filesystem::current_path();
}

}  // namespace

AdblockService& AdblockService::Get() {
  static AdblockService instance;
  return instance;
}

AdblockService::AdblockService()
    : engine_(std::make_shared<AdblockEngineHolder>()),
      stats_(std::make_shared<AdblockStats>()),
      policy_(std::make_shared<AdblockPolicy>()),
      history_(std::make_shared<AdblockHistory>()),
      cosmetic_handler_(
          std::make_unique<CosmeticQueryHandler>(engine_, policy_)),
      router_(CefMessageRouterBrowserSide::Create(CosmeticRouterConfig())),
      request_handler_(new AdblockRequestHandler(engine_, stats_, router_,
                                                 policy_, history_)) {
  router_->AddHandler(cosmetic_handler_.get(), /*first=*/false);
}

void AdblockService::Shutdown() {
  shutting_down_ = true;
  history_->Stop();  // final save; idempotent
  if (updater_) {
    updater_->Cancel();
    updater_.reset();
  }
  // Remove the raw handler pointer before destroying the handler itself.
  if (router_ && cosmetic_handler_) {
    router_->RemoveHandler(cosmetic_handler_.get());
  }

  // AdblockRequestHandler also owns a CefRefPtr to the router.
  request_handler_ = nullptr;
  router_ = nullptr;
  cosmetic_handler_.reset();

  enabled_ = false;
  filter_dir_.clear();
}

bool AdblockService::Initialize(CefRefPtr<CefCommandLine> command_line) {
  // Diagnostic switch used by CI to isolate the browser-side cosmetic query
  // router from network filtering. The request handler remains installed.
  if (command_line && command_line->HasSwitch("disable-cosmetic-router")) {
    if (router_ && cosmetic_handler_) {
      router_->RemoveHandler(cosmetic_handler_.get());
    }
    request_handler_ = nullptr;
    router_ = nullptr;
    cosmetic_handler_.reset();
    request_handler_ = new AdblockRequestHandler(engine_, stats_, nullptr,
                                                 policy_, history_);
    LOG(WARNING) << "Cosmetic message router disabled by command-line switch";
  }

  std::string filter_dir;
  const bool explicit_dir =
      command_line && command_line->HasSwitch("filter-dir");
  if (explicit_dir) {
    filter_dir = command_line->GetSwitchValue("filter-dir").ToString();
  }
  const std::filesystem::path shipped_dir = ExecutableDirectory() / "filters";
  bool use_updater = false;
  if (filter_dir.empty()) {
    // Deliberately not the working directory: that depends on how the
    // browser was launched. Prefer the per-user directory so downloaded
    // lists can be stored (the install directory is often read-only).
    const std::filesystem::path user_dir = UserFilterDirectory();
    std::error_code ec;
    if (!user_dir.empty() &&
        (std::filesystem::create_directories(user_dir, ec) ||
         std::filesystem::is_directory(user_dir, ec))) {
      SeedShippedLists(shipped_dir, user_dir);
      filter_dir = ToUtf8(user_dir);
      use_updater = !(command_line &&
                      command_line->HasSwitch("no-filter-updates"));
    } else {
      filter_dir = ToUtf8(shipped_dir);
    }
  }

  filter_dir_ = filter_dir;
  // The policy must be loaded first: it says which lists are switched off.
  policy_->Open(PathFromUtf8(filter_dir_) / kPolicyFileName, /*owner=*/true);

  // Session and lifetime counts. Tests (--filter-dir) keep them in memory so
  // they never touch the user's real history.
  std::filesystem::path history_file;
#if defined(_WIN32)
  if (!explicit_dir) {
    if (const wchar_t* base = _wgetenv(L"LOCALAPPDATA")) {
      history_file =
          std::filesystem::path(base) / L"Vibrium" / L"adblock-history.tsv";
    }
  }
#endif
  history_->SetBackgroundRunner(
      [](std::function<void()> work) { PostBackground(std::move(work)); });
  history_->Start(history_file);

  if (auto engine = BuildEngine()) {
    engine_->Set(std::move(engine));
    enabled_ = true;
    LOG(INFO) << "Ad blocking enabled; filter lists from " << filter_dir;
  } else {
    enabled_ = false;
    LOG(WARNING) << "Ad blocking DISABLED: no usable filter lists in "
                 << filter_dir;
  }

  // Start downloading even if nothing usable was loaded yet: the updater
  // installs the engine when the first lists arrive.
  if (use_updater) {
    updater_ = FilterListUpdater::Create(engine_, filter_dir, [this]() {
      std::shared_ptr<AdblockEngine> engine = BuildEngine();
      // Lists that arrive on a first run switch blocking on.
      if (engine) enabled_ = true;
      return engine;
    });
    updater_->Start();
  }
  return enabled_;
}

void AdblockService::ApplyCosmeticFilters(CefRefPtr<CefFrame> frame) const {
  if (!router_ || !frame || !frame->IsValid()) {
    return;
  }
  const std::string url = frame->GetURL().ToString();
  if (!IsFilterableUrl(url)) {
    return;
  }
  const std::shared_ptr<AdblockEngine> engine = engine_->Get();
  if (!engine) {
    return;
  }

  // Blocking off, cosmetic filtering off or an allowlisted page: hide nothing.
  // The allowlist is about the page in the address bar, not this frame.
  if (!policy_->cosmetic_enabled()) {
    return;
  }
  std::string top = url;
  if (CefRefPtr<CefBrowser> browser = frame->GetBrowser()) {
    if (CefRefPtr<CefFrame> main_frame = browser->GetMainFrame()) {
      const std::string main_url = main_frame->GetURL().ToString();
      if (IsFilterableUrl(main_url)) top = main_url;
    }
  }
  if (policy_->ShouldBypass(top)) {
    return;
  }

  // Scriptlets are NOT injected here. ExecuteJavaScript is asynchronous, so
  // they would not be guaranteed to run before the page's own scripts, which
  // is too late for things like YouTube's inline player data. They run
  // synchronously in the renderer at document start instead (see
  // scriptlet_engine.h and VibriumApp::OnContextCreated).
  if (auto payload = engine->CosmeticPayload(url)) {
    frame->ExecuteJavaScript(BuildContentScript(*payload), url, 0);
  }
}

bool AdblockService::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  return router_ && router_->OnProcessMessageReceived(
                           browser, frame, source_process, message);
}

void AdblockService::OnBrowserClosed(CefRefPtr<CefBrowser> browser) {
  if (router_) {
    router_->OnBeforeClose(browser);
  }
}

std::string AdblockService::engine_dir() const {
  std::lock_guard<std::mutex> lock(build_mutex_);
  return engine_dir_.empty() ? filter_dir_ : engine_dir_;
}

std::string AdblockService::policy_file() const {
  return PathToUtf8(policy_->file());
}

// ---------------------------------------------------------------------------
// Engine building
// ---------------------------------------------------------------------------

std::shared_ptr<AdblockEngine> AdblockService::BuildEngine() {
  std::lock_guard<std::mutex> lock(build_mutex_);
  if (filter_dir_.empty()) return nullptr;

  const std::vector<std::string> disabled_list = policy_->DisabledLists();
  if (disabled_list.empty()) {
    engine_dir_.clear();
    return AdblockEngine::FromDirectory(filter_dir_);
  }

  // The Rust loader reads every <n>.txt in one directory, so lists the user
  // turned off are left out by assembling the others (hard links, or copies
  // when linking is not possible) in a staging subdirectory.
  namespace fs = std::filesystem;
  const std::set<std::string> disabled(disabled_list.begin(),
                                       disabled_list.end());
  const fs::path store = PathFromUtf8(filter_dir_);
  const fs::path stage = store / "active";
  std::error_code ec;
  fs::create_directories(stage, ec);
  if (ec) return nullptr;

  for (const auto& entry : fs::directory_iterator(stage, ec)) {
    std::error_code rm;
    if (entry.is_regular_file(rm) && IsRulesFile(entry.path())) {
      fs::remove(entry.path(), rm);
    }
  }
  for (const auto& entry : fs::directory_iterator(store, ec)) {
    std::error_code each;
    if (!entry.is_regular_file(each) || !IsRulesFile(entry.path())) continue;
    const fs::path name = entry.path().filename();
    if (name.string().rfind('.', 0) == 0) continue;  // temp files
    if (entry.path().extension() == ".txt" &&
        disabled.count(entry.path().stem().string()) != 0) {
      continue;
    }
    const fs::path dest = stage / name;
    fs::create_hard_link(entry.path(), dest, each);
    if (each) {
      each.clear();
      fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing,
                    each);
    }
  }

  std::shared_ptr<AdblockEngine> engine =
      AdblockEngine::FromDirectory(PathToUtf8(stage));
  if (engine) engine_dir_ = PathToUtf8(stage);
  return engine;
}

void AdblockService::ReloadEngineAsync() {
  PostBackground([this]() {
    if (shutting_down_) return;
    if (std::shared_ptr<AdblockEngine> engine = BuildEngine()) {
      engine_->Set(std::move(engine));
      enabled_ = true;
      LOG(INFO) << "Ad-block engine rebuilt after a settings change";
    } else {
      LOG(WARNING) << "Rebuild failed; keeping the previous ad-block engine";
    }
  });
}

// ---------------------------------------------------------------------------
// Filter lists
// ---------------------------------------------------------------------------

std::vector<AdblockService::FilterListInfo> AdblockService::FilterLists()
    const {
  namespace fs = std::filesystem;
  std::vector<FilterListInfo> lists;
  const fs::path store = PathFromUtf8(filter_dir_);
  for (const FilterListSpec& spec : kDefaultFilterLists) {
    FilterListInfo info;
    info.name = spec.name;
    info.title = spec.name;
    for (const ListText& text : kListText) {
      if (info.name == text.name) {
        info.title = text.title;
        info.description = text.description;
        break;
      }
    }
    info.enabled = policy_->IsListEnabled(info.name);
    if (!store.empty()) {
      std::error_code ec;
      const fs::path file = store / (info.name + ".txt");
      if (fs::is_regular_file(file, ec)) {
        info.present = true;
        info.size_bytes = static_cast<uint64_t>(fs::file_size(file, ec));
        const auto stamp = fs::last_write_time(file, ec);
        if (!ec) info.updated_ms = FileTimeToEpochMs(stamp);
      }
    }
    lists.push_back(std::move(info));
  }
  return lists;
}

bool AdblockService::SetFilterListEnabled(const std::string& name,
                                          bool enabled) {
  if (!enabled) {
    // Never leave the engine with nothing to load: count what would remain.
    namespace fs = std::filesystem;
    const fs::path store = PathFromUtf8(filter_dir_);
    std::error_code ec;
    size_t remaining = 0;
    for (const FilterListSpec& spec : kDefaultFilterLists) {
      if (name == spec.name || !policy_->IsListEnabled(spec.name)) continue;
      if (fs::is_regular_file(store / (std::string(spec.name) + ".txt"), ec)) {
        ++remaining;
      }
    }
    if (remaining == 0 &&
        !fs::is_regular_file(store / (std::string(kCustomRulesName) + ".txt"),
                             ec)) {
      return false;
    }
  }
  if (!policy_->SetListEnabled(name, enabled)) return false;
  ReloadEngineAsync();
  return true;
}

uint64_t AdblockService::ListsUpdatedMs() const {
  uint64_t newest = 0;
  for (const FilterListInfo& info : FilterLists()) {
    if (info.updated_ms > newest) newest = info.updated_ms;
  }
  return newest;
}

bool AdblockService::UpdateListsNow(
    std::function<void(const UpdateResult&)> done) {
  if (!updater_ || updating_) return false;
  updating_ = true;
  const bool started =
      updater_->UpdateNow([this, done](const UpdateResult& result) {
        updating_ = false;
        if (done) done(result);
      });
  if (!started) updating_ = false;
  return started;
}

// ---------------------------------------------------------------------------
// Custom rules
// ---------------------------------------------------------------------------

void AdblockService::ResetUserSettings() {
  policy_->ResetToDefaults();
  ReloadEngineAsync();
}

std::string AdblockService::LoadCustomRules() const {
  if (filter_dir_.empty()) return {};
  std::ifstream in(
      PathFromUtf8(filter_dir_) / (std::string(kCustomRulesName) + ".txt"),
      std::ios::binary);
  if (!in) return {};
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  if (text.size() > kMaxCustomRulesBytes) text.resize(kMaxCustomRulesBytes);
  return text;
}

AdblockService::CustomRulesStatus AdblockService::SaveCustomRules(
    const std::string& text) {
  namespace fs = std::filesystem;
  if (filter_dir_.empty()) return CustomRulesStatus::kUnavailable;
  if (text.size() > kMaxCustomRulesBytes) return CustomRulesStatus::kTooLarge;

  const fs::path target =
      PathFromUtf8(filter_dir_) / (std::string(kCustomRulesName) + ".txt");
  std::error_code ec;

  if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
    fs::remove(target, ec);
    ReloadEngineAsync();
    return CustomRulesStatus::kCleared;
  }

  // The loader applies exactly this validation to every list, so a text that
  // passes here is a text that will be loaded.
  if (!AdblockEngine::FromRules(text)) return CustomRulesStatus::kInvalid;

  fs::create_directories(target.parent_path(), ec);
  fs::path temp = target.parent_path() /
                  (std::string(".") + kCustomRulesName + ".tmp");
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out.good()) {
      out.close();
      fs::remove(temp, ec);
      return CustomRulesStatus::kWriteFailed;
    }
  }
  fs::rename(temp, target, ec);  // replaces an existing file
  if (ec) {
    fs::remove(temp, ec);
    return CustomRulesStatus::kWriteFailed;
  }
  ReloadEngineAsync();
  return CustomRulesStatus::kSaved;
}

}  // namespace vibrium
