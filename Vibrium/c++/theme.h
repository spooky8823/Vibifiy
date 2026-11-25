#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <string>

namespace vibrium::Theme {
inline constexpr COLORREF kDefaultAccent = RGB(0, 120, 215);
inline COLORREF accent = kDefaultAccent;
inline bool manual = false;
inline std::filesystem::path profileDirectory;

inline bool IsSystemDark() {
  HKEY key = nullptr;
  DWORD value = 1, size = sizeof(value), type = 0;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
    const LONG result = RegQueryValueExW(key, L"AppsUseLightTheme", nullptr,
        &type, reinterpret_cast<BYTE*>(&value), &size);
    RegCloseKey(key);
    if (result == ERROR_SUCCESS && type == REG_DWORD) return value == 0;
  }
  const COLORREF window = GetSysColor(COLOR_WINDOW);
  return (GetRValue(window) + GetGValue(window) + GetBValue(window)) < 384;
}
inline COLORREF Surface() { return IsSystemDark() ? RGB(0,0,0) : RGB(245,245,245); }
inline COLORREF Panel() { return IsSystemDark() ? RGB(18,18,18) : RGB(232,232,232); }
inline COLORREF RaisedPanel() { return IsSystemDark() ? RGB(30,30,30) : RGB(255,255,255); }
inline COLORREF Text() { return IsSystemDark() ? RGB(245,245,245) : RGB(28,28,28); }
inline COLORREF MutedText() { return IsSystemDark() ? RGB(165,165,165) : RGB(105,105,105); }
inline COLORREF Accent() { return accent; }
inline bool IsManual() { return manual; }

inline void SetProfileDirectory(const std::filesystem::path& directory) {
  profileDirectory = directory;
}
inline COLORREF ProfileDefaultAccent() {
  return IsSystemDark() ? RGB(0,0,0) : RGB(238,238,238);
}
inline std::filesystem::path ProfileThemeFile() {
  return profileDirectory.empty() ? std::filesystem::path{} : profileDirectory / L"theme.ini";
}
inline void SetAccent(COLORREF color) {
  accent = color; manual = true;
  const auto file = ProfileThemeFile();
  if (!file.empty()) {
    WritePrivateProfileStringW(L"Appearance", L"Accent", std::to_wstring(static_cast<DWORD>(color)).c_str(), file.c_str());
    WritePrivateProfileStringW(L"Appearance", L"HasAccent", L"1", file.c_str());
    return;
  }
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Vibrium\\Appearance", 0,
      nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
    DWORD value = static_cast<DWORD>(color);
    RegSetValueExW(key, L"Accent", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
  }
}
inline void ResetAccent() {
  accent = ProfileDefaultAccent(); manual = false;
  const auto file = ProfileThemeFile();
  if (!file.empty()) {
    WritePrivateProfileStringW(L"Appearance", L"HasAccent", L"0", file.c_str());
    return;
  }
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Vibrium\\Appearance", 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
    RegDeleteValueW(key, L"Accent"); RegCloseKey(key);
  }
}
inline void Load() {
  const auto file = ProfileThemeFile();
  if (!file.empty()) {
    manual = GetPrivateProfileIntW(L"Appearance", L"HasAccent", 0, file.c_str()) != 0;
    accent = manual ? static_cast<COLORREF>(GetPrivateProfileIntW(L"Appearance", L"Accent", static_cast<UINT>(ProfileDefaultAccent()), file.c_str())) : ProfileDefaultAccent();
    return;
  }
  HKEY key = nullptr;
  DWORD value = 0, size = sizeof(value), type = 0;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Vibrium\\Appearance", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
    if (RegQueryValueExW(key, L"Accent", nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS && type == REG_DWORD) {
      accent = static_cast<COLORREF>(value); manual = true; RegCloseKey(key); return;
    }
    RegCloseKey(key);
  }
  accent = ProfileDefaultAccent(); manual = false;
}
inline COLORREF Mix(COLORREF a, COLORREF b, int amount) {
  const int inv = 100 - amount;
  return RGB((GetRValue(a)*inv + GetRValue(b)*amount)/100,
             (GetGValue(a)*inv + GetGValue(b)*amount)/100,
             (GetBValue(a)*inv + GetBValue(b)*amount)/100);
}
}
