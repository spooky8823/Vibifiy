#include "filter_list_updater.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <utility>
#include <vector>

#include "filter_list_catalog.h"
#include "include/base/cef_logging.h"
#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_task.h"
#include "include/cef_urlrequest.h"
#include "vibrium_adblock.h"

namespace vibrium {

namespace {

namespace fs = std::filesystem;

constexpr int kMinAgeHours = 24;       // do not re-download a fresher file
constexpr int kRecheckHours = 6;       // how often the schedule wakes up
constexpr size_t kMaxListBytes = 10 * 1024 * 1024;  // same cap as the Rust side

class LambdaTask : public CefTask {
 public:
  explicit LambdaTask(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Execute() override { fn_(); }

 private:
  std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(LambdaTask);
};

void Post(CefThreadId thread, std::function<void()> fn) {
  CefPostTask(thread, new LambdaTask(std::move(fn)));
}

void PostDelayed(CefThreadId thread, std::function<void()> fn, int64_t ms) {
  CefPostDelayedTask(thread, new LambdaTask(std::move(fn)), ms);
}

struct Download {
  std::string name;
  std::string url;
  std::string etag;       // sent as If-None-Match
  // Filled in by the network callbacks (UI thread).
  bool transport_ok = false;
  int status = 0;
  size_t content_length = SIZE_MAX;
  std::string new_etag;
  std::string body;
  bool too_large = false;
};

struct Batch {
  std::string directory;
  std::shared_ptr<AdblockEngineHolder> engine;
  std::vector<Download> downloads;
  size_t pending = 0;
  std::function<void()> on_done;  // called on the UI thread
};

fs::path ListPath(const std::string& dir, const std::string& name) {
  return fs::path(dir) / (name + ".txt");
}

bool IsFresh(const fs::path& file) {
  std::error_code ec;
  if (!fs::is_regular_file(file, ec)) return false;
  const auto written = fs::last_write_time(file, ec);
  if (ec) return false;
  const auto age = fs::file_time_type::clock::now() - written;
  return age < std::chrono::hours(kMinAgeHours);
}

std::string ReadSmallFile(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::string s;
  std::getline(in, s);
  return s;
}

// Runs on TID_FILE_BACKGROUND: validate, persist atomically, rebuild, swap.
void Commit(std::shared_ptr<Batch> batch) {
  bool changed = false;
  for (Download& d : batch->downloads) {
    const fs::path target = ListPath(batch->directory, d.name);
    std::error_code ec;

    if (d.transport_ok && d.status == 304) {
      fs::last_write_time(target, fs::file_time_type::clock::now(), ec);
      continue;
    }
    if (!d.transport_ok || d.too_large || d.status != 200) {
      LOG(WARNING) << "Filter list '" << d.name << "' not updated (transport="
                   << d.transport_ok << ", status=" << d.status << ")";
      continue;
    }
    // Rejects HTML/JSON error pages, truncated bodies and prose.
    const int32_t valid = vibrium_adblock_validate_filter_response(
        d.name.c_str(), d.url.c_str(), static_cast<uint16_t>(d.status),
        d.content_length,
        reinterpret_cast<const uint8_t*>(d.body.data()), d.body.size());
    if (valid != 0) {
      LOG(WARNING) << "Filter list '" << d.name << "' failed validation";
      continue;
    }
    // Dot-prefixed temp name: the Rust loader ignores it.
    const fs::path tmp = fs::path(batch->directory) / ("." + d.name + ".tmp");
    {
      std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
      out.write(d.body.data(), static_cast<std::streamsize>(d.body.size()));
      if (!out.good()) {
        LOG(WARNING) << "Could not write filter list '" << d.name << "'";
        fs::remove(tmp, ec);
        continue;
      }
    }
    fs::rename(tmp, target, ec);  // replaces an existing file
    if (ec) {
      LOG(WARNING) << "Could not install filter list '" << d.name
                   << "': " << ec.message();
      fs::remove(tmp, ec);
      continue;
    }
    if (!d.new_etag.empty()) {
      std::ofstream(ListPath(batch->directory, d.name).replace_extension(".etag"),
                    std::ios::binary | std::ios::trunc)
          << d.new_etag;
    }
    changed = true;
    LOG(INFO) << "Updated filter list '" << d.name << "' (" << d.body.size()
              << " bytes)";
  }

  if (!changed) return;
  // Rebuild from everything on disk and publish. Requests already running keep
  // the previous engine alive through their shared_ptr.
  if (auto engine = AdblockEngine::FromDirectory(batch->directory)) {
    batch->engine->Set(std::move(engine));
    LOG(INFO) << "Ad-block engine reloaded from " << batch->directory;
  } else {
    LOG(WARNING) << "Reload failed; keeping the previous ad-block engine";
  }
}

class DownloadClient : public CefURLRequestClient {
 public:
  DownloadClient(std::shared_ptr<Batch> batch, size_t index)
      : batch_(std::move(batch)), index_(index) {}

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    Download& d = batch_->downloads[index_];
    d.transport_ok = request->GetRequestStatus() == UR_SUCCESS;
    if (CefRefPtr<CefResponse> r = request->GetResponse()) {
      d.status = r->GetStatus();
      d.new_etag = r->GetHeaderByName("ETag").ToString();
      const std::string len = r->GetHeaderByName("Content-Length").ToString();
      if (!len.empty()) {
        char* end = nullptr;
        const unsigned long long v = std::strtoull(len.c_str(), &end, 10);
        if (end != len.c_str()) d.content_length = static_cast<size_t>(v);
      }
    }
    if (--batch_->pending == 0 && batch_->on_done) {
      auto done = std::move(batch_->on_done);
      auto batch = batch_;
      Post(TID_FILE_BACKGROUND, [batch, done]() {
        Commit(batch);
        Post(TID_UI, done);
      });
    }
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest> request, const void* data,
                      size_t size) override {
    Download& d = batch_->downloads[index_];
    if (d.too_large) return;
    if (d.body.size() + size > kMaxListBytes) {
      d.too_large = true;
      d.body.clear();
      request->Cancel();
      return;
    }
    d.body.append(static_cast<const char*>(data), size);
  }
  bool GetAuthCredentials(bool, const CefString&, int, const CefString&,
                          const CefString&,
                          CefRefPtr<CefAuthCallback>) override {
    return false;  // never answer auth challenges
  }

 private:
  std::shared_ptr<Batch> batch_;
  size_t index_;
  IMPLEMENT_REFCOUNTING(DownloadClient);
};

}  // namespace

std::shared_ptr<FilterListUpdater> FilterListUpdater::Create(
    std::shared_ptr<AdblockEngineHolder> engine, std::string directory) {
  return std::shared_ptr<FilterListUpdater>(
      new FilterListUpdater(std::move(engine), std::move(directory)));
}

FilterListUpdater::FilterListUpdater(
    std::shared_ptr<AdblockEngineHolder> engine, std::string directory)
    : engine_(std::move(engine)), directory_(std::move(directory)) {}

void FilterListUpdater::Start() {
  if (started_) return;
  started_ = true;
  RunOnce();
}

void FilterListUpdater::Cancel() { cancelled_ = true; }

void FilterListUpdater::ScheduleNext() {
  if (cancelled_) return;
  auto self = shared_from_this();
  PostDelayed(TID_UI, [self]() { if (!self->cancelled_) self->RunOnce(); },
              int64_t{kRecheckHours} * 3600 * 1000);
}

void FilterListUpdater::RunOnce() {
  if (cancelled_ || running_) return;

  auto batch = std::make_shared<Batch>();
  batch->directory = directory_;
  batch->engine = engine_;
  for (const FilterListSpec& spec : kDefaultFilterLists) {
    const fs::path file = ListPath(directory_, spec.name);
    if (IsFresh(file)) continue;
    Download d;
    d.name = spec.name;
    d.url = spec.url;
    // Only send the ETag if the file it describes still exists.
    std::error_code ec;
    if (fs::is_regular_file(file, ec)) {
      d.etag = ReadSmallFile(fs::path(file).replace_extension(".etag"));
    }
    batch->downloads.push_back(std::move(d));
  }
  if (batch->downloads.empty()) {
    ScheduleNext();
    return;
  }

  running_ = true;
  batch->pending = batch->downloads.size();
  auto self = shared_from_this();
  batch->on_done = [self]() {
    self->running_ = false;
    self->ScheduleNext();
  };
  LOG(INFO) << "Checking " << batch->downloads.size() << " filter list(s)";

  for (size_t i = 0; i < batch->downloads.size(); ++i) {
    const Download& d = batch->downloads[i];
    CefRefPtr<CefRequest> request = CefRequest::Create();
    request->SetURL(d.url);
    request->SetMethod("GET");
    request->SetFlags(UR_FLAG_SKIP_CACHE);
    if (!d.etag.empty()) request->SetHeaderByName("If-None-Match", d.etag, true);
    CefURLRequest::Create(request, new DownloadClient(batch, i), nullptr);
  }
}

}  // namespace vibrium
