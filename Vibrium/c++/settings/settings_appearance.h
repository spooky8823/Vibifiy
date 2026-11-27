// "Appearance" page: accent colour palette, reset, and the custom colour wheel.
#ifndef VIBRIUM_SETTINGS_APPEARANCE_H_
#define VIBRIUM_SETTINGS_APPEARANCE_H_

#include "settings/settings_section.h"

namespace vibrium {

class AppearanceSection final : public SettingsSection {
 public:
  const wchar_t* Name() const override { return L"Appearance"; }
  void Layout(const RECT& area) override;
  void Paint(HDC hdc) override;

 protected:
  void OnClick(int id) override;

 private:
  RECT title_{};
  RECT subtitle_{};
  RECT palette_[6]{};
  RECT resetAccentRect_{};
  RECT customAccentRect_{};
};

}  // namespace vibrium

#endif  // VIBRIUM_SETTINGS_APPEARANCE_H_
