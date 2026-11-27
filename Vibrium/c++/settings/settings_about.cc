#include "settings/settings_about.h"

#include <shellapi.h>

#include <cstdlib>
#include <string>

#include "adblock/adblock_service.h"
#include "include/cef_version.h"
#include "update_launcher.h"

namespace vibrium {

namespace {

std::wstring DataFolder() {
  if (const wchar_t* base = _wgetenv(L"LOCALAPPDATA")) {
    return std::wstring(base) + L"\\Vibrium";
  }
  return L"";
}

}  // namespace

void AboutSection::BuildRows(std::vector<Row>* rows) {
  rows->push_back(Info(L"Installed build", InstalledBuildLabel()));
  rows->push_back(Info(L"Chromium Embedded Framework", L"" CEF_VERSION));

  const std::wstring data = DataFolder();
  rows->push_back(Button(
      L"Vibrium data", data.empty() ? L"Not available." : data, L"Open folder",
      [this, data]() {
        if (data.empty()) {
          Flash(L"The data folder is not available.");
          return;
        }
        const HINSTANCE result = ShellExecuteW(host_->window(), L"open", data.c_str(),
                                               nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) Flash(L"Could not open the folder.");
      }));

  rows->push_back(Button(
      L"Reset ad-blocker settings",
      L"Back to the defaults: everything on, no allowlist. Your custom rules are kept.",
      L"Reset", [this]() {
        if (MessageBoxW(host_->window(),
                        L"Reset all ad-blocker settings to their defaults?\n\n"
                        L"Your allowlist will be cleared. Custom rules are kept.",
                        L"Reset ad-blocker settings",
                        MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
          return;
        }
        AdblockService::Get().ResetUserSettings();
        Flash(L"Ad-blocker settings were reset.");
      }));
}

}  // namespace vibrium
