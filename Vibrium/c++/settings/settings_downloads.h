// "Downloads" page (see settings_downloads.cc).
#ifndef VIBRIUM_SETTINGS_DOWNLOADS_H_
#define VIBRIUM_SETTINGS_DOWNLOADS_H_

#include "settings/settings_rows.h"

namespace vibrium {

class DownloadsSection final : public RowsSection {
 public:
  const wchar_t* Name() const override { return L"Downloads"; }

 protected:
  const wchar_t* PageTitle() const override { return L"Downloads"; }
  const wchar_t* PageSubtitle() const override { return L"Where and how files are saved."; }
  void BuildRows(std::vector<Row>* rows) override;
};

}  // namespace vibrium

#endif  // VIBRIUM_SETTINGS_DOWNLOADS_H_
