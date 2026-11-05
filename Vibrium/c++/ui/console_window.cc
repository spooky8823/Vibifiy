#include "console_window.h"

#include <algorithm>
#include <ctime>
#include <commdlg.h>
#include <fstream>
#include <string>

#include "adblock/adblock_service.h"

namespace vibrium {
namespace {

constexpr wchar_t kConsoleClass[] = L"VibriumConsoleWindow";
constexpr UINT_PTR kRefreshTimer = 1;
constexpr int kSearchId = 4101;
constexpr int kFilterId = 4102;
constexpr int kClearId = 4103;
constexpr int kExportId = 4104;
constexpr COLORREF kBackground = RGB(19, 17, 27);
constexpr COLORREF kText = RGB(235, 231, 244);

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

std::wstring Timestamp(uint64_t milliseconds) {
  const std::time_t seconds = static_cast<std::time_t>(milliseconds / 1000);
  std::tm local{};
  localtime_s(&local, &seconds);
  wchar_t buffer[32]{};
  swprintf_s(buffer, L"%02d:%02d:%02d.%03llu",
             local.tm_hour, local.tm_min, local.tm_sec,
             static_cast<unsigned long long>(milliseconds % 1000));
  return buffer;
}

std::wstring ReadText(HWND control) {
  const int length = GetWindowTextLengthW(control);
  std::wstring text(length + 1, L'\0');
  if (length > 0) GetWindowTextW(control, text.data(), length + 1);
  text.resize(length);
  return text;
}

}  // namespace

ConsoleWindow::~ConsoleWindow() {
  if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
}

bool ConsoleWindow::Create(HWND owner, HINSTANCE instance) {
  owner_ = owner;
  instance_ = instance;

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance_;
  wc.lpszClassName = kConsoleClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = CreateSolidBrush(kBackground);
  if (!RegisterClassExW(&wc) &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    DeleteObject(wc.hbrBackground);
    return false;
  }

  hwnd_ = CreateWindowExW(
      WS_EX_TOOLWINDOW, kConsoleClass, L"Vibrium Settings - Console",
      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1050, 680,
      owner_, nullptr, instance_, this);
  if (!hwnd_) return false;

  CreateControls();
  Layout();
  SetTimer(hwnd_, kRefreshTimer, 500, nullptr);
  Refresh();
  return true;
}

void ConsoleWindow::Show() {
  if (!hwnd_ && !Create(owner_, instance_)) return;
  ShowWindow(hwnd_, SW_SHOW);
  SetForegroundWindow(hwnd_);
  Refresh();
}

void ConsoleWindow::CreateControls() {
  summary_ = CreateWindowExW(0, L"STATIC", L"Loading ad-blocking status...",
      WS_CHILD | WS_VISIBLE, 16, 14, 1000, 28, hwnd_, nullptr, instance_, nullptr);
  search_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
      16, 50, 1000, 28, hwnd_, reinterpret_cast<HMENU>(kSearchId),
      instance_, nullptr);
  SendMessageW(search_, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(L"Filter by URL, source page, or resource type..."));
  filter_ = CreateWindowExW(0, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
      16, 50, 150, 220, hwnd_, reinterpret_cast<HMENU>(kFilterId), instance_, nullptr);
  SendMessageW(filter_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All decisions"));
  SendMessageW(filter_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Blocked"));
  SendMessageW(filter_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Allowed"));
  SendMessageW(filter_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Errors"));
  SendMessageW(filter_, CB_SETCURSEL, 0, 0);
  clear_ = CreateWindowExW(0, L"BUTTON", L"Clear", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      0, 50, 82, 28, hwnd_, reinterpret_cast<HMENU>(kClearId), instance_, nullptr);
  export_ = CreateWindowExW(0, L"BUTTON", L"Export", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      0, 50, 82, 28, hwnd_, reinterpret_cast<HMENU>(kExportId), instance_, nullptr);
  logs_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
      ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
      16, 90, 1000, 520, hwnd_, nullptr, instance_, nullptr);
  SendMessageW(logs_, WM_SETFONT,
      reinterpret_cast<WPARAM>(GetStockObject(ANSI_FIXED_FONT)), TRUE);
}

void ConsoleWindow::Layout() {
  if (!hwnd_) return;
  RECT client{};
  GetClientRect(hwnd_, &client);
  const int width = std::max(100, client.right - 32);
  const int height = std::max(100, client.bottom - 106);
  MoveWindow(summary_, 16, 14, width, 26, TRUE);
  MoveWindow(search_, 16, 48, width, 28, TRUE);
  MoveWindow(logs_, 16, 86, width, height, TRUE);
}

void ConsoleWindow::Refresh() {
  if (!hwnd_ || !IsWindow(hwnd_)) return;
  RenderLogs();
}

void ConsoleWindow::RenderLogs() {
  const auto snapshot = AdblockService::Get().Stats();
  const bool enabled = AdblockService::Get().enabled();
  const std::wstring status = enabled ? L"ENABLED" : L"DISABLED";
  std::wstring summary = L"Ad blocking: " + status +
      L"    Blocked: " + std::to_wstring(snapshot.blocked) +
      L"    Allowed: " + std::to_wstring(snapshot.allowed) +
      L"    Errors: " + std::to_wstring(snapshot.errors) +
      L"    Recent events: " + std::to_wstring(snapshot.events.size());
  SetWindowTextW(summary_, summary.c_str());

  const std::wstring filter = ReadText(search_);
  std::wstring output;
  output.reserve(snapshot.events.size() * 180);

  for (const auto& event : snapshot.events) {
    const wchar_t* decision = L"ALLOWED";
    if (event.decision == AdblockEngine::Decision::kBlock) decision = L"BLOCKED";
    else if (event.decision == AdblockEngine::Decision::kError) decision = L"ERROR";

    const std::wstring url = Utf8ToWide(event.url);
    const std::wstring source = Utf8ToWide(event.source_url);
    const std::wstring type = Utf8ToWide(event.resource_type);
    std::wstring line = L"[" + Timestamp(event.timestamp_ms) + L"] [" +
        decision + L"] [" + type + L"] " + url;
    if (!source.empty()) line += L"  (from " + source + L")";
    line += L"\r\n";

    if (!filter.empty() &&
        line.find(filter) == std::wstring::npos) continue;
    output += line;
  }

  if (output.empty()) {
    output = snapshot.events.empty()
        ? L"No ad-blocking decisions recorded yet. Browse a website to generate events.\r\n"
        : L"No events match the current search.\r\n";
  }
  SetWindowTextW(logs_, output.c_str());
  SendMessageW(logs_, EM_SETSEL, static_cast<WPARAM>(-1),
               static_cast<LPARAM>(-1));
  SendMessageW(logs_, EM_SCROLLCARET, 0, 0);
}

LRESULT CALLBACK ConsoleWindow::WindowProc(HWND hwnd, UINT message,
                                            WPARAM wParam, LPARAM lParam) {
  ConsoleWindow* self = reinterpret_cast<ConsoleWindow*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    self = static_cast<ConsoleWindow*>(create->lpCreateParams);
    self->hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
  return self->HandleMessage(message, wParam, lParam);
}

LRESULT ConsoleWindow::HandleMessage(UINT message, WPARAM wParam,
                                     LPARAM lParam) {
  switch (message) {
    case WM_SIZE:
      Layout();
      return 0;
    case WM_TIMER:
      if (wParam == kRefreshTimer) Refresh();
      return 0;
    case WM_COMMAND:
      if (LOWORD(wParam) == kSearchId && HIWORD(wParam) == EN_CHANGE) {
        Refresh();
        return 0;
      }
      break;
    case WM_CLOSE:
      ShowWindow(hwnd_, SW_HIDE);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd_, kRefreshTimer);
      hwnd_ = nullptr;
      return 0;
  }
  return DefWindowProcW(hwnd_, message, wParam, lParam);
}

}  // namespace vibrium
