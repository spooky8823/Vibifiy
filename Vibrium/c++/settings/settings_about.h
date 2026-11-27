// "About" page (see settings_about.cc).
#ifndef VIBRIUM_SETTINGS_ABOUT_H_
#define VIBRIUM_SETTINGS_ABOUT_H_

#include "settings/settings_rows.h"

namespace vibrium {

class AboutSection final : public RowsSection {
 public:
  const wchar_t* Name() const override { return L"About"; }

 protected:
  const wchar_t* PageTitle() const override { return L"About"; }
  const wchar_t* PageSubtitle() const override { return L"Version information and your Vibrium data."; }
  void BuildRows(std::vector<Row>* rows) override;
};

}  // namespace vibrium

#endif  // VIBRIUM_SETTINGS_ABOUT_H_
