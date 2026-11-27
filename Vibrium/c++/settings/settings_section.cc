#include "settings/settings_section.h"

namespace vibrium {

using namespace settings;

void SettingsSection::Flash(const std::wstring& message) {
  flash_ = message;
  host_->StartFlashTimer();
  RepaintRect(footer_);
}

void SettingsSection::OnFlashExpired() {
  flash_.clear();
  RepaintRect(footer_);
}

void SettingsSection::MouseMove(int x, int y) {
  const int id = hits_.Test(x, y);
  if (id != hoverId_) {
    hoverId_ = id;
    RepaintContent();
  }
}

void SettingsSection::MouseLeave() {
  if (hoverId_ != 0) {
    hoverId_ = 0;
    RepaintContent();
  }
}

bool SettingsSection::MouseDown(int x, int y) {
  pressedId_ = hits_.Test(x, y);
  return false;
}

void SettingsSection::MouseUp(int x, int y) {
  const int id = hits_.Test(x, y);
  const int pressed = pressedId_;
  pressedId_ = 0;
  if (pressed != 0 && pressed == id) OnClick(id);
}

HBRUSH SettingsSection::CtlColorEdit(HDC dc, HWND /*edit*/) {
  SetTextColor(dc, kText);
  SetBkColor(dc, kPanel2);
  return host_->edit_brush();
}

}  // namespace vibrium
