// "Privacy" page (see settings_privacy.cc).
#ifndef VIBRIUM_SETTINGS_PRIVACY_H_
#define VIBRIUM_SETTINGS_PRIVACY_H_

#include "settings/settings_rows.h"

namespace vibrium {

class PrivacySection final : public RowsSection {
 public:
  const wchar_t* Name() const override { return L"Privacy"; }

 protected:
  const wchar_t* PageTitle() const override { return L"Privacy"; }
  const wchar_t* PageSubtitle() const override { return L"Control what websites learn about you and what Vibrium remembers."; }
  void BuildRows(std::vector<Row>* rows) override;
};

}  // namespace vibrium

#endif  // VIBRIUM_SETTINGS_PRIVACY_H_
