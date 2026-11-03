#include "adblock_service.h"

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
  if (command_line && command_line->HasSwitch("filter-dir")) {
    filter_dir = command_line->GetSwitchValue("filter-dir").ToString();
  }
  if (filter_dir.empty()) {
    // Deliberately not the working directory: that depends on how the
    // browser was launched.
    filter_dir = ToUtf8(ExecutableDirectory() / "filters");
  }

  filter_dir_ = filter_dir;
  if (auto engine = AdblockEngine::FromDirectory(filter_dir)) {
    engine_->Set(std::move(engine));
    enabled_ = true;
    LOG(INFO) << "Ad blocking enabled; filter lists from " << filter_dir;
    return true;
  }
  enabled_ = false;
  LOG(WARNING) << "Ad blocking DISABLED: no usable filter lists in "
               << filter_dir;
  return false;
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
