#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace vibrium::Theme {
inline constexpr COLORREF kDefaultAccent = RGB(0, 120, 215);
inline COLORREF accent = kDefaultAccent;
inline bool manual = false;

inline COLORREF Accent() { return accent; }
inline bool IsManual() { return manual; }

inline void SetAccent(COLORREF color) {
  accent = color;
  manual = true;
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Vibrium\\Appearance", 0,
      nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
    DWORD value = static_cast<DWORD>(color);
    RegSetValueExW(key, L"Accent", 0, REG_DWORD,
      reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
  }
}

inline void ResetAccent() {
  accent = kDefaultAccent;
  manual = false;
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Vibrium\\Appearance", 0,
      KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
    RegDeleteValueW(key, L"Accent");
    RegCloseKey(key);
  }
}

inline void Load() {
  HKEY key = nullptr;
  DWORD value = 0, size = sizeof(value), type = 0;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Vibrium\\Appearance", 0,
      KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
    if (RegQueryValueExW(key, L"Accent", nullptr, &type,
        reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
        type == REG_DWORD) {
      accent = static_cast<COLORREF>(value);
      manual = true;
      RegCloseKey(key);
      return;
    }
    RegCloseKey(key);
  }
  accent = kDefaultAccent;
  manual = false;
}

inline COLORREF Mix(COLORREF a, COLORREF b, int amount) {
  const int inv = 100 - amount;
  return RGB((GetRValue(a)*inv + GetRValue(b)*amount)/100,
             (GetGValue(a)*inv + GetGValue(b)*amount)/100,
             (GetBValue(a)*inv + GetBValue(b)*amount)/100);
}
}
