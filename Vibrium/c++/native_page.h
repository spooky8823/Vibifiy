// Shared base for Vibrium's native internal pages (vibrium://history,
// vibrium://downloads). Like the settings page, each one is a child window
// of the main window that sits where a tab's browser view would be.
//
// The base class owns the window, off-screen painting, fonts, mouse
// tracking, hit-testing and a simple scrollbar. A page only has to implement
// Paint() (registering clickable areas with AddHit) and OnClick().
#ifndef VIBRIUM_UI_NATIVE_PAGE_H_
#define VIBRIUM_UI_NATIVE_PAGE_H_

#include <windows.h>

#include <string>
#include <vector>

namespace vibrium {

// Palette shared with the settings page.
namespace pageui {
inline constexpr COLORREF kBg = RGB(18, 18, 20);
inline constexpr COLORREF kPanel = RGB(27, 27, 30);
inline constexpr COLORREF kPanel2 = RGB(35, 35, 39);
inline constexpr COLORREF kPanel3 = RGB(43, 43, 48);
inline constexpr COLORREF kBorder = RGB(52, 52, 57);
inline constexpr COLORREF kText = RGB(241, 237, 250);
inline constexpr COLORREF kMuted = RGB(157, 148, 177);
inline constexpr COLORREF kGood = RGB(95, 211, 154);
inline constexpr COLORREF kBad = RGB(255, 122, 144);
}  // namespace pageui

class NativePage {
 public:
  virtual ~NativePage();

  NativePage(const NativePage&) = delete;
  NativePage& operator=(const NativePage&) = delete;

  // `className` must be unique per page type.
  bool Create(HWND parent, HINSTANCE instance, const wchar_t* className);

  // Positions the page over `bounds` (parent client coordinates), shows it
  // and starts the refresh timer.
  void Show(const RECT& bounds);
  void Hide();
  void Resize(const RECT& bounds);

  bool visible() const { return visible_; }
  HWND handle() const { return hwnd_; }

 protected:
  NativePage() = default;

  // ---- hooks for pages -------------------------------------------------
  // Called once after the window exists (create child controls here).
  virtual void OnCreated() {}
  // Called every time the page becomes visible (reload data here).
  virtual void OnShown() {}
  // Called when the window size changes.
  virtual void OnResized(int /*width*/, int /*height*/) {}
  // Called every ~400 ms while visible. Return true to repaint.
  virtual bool OnTick() { return false; }
  // Draw the whole page. Call AddHit() for clickable areas and SetScroll()
  // for scrollable content.
  virtual void Paint(HDC hdc, const RECT& client) = 0;
  virtual void OnClick(int id, int x, int y) = 0;
  // Gives a page first look at raw messages (WM_COMMAND, WM_CTLCOLOREDIT...).
  // Return true and set *result if handled.
  virtual bool OnMessage(UINT /*message*/, WPARAM /*wParam*/,
                         LPARAM /*lParam*/, LRESULT* /*result*/) {
    return false;
  }

  // ---- helpers for pages -----------------------------------------------
  void AddHit(const RECT& rect, int id);  // id must be > 0
  int hoverId() const { return hoverId_; }
  int pressedId() const { return pressedId_; }
  int mouseX() const { return mouseX_; }
  int mouseY() const { return mouseY_; }
  int scrollY() const { return scrollY_; }
  void ScrollToTop();
  void Repaint();

  // Declares the scrollable content: total height, visible height and the
  // scrollbar track. Call from Paint() before drawing the scrolled content.
  void SetScroll(int contentHeight, int viewHeight, const RECT& track);
  void PaintScrollbar(HDC hdc) const;

  static void Fill(HDC hdc, const RECT& rect, COLORREF color);
  static void FillRound(HDC hdc, const RECT& rect, int radius, COLORREF fill,
                        COLORREF border);
  void DrawLabel(HDC hdc, const std::wstring& text, RECT rect, HFONT font,
                 COLORREF color, UINT flags = DT_LEFT) const;
  // Pill-shaped text button. `primary` fills it with the accent colour.
  // Also registers the rectangle as a hit area with `hitId`.
  void DrawButton(HDC hdc, const RECT& rect, const std::wstring& label,
                  int hitId, bool primary);

  HWND hwnd_ = nullptr;
  HWND parent_ = nullptr;
  HINSTANCE instance_ = nullptr;

  HFONT fontUi_ = nullptr;      // 15px
  HFONT fontBold_ = nullptr;    // 15px semibold
  HFONT fontSmall_ = nullptr;   // 12px
  HFONT fontButton_ = nullptr;  // 13px semibold
  HFONT fontTitle_ = nullptr;   // 28px semibold

 private:
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam);
  LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
  int HitTest(int x, int y) const;
  bool ThumbRect(RECT* rect) const;
  int MaxScroll() const;
  void ClampScroll();

  struct Hit {
    RECT rect;
    int id;
  };

  bool visible_ = false;
  bool trackingMouse_ = false;
  std::vector<Hit> hits_;

  int hoverId_ = 0;
  int pressedId_ = 0;
  int mouseX_ = -1;
  int mouseY_ = -1;

  int scrollY_ = 0;
  int contentHeight_ = 0;
  int viewHeight_ = 0;
  RECT track_{};
  bool draggingThumb_ = false;
  int dragStartY_ = 0;
  int dragStartScroll_ = 0;
};

}  // namespace vibrium

#endif  // VIBRIUM_UI_NATIVE_PAGE_H_