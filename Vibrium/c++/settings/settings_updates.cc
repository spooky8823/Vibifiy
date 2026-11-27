#include "settings/settings_updates.h"

#include <algorithm>

#include "theme.h"
#include "update_launcher.h"

namespace vibrium {

using namespace settings;

namespace {
constexpr int kHitUpdate = 600;
}  // namespace

void UpdatesSection::Layout(const RECT& area) {
  width_ = area.right;
  height_ = area.bottom;
  hits_.Clear();

  const PageFrame frame = MakePageFrame(width_);
  title_ = frame.title;
  subtitle_ = frame.subtitle;

  installedBuild_ = vibrium::InstalledBuildLabel();
  updateCard_ = RECT{frame.cx, 112, frame.right, 112 + 176};
  updateButton_ = RECT{frame.cx + 22, updateCard_.bottom - 22 - 42,
                       frame.cx + 22 + 250, updateCard_.bottom - 22};
  hits_.Add(updateButton_, kHitUpdate);

  // The flash message sits on the line at the bottom of the window, where the
  // console keeps its footer.
  footer_ = RECT{frame.cx, std::max(0, height_ - 20 - 24) + 6, frame.right,
                 std::max(0, height_ - 20 - 24) + 26};
}

void UpdatesSection::OnClick(int id) {
  if (id != kHitUpdate) return;
  if (MessageBoxW(host_->window(),
                  L"Vibrium will close, download the latest build and reopen automatically.\n\n"
                  L"Open tabs will be closed. Continue?",
                  L"Update Vibrium Browser", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
    host_->RequestUpdate();
  }
}

void UpdatesSection::Paint(HDC hdc) {
  const SettingsFonts& f = fonts();
  DrawLabel(hdc, L"Updates", title_, f.title, kText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  DrawLabel(hdc, L"Install the newest Vibrium build made by CI. Nothing is compiled on this computer.", subtitle_,
            f.ui, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  FillRound(hdc, updateCard_, 14, kPanel, kBorder);
  DrawLabel(hdc, L"INSTALLED BUILD", RECT{updateCard_.left + 22, updateCard_.top + 16, updateCard_.right - 22, updateCard_.top + 34},
            f.small, kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  DrawLabel(hdc, installedBuild_, RECT{updateCard_.left + 22, updateCard_.top + 34, updateCard_.right - 22, updateCard_.top + 62},
            f.brand, kText, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  DrawLabel(hdc, L"Vibrium closes, downloads and installs the latest build, then reopens by itself.",
            RECT{updateCard_.left + 22, updateCard_.top + 70, updateCard_.right - 22, updateCard_.top + 92}, f.label,
            kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  const bool hover = hoverId_ == kHitUpdate;
  const COLORREF fill = hover ? Theme::Mix(Theme::Accent(), RGB(255, 255, 255), 14) : Theme::Accent();
  FillRound(hdc, updateButton_, 14, fill, fill);
  DrawLabel(hdc, L"Update Vibrium Browser", updateButton_, f.ui, RGB(255, 255, 255),
            DT_SINGLELINE | DT_VCENTER | DT_CENTER);
  if (!flash_.empty()) {
    DrawLabel(hdc, flash_, footer_, f.label, Theme::Accent(), DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  }
}

}  // namespace vibrium
