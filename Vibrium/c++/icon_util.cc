#include "icon_util.h"

#include <algorithm>
using std::min;
using std::max;
#include <unknwn.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstddef>
#include <cwchar>
#include <iterator>
#include <map>
#include <memory>
#include <string>

namespace vibrium {
namespace {

// <exe folder>\icons\<name>
std::wstring IconPath(const wchar_t* name) {
  wchar_t exe[MAX_PATH * 4]{};
  const DWORD n =
      GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
  if (!n || n >= std::size(exe)) return L"";
  std::wstring path(exe);
  const size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return L"";
  return path.substr(0, slash) + L"\\icons\\" + name;
}

// Loads icons\<name> and recolours it. Results are cached; a failed load is
// cached as nullptr.
Gdiplus::Bitmap* TintedIcon(const wchar_t* name, COLORREF tint) {
  // Intentionally never destroyed: GDI+ objects must not outlive GDI+, and the
  // process is about to exit anyway.
  using Cache = std::map<std::wstring, std::unique_ptr<Gdiplus::Bitmap>>;
  static Cache& cache = *new Cache();

  wchar_t key[128]{};
  swprintf_s(key, L"%ls|%06X", name, static_cast<unsigned>(tint & 0xFFFFFF));
  auto it = cache.find(key);
  if (it != cache.end()) return it->second.get();

  std::unique_ptr<Gdiplus::Bitmap> result;
  EnsureGdiplus();
  const std::wstring path = IconPath(name);
  if (!path.empty()) {
    Gdiplus::Bitmap source(path.c_str());
    if (source.GetLastStatus() == Gdiplus::Ok) {
      const int w = static_cast<int>(source.GetWidth());
      const int h = static_cast<int>(source.GetHeight());
      auto out =
          std::make_unique<Gdiplus::Bitmap>(w, h, PixelFormat32bppARGB);
      Gdiplus::Rect rect(0, 0, w, h);
      Gdiplus::BitmapData sd{}, dd{};
      if (source.LockBits(&rect, Gdiplus::ImageLockModeRead,
                          PixelFormat32bppARGB, &sd) == Gdiplus::Ok) {
        if (out->LockBits(&rect, Gdiplus::ImageLockModeWrite,
                          PixelFormat32bppARGB, &dd) == Gdiplus::Ok) {
          for (int y = 0; y < h; ++y) {
            const BYTE* sp = static_cast<const BYTE*>(sd.Scan0) +
                             static_cast<ptrdiff_t>(y) * sd.Stride;
            BYTE* dp = static_cast<BYTE*>(dd.Scan0) +
                       static_cast<ptrdiff_t>(y) * dd.Stride;
            for (int x = 0; x < w; ++x, sp += 4, dp += 4) {
              const int lum = (sp[2] * 299 + sp[1] * 587 + sp[0] * 114) / 1000;
              dp[0] = GetBValue(tint);
              dp[1] = GetGValue(tint);
              dp[2] = GetRValue(tint);
              dp[3] = static_cast<BYTE>(sp[3] * (255 - lum) / 255);
            }
          }
          out->UnlockBits(&dd);
          result = std::move(out);
        }
        source.UnlockBits(&sd);
      }
    }
  }

  Gdiplus::Bitmap* raw = result.get();
  cache[key] = std::move(result);
  return raw;
}

}  // namespace

void EnsureGdiplus() {
  // Started once and left running until the process exits.
  static ULONG_PTR token = [] {
    ULONG_PTR value = 0;
    Gdiplus::GdiplusStartupInput in;
    Gdiplus::GdiplusStartup(&value, &in, nullptr);
    return value;
  }();
  (void)token;
}

bool DrawTintedIcon(HDC hdc, const wchar_t* name, COLORREF tint, int cx,
                    int cy, int size) {
  Gdiplus::Bitmap* icon = TintedIcon(name, tint);
  if (!icon) return false;
  Gdiplus::Graphics g(hdc);
  g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  g.DrawImage(icon, Gdiplus::Rect(cx - size / 2, cy - size / 2, size, size));
  return true;
}

void DrawHoverCircle(HDC hdc, int cx, int cy, int diameter, int alpha) {
  if (alpha <= 0) return;
  EnsureGdiplus();
  Gdiplus::Graphics g(hdc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  Gdiplus::SolidBrush brush(Gdiplus::Color(
      static_cast<BYTE>(alpha > 255 ? 255 : alpha), 255, 255, 255));
  g.FillEllipse(&brush, cx - diameter / 2, cy - diameter / 2, diameter,
                diameter);
}

}  // namespace vibrium