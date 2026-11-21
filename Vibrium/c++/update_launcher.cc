#include "update_launcher.h"

#include <windows.h>

#include <cctype>
#include <string>

namespace vibrium {
namespace {

std::wstring SelfPath() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return path;
}

std::wstring Quote(std::wstring value) {
  // A trailing backslash would escape the closing quote.
  if (!value.empty() && value.back() == L'\\') value += L'\\';
  return L"\"" + value + L"\"";
}

}  // namespace

bool StartUpdater(std::wstring* error) {
  const std::wstring self = SelfPath();
  const size_t slash = self.find_last_of(L'\\');
  if (slash == std::wstring::npos) {
    *error = L"Could not determine where Vibrium is installed.";
    return false;
  }
  const std::wstring dir = self.substr(0, slash);
  const std::wstring exe = self.substr(slash + 1);
  const std::wstring updater = dir + L"\\vibrium_updater.exe";

  if (GetFileAttributesW(updater.c_str()) == INVALID_FILE_ATTRIBUTES) {
    *error = L"vibrium_updater.exe is missing next to vibrium.exe. Install a build that includes the updater once, "
             L"then updates work from the button.";
    return false;
  }

  // Fail now, while the browser is still open, if the folder is read-only
  // (for example under Program Files).
  const std::wstring probe = dir + L"\\.update-write-test";
  HANDLE f = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (f == INVALID_HANDLE_VALUE) {
    *error = L"Vibrium's folder is not writable, so it cannot update itself:\n" + dir +
             L"\n\nMove Vibrium to a folder you own (for example under your user profile).";
    return false;
  }
  CloseHandle(f);

  std::wstring cmd = Quote(updater) + L" --pid " + std::to_wstring(GetCurrentProcessId()) + L" --dir " + Quote(dir) +
                     L" --exe " + Quote(exe);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(updater.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
    *error = L"Could not start the updater (error " + std::to_wstring(GetLastError()) + L").";
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

std::wstring InstalledBuildLabel() {
  const std::wstring self = SelfPath();
  const size_t slash = self.find_last_of(L'\\');
  if (slash == std::wstring::npos) return L"unknown";
  HANDLE f = CreateFileW((self.substr(0, slash) + L"\\BUILD_INFO.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return L"unknown (not installed from a CI build yet)";
  char buffer[1024]{};
  DWORD got = 0;
  ReadFile(f, buffer, sizeof(buffer) - 1, &got, nullptr);
  CloseHandle(f);
  const std::string text(buffer, got);
  const size_t at = text.find("commit=");
  if (at == std::string::npos) return L"unknown";
  std::wstring commit;
  for (size_t i = at + 7; i < text.size() && commit.size() < 7 && isxdigit((unsigned char)text[i]); ++i) {
    commit += (wchar_t)text[i];
  }
  return commit.empty() ? L"unknown" : L"commit " + commit;
}

}  // namespace vibrium
