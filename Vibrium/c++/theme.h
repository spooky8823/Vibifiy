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

inline COLORREF Accent() { return accent; }
inline bool IsManual() { return manual; }

inline void SetProfileDirectory(const std::filesystem::path& directory) {
  profileDirectory = directory;
}

inline COLORREF ProfileDefaultAccent() {
  if (profileDirectory.empty()) return kDefaultAccent;
  const std::wstring name = profileDirectory.filename().wstring();
  unsigned hash = 2166136261u;
  for (wchar_t ch : name) { hash ^= static_cast<unsigned>(ch); hash *= 16777619u; }
  constexpr COLORREF palette[] = {
    RGB(174, 72, 225), RGB(80, 130, 245), RGB(35, 175, 160),
    RGB(225, 92, 145), RGB(230, 150, 55), RGB(115, 105, 235)
  };
  return palette[hash % (sizeof(palette) / sizeof(palette[0]))];
}

inline std::filesystem::path ProfileThemeFile() {
  return profileDirectory.empty() ? std::filesystem::path{} : profileDirectory / L"theme.ini";
}

inline void SetAccent(COLORREF color) {
  accent = color;
  manual = true;
  const auto profileFile = ProfileThemeFile();
  if (!profileFile.empty()) {
    WritePrivateProfileStringW(L"Appearance", L"Accent", std::to_wstring(static_cast<DWORD>(color)).c_str(), profileFile.c_str());
    WritePrivateProfileStringW(L"Appearance", L"HasAccent", L"1", profileFile.c_str());
    return;
  }
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
  accent = ProfileDefaultAccent();
  manual = false;
  const auto profileFile = ProfileThemeFile();
  if (!profileFile.empty()) {
    WritePrivateProfileStringW(L"Appearance", L"HasAccent", L"0", profileFile.c_str());
    return;
  }
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Vibrium\\Appearance", 0,
      KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
    RegDeleteValueW(key, L"Accent");
    RegCloseKey(key);
  }
}

inline void Load() {
  const auto profileFile = ProfileThemeFile();
  if (!profileFile.empty()) {
    manual = GetPrivateProfileIntW(L"Appearance", L"HasAccent", 0, profileFile.c_str()) != 0;
    accent = manual
        ? static_cast<COLORREF>(GetPrivateProfileIntW(L"Appearance", L"Accent", static_cast<UINT>(kDefaultAccent), profileFile.c_str()))
         : ProfileDefaultAccent();
    return;
  }
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
