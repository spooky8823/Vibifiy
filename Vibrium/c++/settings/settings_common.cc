#include "settings/settings_common.h"

#include <cwchar>
#include <ctime>

namespace vibrium::settings {

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                       value.data(),
                                       static_cast<int>(value.size()),
                                       nullptr, 0);
  if (size <= 0) return L"[unprintable URL]";
  std::wstring result(size, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), size);
  return result;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

std::wstring Timestamp(uint64_t milliseconds) {
  const std::time_t seconds = static_cast<std::time_t>(milliseconds / 1000);
  std::tm local{};
  localtime_s(&local, &seconds);
  wchar_t buffer[32]{};
  swprintf_s(buffer, L"%02d:%02d:%02d.%03llu", local.tm_hour, local.tm_min,
             local.tm_sec, static_cast<unsigned long long>(milliseconds % 1000));
  return buffer;
}

std::wstring FormatCount(uint64_t value) {
  std::wstring digits = std::to_wstring(value);
  for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) {
    digits.insert(static_cast<size_t>(i), L",");
  }
  return digits;
}

std::wstring ToLower(std::wstring text) {
  if (!text.empty()) {
    CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
  }
  return text;
}

std::wstring Trim(const std::wstring& value) {
  const size_t first = value.find_first_not_of(L" \t\r\n");
  if (first == std::wstring::npos) return L"";
  const size_t last = value.find_last_not_of(L" \t\r\n");
  return value.substr(first, last - first + 1);
}

void FillRound(HDC hdc, const RECT& rect, int radius, COLORREF fill,
               COLORREF border, int borderWidth) {
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = border == CLR_INVALID
                 ? static_cast<HPEN>(GetStockObject(NULL_PEN))
                 : CreatePen(PS_SOLID, borderWidth, border);
  HGDIOBJ oldBrush = SelectObject(hdc, brush);
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
  SelectObject(hdc, oldBrush);
  SelectObject(hdc, oldPen);
  DeleteObject(brush);
  if (border != CLR_INVALID) DeleteObject(pen);
}

void DrawLabel(HDC hdc, const std::wstring& text, RECT rect, HFONT font,
               COLORREF color, UINT flags) {
  HGDIOBJ oldFont = SelectObject(hdc, font);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, color);
  DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rect,
            flags | DT_NOPREFIX);
  SelectObject(hdc, oldFont);
}

int TextWidth(HDC hdc, HFONT font, const std::wstring& text) {
  HGDIOBJ oldFont = SelectObject(hdc, font);
  SIZE size{};
  GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()),
                        &size);
  SelectObject(hdc, oldFont);
  return size.cx;
}

HFONT MakeFont(int pixelHeight, int weight, const wchar_t* face) {
  return CreateFontW(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

void DrawTerminalIcon(HDC hdc, int x, int y, COLORREF color) {
  LOGBRUSH brush{};
  brush.lbStyle = BS_SOLID;
  brush.lbColor = color;
  HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND |
                              PS_JOIN_ROUND,
                          2, &brush, 0, nullptr);
  HGDIOBJ oldPen = SelectObject(hdc, pen);
  const POINT chevron[] = {{x + 3, y + 4}, {x + 9, y + 9}, {x + 3, y + 14}};
  Polyline(hdc, chevron, 3);
  MoveToEx(hdc, x + 11, y + 15, nullptr);
  LineTo(hdc, x + 16, y + 15);
  SelectObject(hdc, oldPen);
  DeleteObject(pen);
}

}  // namespace vibrium::settings
