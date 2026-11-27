// Base for simple pages made of one card of rows: an on/off switch, a button,
// or a line of information per row. A page only says what its rows are.
#ifndef VIBRIUM_SETTINGS_ROWS_H_
#define VIBRIUM_SETTINGS_ROWS_H_

#include <functional>
#include <string>
#include <vector>

#include "settings/settings_section.h"

namespace vibrium {

class RowsSection : public SettingsSection {
 public:
  struct Row {
    enum Kind { kToggle, kButton, kInfo };
    Kind kind = kInfo;
    std::wstring title;
    std::wstring description;
    bool on = false;                 // kToggle: current state
    std::wstring button;             // kButton: label
    std::function<void()> activate;  // kToggle: flip it; kButton: run it
  };

  static Row Toggle(std::wstring title, std::wstring description, bool on,
                    std::function<void()> flip);
  static Row Button(std::wstring title, std::wstring description,
                    std::wstring label, std::function<void()> run);
  static Row Info(std::wstring title, std::wstring description);

  void OnShow(bool page_opened) override;
  void Layout(const RECT& area) override;
  void Paint(HDC hdc) override;

 protected:
  virtual const wchar_t* PageTitle() const = 0;
  virtual const wchar_t* PageSubtitle() const = 0;
  // Called whenever the page is shown and after every action.
  virtual void BuildRows(std::vector<Row>* rows) = 0;

  void OnClick(int id) override;
  void Rebuild();

 private:
  static constexpr int kRowHeight = 72;
  static constexpr int kHitRow = 800;  // + row index

  RECT area_{};
  RECT title_{};
  RECT subtitle_{};
  RECT card_{};
  std::vector<Row> rows_;
  std::vector<RECT> controls_;
};

}  // namespace vibrium

#endif  // VIBRIUM_SETTINGS_ROWS_H_
