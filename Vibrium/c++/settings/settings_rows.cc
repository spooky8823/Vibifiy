#include "settings/settings_rows.h"

#include <algorithm>

#include "theme.h"

namespace vibrium {

using namespace settings;

RowsSection::Row RowsSection::Toggle(std::wstring title, std::wstring description,
                                     bool on, std::function<void()> flip) {
  Row row;
  row.kind = Row::kToggle;
  row.title = std::move(title);
  row.description = std::move(description);
  row.on = on;
  row.activate = std::move(flip);
  return row;
}

RowsSection::Row RowsSection::Button(std::wstring title, std::wstring description,
                                     std::wstring label, std::function<void()> run) {
  Row row;
  row.kind = Row::kButton;
  row.title = std::move(title);
  row.description = std::move(description);
  row.button = std::move(label);
  row.activate = std::move(run);
  return row;
}

RowsSection::Row RowsSection::Info(std::wstring title, std::wstring description) {
  Row row;
  row.title = std::move(title);
  row.description = std::move(description);
  return row;
}

void RowsSection::OnShow(bool /*page_opened*/) { Rebuild(); }

void RowsSection::Rebuild() {
  rows_.clear();
  BuildRows(&rows_);
  Layout(area_);
  RepaintContent();
}

void RowsSection::Layout(const RECT& area) {
  area_ = area;
  width_ = area.right;
  height_ = area.bottom;
  hits_.Clear();
  controls_.clear();

  const PageFrame frame = MakePageFrame(width_);
  title_ = frame.title;
  subtitle_ = frame.subtitle;
  const int footerTop = std::max(0, height_ - 20 - 24) + 6;
  footer_ = RECT{frame.cx, footerTop, frame.right, footerTop + 20};

  const int top = 104;
  card_ = RECT{frame.cx, top, frame.right,
               top + static_cast<int>(rows_.size()) * kRowHeight};

  HDC dc = GetDC(host_->window());
  for (size_t i = 0; i < rows_.size(); ++i) {
    const int rowTop = top + static_cast<int>(i) * kRowHeight;
    const int right = frame.right - 22;
    RECT control{};
    if (rows_[i].kind == Row::kToggle) {
      control = RECT{right - 48, rowTop + 23, right, rowTop + 49};
    } else if (rows_[i].kind == Row::kButton) {
      const int w = TextWidth(dc, fonts().ui, rows_[i].button) + 36;
      control = RECT{right - w, rowTop + 17, right, rowTop + 55};
    }
    controls_.push_back(control);
    if (rows_[i].kind != Row::kInfo) {
      hits_.Add(Inflated(control, 8, 8), kHitRow + static_cast<int>(i));
    }
  }
  ReleaseDC(host_->window(), dc);
}

void RowsSection::OnClick(int id) {
  const int index = id - kHitRow;
  if (index < 0 || index >= static_cast<int>(rows_.size())) return;
  const std::function<void()> action = rows_[static_cast<size_t>(index)].activate;
  if (action) action();
  Rebuild();  // the action may change what the rows say
}

void RowsSection::Paint(HDC hdc) {
  const SettingsFonts& f = fonts();
  DrawLabel(hdc, PageTitle(), title_, f.title, kText,
            DT_SINGLELINE | DT_VCENTER | DT_LEFT);
  DrawLabel(hdc, PageSubtitle(), subtitle_, f.ui, kMuted,
            DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

  if (rows_.empty()) return;
  FillRound(hdc, card_, 14, kPanel, kBorder);
  for (size_t i = 0; i < rows_.size(); ++i) {
    const Row& row = rows_[i];
    const int rowTop = card_.top + static_cast<int>(i) * kRowHeight;
    if (i > 0) {
      HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
      HGDIOBJ old = SelectObject(hdc, pen);
      MoveToEx(hdc, card_.left + 16, rowTop, nullptr);
      LineTo(hdc, card_.right - 16, rowTop);
      SelectObject(hdc, old);
      DeleteObject(pen);
    }
    const RECT& control = controls_[i];
    const int textRight = (row.kind == Row::kInfo ? card_.right - 22
                                                  : static_cast<int>(control.left) - 16);
    DrawLabel(hdc, row.title, RECT{card_.left + 22, rowTop + 12, textRight, rowTop + 36},
              f.ui, kText, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    DrawLabel(hdc, row.description,
              RECT{card_.left + 22, rowTop + 38, textRight, rowTop + 60}, f.label,
              kMuted, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    const bool hover = hoverId_ == kHitRow + static_cast<int>(i);
    if (row.kind == Row::kToggle) {
      DrawToggle(hdc, control, row.on, hover);
    } else if (row.kind == Row::kButton) {
      DrawPill(hdc, control, row.button, f.ui, false, hover);
    }
  }

  if (!flash_.empty()) {
    DrawLabel(hdc, flash_, footer_, f.label, Theme::Accent(),
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
  }
}

}  // namespace vibrium
