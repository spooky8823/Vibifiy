#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

namespace vibrium::Theme {
inline COLORREF accent = GetSysColor(COLOR_HIGHLIGHT);
inline bool manual = false;

inline void SetSystemAccentColor(COLORREF color) {
  if (!manual) accent = color;
}
inline COLORREF Accent() { return accent; }
inline bool IsManual() { return manual; }
inline void SetManualAccent(COLORREF color) {
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
inline void UseSystemAccent() {
  manual = false;
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Vibrium\\Appearance", 0,
      KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
    RegDeleteValueW(key, L"Accent");
    RegCloseKey(key);
  }
  DWORD color = 0;
  BOOL opaque = FALSE;
  if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque)))
    accent = RGB(GetRValue(color), GetGValue(color), GetBValue(color));
  else
    accent = GetSysColor(COLOR_HIGHLIGHT);
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
  manual = false;
  UseSystemAccent();
}
inline COLORREF Mix(COLORREF a, COLORREF b, int amount) {
  const int inv = 100 - amount;
  return RGB((GetRValue(a)*inv + GetRValue(b)*amount)/100,
             (GetGValue(a)*inv + GetGValue(b)*amount)/100,
             (GetBValue(a)*inv + GetBValue(b)*amount)/100);
}
}
