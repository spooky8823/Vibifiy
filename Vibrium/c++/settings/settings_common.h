// Palette, metrics, GDI helpers and small UI building blocks shared by every
// page of the settings window (Console, Ad blocker, Appearance, Updates).
//
// Nothing here knows about a particular page. The helpers are the ones the
// original single-file settings page used, moved here unchanged so every page
// draws exactly as before.
#ifndef VIBRIUM_SETTINGS_COMMON_H_
#define VIBRIUM_SETTINGS_COMMON_H_

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace vibrium::settings {

// ---- palette ---------------------------------------------------------------
inline constexpr COLORREF kBg = RGB(18, 18, 20);
inline constexpr COLORREF kPanel = RGB(27, 27, 30);
inline constexpr COLORREF kPanel2 = RGB(35, 35, 39);
inline constexpr COLORREF kPanel3 = RGB(43, 43, 48);
inline constexpr COLORREF kLogBg = RGB(14, 14, 16);
inline constexpr COLORREF kBorder = RGB(52, 52, 57);
inline constexpr COLORREF kBorderStrong = RGB(72, 72, 78);
inline constexpr COLORREF kSoft = RGB(48, 48, 54);
inline constexpr COLORREF kSoftStrong = RGB(58, 58, 64);
inline constexpr COLORREF kText = RGB(241, 237, 250);
inline constexpr COLORREF kMuted = RGB(157, 148, 177);
inline constexpr COLORREF kGood = RGB(95, 211, 154);
inline constexpr COLORREF kBad = RGB(255, 122, 144);
inline constexpr COLORREF kWarn = RGB(242, 184, 75);

// ---- metrics ---------------------------------------------------------------
inline constexpr int kSidebarWidth = 240;
inline constexpr int kPad = 32;
inline constexpr int kRowHeight = 22;
inline constexpr int kScrollbarWidth = 8;

// Signed mouse coordinates without windowsx.h, whose common-control macros
// collide with CEF's GetFirstChild/GetNextSibling methods.
inline int MouseX(LPARAM lp) {
  return static_cast<int>(static_cast<short>(LOWORD(lp)));
}
inline int MouseY(LPARAM lp) {
  return static_cast<int>(static_cast<short>(HIWORD(lp)));
}

inline bool InRect(const RECT& rect, int x, int y) {
  const POINT point{x, y};
  return PtInRect(&rect, point) != FALSE;
}

// ---- strings ---------------------------------------------------------------
std::wstring Utf8ToWide(const std::string& value);
std::string WideToUtf8(const std::wstring& value);
std::wstring Timestamp(uint64_t milliseconds);  // "17:21:05.920"
std::wstring FormatCount(uint64_t value);       // "1,234,567"
std::wstring ToLower(std::wstring text);
std::wstring Trim(const std::wstring& value);

// ---- GDI helpers -----------------------------------------------------------
void FillRound(HDC hdc, const RECT& rect, int radius, COLORREF fill,
               COLORREF border = CLR_INVALID, int borderWidth = 1);
void DrawLabel(HDC hdc, const std::wstring& text, RECT rect, HFONT font,
               COLORREF color, UINT flags);
int TextWidth(HDC hdc, HFONT font, const std::wstring& text);
HFONT MakeFont(int pixelHeight, int weight, const wchar_t* face);
// ">_" terminal glyph, 18x18, top-left at (x, y).
void DrawTerminalIcon(HDC hdc, int x, int y, COLORREF color);

inline RECT Inflated(RECT r, int dx, int dy) {
  r.left -= dx; r.right += dx; r.top -= dy; r.bottom += dy;
  return r;
}
// On/off switch drawn with the accent colour.
void DrawToggle(HDC hdc, const RECT& track, bool on, bool hover);
// Rounded button in the style of the console toolbar.
void DrawPill(HDC hdc, const RECT& rect, const std::wstring& label, HFONT font,
              bool primary, bool hover, bool enabled = true);

// ---- hit areas -------------------------------------------------------------
// Clickable rectangles registered while laying a page out. The first match
// wins, like the original page's list.
class HitMap {
 public:
  void Clear() { hits_.clear(); }
  void Add(const RECT& rect, int id) { hits_.push_back({rect, id}); }
  int Test(int x, int y) const {
    for (const Entry& entry : hits_) {
      if (InRect(entry.rect, x, y)) return entry.id;
    }
    return 0;
  }
  RECT RectOf(int id) const {
    for (const Entry& entry : hits_) {
      if (entry.id == id) return entry.rect;
    }
    return RECT{};
  }

 private:
  struct Entry {
    RECT rect;
    int id;
  };
  std::vector<Entry> hits_;
};

// ---- scrollable list -------------------------------------------------------
// Geometry and scrolling for a bordered list panel with fixed-height rows and
// an accent-coloured scrollbar thumb. This is the console log's scrolling,
// factored out so the other lists behave identically.
struct ScrollView {
  RECT rect{};  // the whole bordered panel, in window client coordinates
  int rowHeight = kRowHeight;
  int count = 0;  // number of rows
  int scrollY = 0;
  bool dragging = false;
  int dragStartY = 0;
  int dragStartScroll = 0;

  int ViewHeight() const {
    return std::max(0, static_cast<int>(rect.bottom - rect.top) - 8);
  }
  int ContentHeight() const { return count * rowHeight; }
  int MaxScroll() const { return std::max(0, ContentHeight() - ViewHeight()); }
  void Clamp() { scrollY = std::clamp(scrollY, 0, MaxScroll()); }
  void ScrollToBottom() { scrollY = MaxScroll(); }
  bool AtBottom() const { return scrollY >= MaxScroll() - 4; }
  int RowTop(int index) const { return rect.top + 4 + index * rowHeight - scrollY; }
  int FirstVisible() const { return std::max(0, scrollY / rowHeight); }
  int LastVisible() const {
    return std::min(count - 1, (scrollY + ViewHeight()) / rowHeight);
  }

  // Row under (x, y), or -1. The scrollbar column is not part of a row.
  int RowAt(int x, int y) const {
    RECT area = rect;
    area.right -= kScrollbarWidth + 8;
    if (!InRect(area, x, y)) return -1;
    const int local = y - (rect.top + 4) + scrollY;
    if (local < 0) return -1;
    const int index = local / rowHeight;
    return index < count ? index : -1;
  }

  bool Thumb(RECT* out) const {
    const int view = ViewHeight();
    const int content = ContentHeight();
    if (view <= 0 || content <= view) return false;
    const int thumbHeight = std::max(32, view * view / content);
    const int travel = view - thumbHeight;
    const int maxScroll = content - view;
    const int top = rect.top + 4 + (maxScroll > 0 ? travel * scrollY / maxScroll : 0);
    *out = RECT{rect.right - kScrollbarWidth - 4, top, rect.right - 4,
                top + thumbHeight};
    return true;
  }

  // Starts a thumb drag if (x, y) is on the thumb.
  bool BeginDrag(int x, int y) {
    RECT thumb{};
    if (!Thumb(&thumb) || !InRect(thumb, x, y)) return false;
    dragging = true;
    dragStartY = y;
    dragStartScroll = scrollY;
    return true;
  }
  // Moves the drag; returns true when the scroll position changed.
  bool Drag(int y) {
    RECT thumb{};
    const int view = ViewHeight();
    const int content = ContentHeight();
    if (!Thumb(&thumb) || content <= view) return false;
    const int thumbHeight = thumb.bottom - thumb.top;
    const int travel = view - thumbHeight;
    if (travel <= 0) return false;
    scrollY = dragStartScroll + (y - dragStartY) * (content - view) / travel;
    Clamp();
    return true;
  }
  void Wheel(int notches) {
    scrollY -= notches * 3 * rowHeight;
    Clamp();
  }
  // Arrow/page/home/end keys. Returns false for any other key.
  bool Key(WPARAM key) {
    const int page = std::max(rowHeight, ViewHeight() - rowHeight);
    switch (key) {
      case VK_UP:    scrollY -= rowHeight; break;
      case VK_DOWN:  scrollY += rowHeight; break;
      case VK_PRIOR: scrollY -= page; break;
      case VK_NEXT:  scrollY += page; break;
      case VK_HOME:  scrollY = 0; break;
      case VK_END:   scrollY = MaxScroll(); break;
      default: return false;
    }
    Clamp();
    return true;
  }
};

// ---- page frame ------------------------------------------------------------
// Column and header geometry shared by every page: content starts right of the
// sidebar and padding, the title and subtitle sit at fixed heights.
struct PageFrame {
  int cx = 0;     // left edge of the content column
  int cw = 0;     // width of the content column
  int right = 0;  // right edge of the content column
  RECT title{};
  RECT subtitle{};
};

inline PageFrame MakePageFrame(int windowWidth) {
  PageFrame f;
  f.cx = kSidebarWidth + kPad;
  f.cw = std::max(360, windowWidth - f.cx - kPad);
  f.right = f.cx + f.cw;
  f.title = RECT{f.cx, 24, f.right, 62};
  f.subtitle = RECT{f.cx, 62, f.right, 84};
  return f;
}

}  // namespace vibrium::settings

#endif  // VIBRIUM_SETTINGS_COMMON_H_
