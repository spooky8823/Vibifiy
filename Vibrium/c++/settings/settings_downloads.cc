#include "settings/settings_downloads.h"

#include <shellapi.h>

#include "downloads.h"

namespace vibrium {

void DownloadsSection::BuildRows(std::vector<Row>* rows) {
  DownloadManager& manager = DownloadManager::Get();

  rows->push_back(Toggle(
      L"Ask where to save each file",
      L"Show a Save As window for every download instead of saving straight to your folder.",
      manager.askWhere(), []() {
        DownloadManager& m = DownloadManager::Get();
        m.SetAskWhere(!m.askWhere());
      }));

  rows->push_back(Info(L"Download folder", manager.DefaultFolder()));

  rows->push_back(Button(
      L"Open download folder", L"Show the folder in File Explorer.",
      L"Open folder", [this]() {
        const std::wstring folder = DownloadManager::Get().DefaultFolder();
        const HINSTANCE result = ShellExecuteW(host_->window(), L"open", folder.c_str(),
                                               nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) Flash(L"Could not open the folder.");
      }));
}

}  // namespace vibrium
