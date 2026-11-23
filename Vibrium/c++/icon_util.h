// Loads the PNGs shipped in <exe folder>\icons and draws them recoloured.
// The icons are black shapes (some with a white cut-out), so the shape comes
// from the alpha channel and darkness and is filled with the tint colour.
#ifndef VIBRIUM_ICON_UTIL_H_
#define VIBRIUM_ICON_UTIL_H_

#include <windows.h>

namespace vibrium {

// Starts GDI+ once for the process (safe to call repeatedly).
void EnsureGdiplus();

// Draws icons\<name> tinted with `tint`, centred on (cx, cy) and scaled to
// `size` x `size`. Returns false if the file could not be loaded, so the
// caller can draw a fallback.
bool DrawTintedIcon(HDC hdc, const wchar_t* name, COLORREF tint, int cx,
                    int cy, int size);

}  // namespace vibrium

#endif  // VIBRIUM_ICON_UTIL_H_