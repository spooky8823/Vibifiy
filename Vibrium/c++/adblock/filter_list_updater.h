// Downloads the lists in filter_list_catalog.h with CEF's network stack, stores
// them atomically in the user filter directory and hot-swaps the engine.
//
// Threading: Start()/Cancel() on the browser UI thread. Disk I/O and the engine
// rebuild run on TID_FILE_BACKGROUND; the new engine is published through
// AdblockEngineHolder::Set(), which is thread-safe, so in-flight requests keep
// the old engine until they finish.
//
// Failure policy: like the rest of ad blocking this fails open. A failed
// download, bad response or unparseable list is logged and skipped; the lists
// already on disk keep working.
#ifndef VIBRIUM_FILTER_LIST_UPDATER_H_
#define VIBRIUM_FILTER_LIST_UPDATER_H_

#include <memory>
#include <string>

#include "adblock_engine.h"

namespace vibrium {

class FilterListUpdater : public std::enable_shared_from_this<FilterListUpdater> {
 public:
  static std::shared_ptr<FilterListUpdater> Create(
      std::shared_ptr<AdblockEngineHolder> engine, std::string directory);

  // Refreshes every list older than kMinAgeHours, then re-checks every
  // kRecheckHours. Safe to call once; later calls are ignored.
  void Start();
  void Cancel();

 private:
  FilterListUpdater(std::shared_ptr<AdblockEngineHolder> engine,
                    std::string directory);
  friend struct UpdaterAccess;

  void RunOnce();
  void ScheduleNext();

  std::shared_ptr<AdblockEngineHolder> engine_;
  std::string directory_;
  bool started_ = false;
  bool cancelled_ = false;
  bool running_ = false;
};

}  // namespace vibrium

#endif  // VIBRIUM_FILTER_LIST_UPDATER_H_
