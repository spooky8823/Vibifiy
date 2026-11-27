// "Updates" page: installed build and the "Update Vibrium Browser" button.
#ifndef VIBRIUM_SETTINGS_UPDATES_H_
#define VIBRIUM_SETTINGS_UPDATES_H_

#include <string>

#include "settings/settings_section.h"

namespace vibrium {

class UpdatesSection final : public SettingsSection {
 public:
  const wchar_t* Name() const override { return L"Updates"; }
  void Layout(const RECT& area) override;
  void Paint(HDC hdc) override;

 protected:
  void OnClick(int id) override;

 private:
  RECT title_{};
  RECT subtitle_{};
  RECT updateCard_{};
  RECT updateButton_{};
  std::wstring installedBuild_;
};

}  // namespace vibrium

#endif  // VIBRIUM_SETTINGS_UPDATES_H_
