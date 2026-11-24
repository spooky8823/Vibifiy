// Download management: tracks every download, decides where files are saved
// and provides the native vibrium://downloads page.
//
// Saving behaviour (setting on the downloads page):
//   * default: files go straight to the user's Downloads folder;
//   * "Ask where to save each file": a Save As window appears per download.
#ifndef VIBRIUM_DOWNLOADS_H_
#define VIBRIUM_DOWNLOADS_H_

#include <windows.h>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_download_handler.h"
#include "native_page.h"

namespace vibrium {

// What the address bar shows for the downloads tab. Typing it opens the page.
inline constexpr wchar_t kDownloadsUrl[] = L"vibrium://downloads";

// True for "vibrium://downloads" (case-insensitive), optionally followed by a
// path, query or fragment.
bool IsDownloadsUrl(const std::wstring& url);

struct DownloadRecord {
  uint32_t id = 0;     // Vibrium's own id (stable while the list lives)
  uint32_t cefId = 0;  // CEF's id for the running transfer
  std::wstring url;
  std::wstring fileName;
  std::wstring path;
  int64_t received = 0;
  int64_t total = -1;  // -1 when unknown
  int64_t speed = 0;   // bytes per second
  int percent = -1;
  bool inProgress = false;
  bool complete = false;
  bool canceled = false;
  bool missing = false;  // finished, but the file is gone from disk
  std::vector<uint32_t> aliases;  // other CEF ids reporting this same download
  bool updated = false;  // has received at least one OnDownloadUpdated
  uint64_t timeMs = 0;   // Unix ms, start (or finish) time
};

// Used from the browser's UI thread only.
class DownloadManager {
 public:
  static DownloadManager& Get();

  // Reads settings and the saved list of finished downloads. Call once.
  void Load();

  // "Ask where to save each file" setting.
  bool askWhere() const { return askWhere_; }
  void SetAskWhere(bool value);
  // The user's Downloads folder.
  std::wstring DefaultFolder() const;

  // CEF glue (see BrowserClient::OnBeforeDownload / OnDownloadUpdated).
  void HandleBeforeDownload(CefRefPtr<CefDownloadItem> item,
                            const std::wstring& suggestedName,
                            CefRefPtr<CefBeforeDownloadCallback> callback);
  void HandleUpdated(CefRefPtr<CefDownloadItem> item,
                     CefRefPtr<CefDownloadItemCallback> callback);

  void Cancel(uint32_t id);
  // Removes an entry from the list (the file on disk is left alone).
  void Remove(uint32_t id);
  void ClearFinished();
  // Marks finished downloads whose file no longer exists.
  void RefreshFileState();

  const std::vector<DownloadRecord>& records() const { return records_; }
  uint64_t revision() const { return revision_; }
  // Counters the download popup watches to know when to appear.
  uint32_t startCount() const { return startCount_; }
  uint32_t finishCount() const { return finishCount_; }
  bool HasActive() const;

 private:
  DownloadManager() = default;

  std::wstring ListPath() const;
  void Save() const;
  DownloadRecord* FindByCefId(uint32_t cefId);
  DownloadRecord* FindById(uint32_t id);
  // A record created in OnBeforeDownload that has not yet been matched to a
  // CEF update (ids can differ between the two callbacks).
  DownloadRecord* FindPendingMatch(const std::wstring& url,
                                   const std::wstring& path);

  std::vector<DownloadRecord> records_;
  std::map<uint32_t, CefRefPtr<CefDownloadItemCallback>> callbacks_;
  uint32_t nextId_ = 1;
  uint64_t revision_ = 1;
  uint32_t startCount_ = 0;
  uint32_t finishCount_ = 0;
  bool askWhere_ = false;
  bool loaded_ = false;
};

// The vibrium://downloads page.
class DownloadsPage final : public NativePage {
 public:
  bool Create(HWND parent, HINSTANCE instance) {
    return NativePage::Create(parent, instance, L"VibriumDownloadsPage");
  }

 protected:
  void OnShown() override;
  bool OnTick() override;
  void Paint(HDC hdc, const RECT& client) override;
  void OnClick(int id, int x, int y) override;

 private:
  std::vector<uint32_t> shownIds_;  // record ids in display order
  uint64_t paintedRevision_ = 0;
};


// Small always-on-top-of-the-browser panel in the top-right corner (black
// background, accent-coloured border) that appears when a download starts or
// finishes, replacing CEF's built-in download bubble.
class DownloadPopup {
 public:
  DownloadPopup() = default;
  ~DownloadPopup();
  DownloadPopup(const DownloadPopup&) = delete;
  DownloadPopup& operator=(const DownloadPopup&) = delete;

  bool Create(HWND owner, HINSTANCE instance);
  // Top-right corner of the panel, in screen coordinates.
  void SetAnchor(POINT topRightScreen);
  // Called when "Show all downloads" is clicked.
  void SetOnShowAll(std::function<void()> callback) {
    onShowAll_ = std::move(callback);
  }

 private:
  struct Hit {
    RECT rect;
    int id;
  };

  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  void OnTimer();
  void ShowPanel();
  void HidePanel();
  void Layout();
  void PaintAll(HDC hdc, const RECT& client);
  int HitTest(int x, int y) const;
  void OnClick(int id);

  HWND hwnd_ = nullptr;
  HWND owner_ = nullptr;
  HINSTANCE instance_ = nullptr;
  POINT anchor_{0, 0};
  bool visible_ = false;
  bool tracking_ = false;
  int hoverId_ = 0;
  uint64_t hideAtMs_ = 0;
  uint64_t paintedRevision_ = 0;
  uint32_t seenStart_ = 0;
  uint32_t seenFinish_ = 0;
  std::vector<uint32_t> shownIds_;
  std::vector<Hit> hits_;
  HFONT fontBold_ = nullptr;
  HFONT fontSmall_ = nullptr;
  HFONT fontTitle_ = nullptr;
  std::function<void()> onShowAll_;
};

}  // namespace vibrium

#endif  // VIBRIUM_DOWNLOADS_H_