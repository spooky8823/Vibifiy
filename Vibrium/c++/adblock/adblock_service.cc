#include "adblock_service.h"

#include <cstdlib>
#include <filesystem>
#include <utility>

#include "include/base/cef_logging.h"
#include "request_policy.h"

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
    if (!entry.is_regular_file(ec) || p.extension() != ".txt") continue;
    const auto dest = user_dir / p.filename();
    if (std::filesystem::exists(dest, ec) &&
        std::filesystem::file_size(dest, ec) == entry.file_size(ec)) {
      continue;
    }
    std::filesystem::copy_file(
        p, dest, std::filesystem::copy_options::overwrite_existing, ec);
  }
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
      cosmetic_handler_(std::make_unique<CosmeticQueryHandler>(engine_)),
      router_(CefMessageRouterBrowserSide::Create(CosmeticRouterConfig())),
      request_handler_(new AdblockRequestHandler(engine_, stats_, router_)) {
  router_->AddHandler(cosmetic_handler_.get(), /*first=*/false);
}

void AdblockService::Shutdown() {
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
    request_handler_ =
        new AdblockRequestHandler(engine_, stats_, nullptr);
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
  if (auto engine = AdblockEngine::FromDirectory(filter_dir)) {
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
    updater_ = FilterListUpdater::Create(engine_, filter_dir);
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

  // Scriptlets first, in their own call so a syntax error in one cannot stop
  // the content script. Note: ExecuteJavaScript is asynchronous, so a
  // scriptlet is not guaranteed to run before the page's own scripts.
  if (auto scriptlet = engine->CosmeticScript(url)) {
    frame->ExecuteJavaScript(BuildScriptletScript(*scriptlet), url, 0);
  }
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

}  // namespace vibrium
