// vibrium_updater.exe - applies a pre-built Vibrium snapshot produced by CI.
//
// The browser cannot overwrite its own files while it runs, so clicking
// "Update Vibrium Browser" starts this helper and the browser exits. The helper:
//
//   1. waits until every process running from the install folder has exited,
//   2. finds the newest CI build (nothing is compiled on this machine),
//   3. downloads it and verifies its SHA-256,
//   4. unpacks it with the Windows built-in tar.exe into <install>\.update,
//   5. swaps the files in, keeping a backup so any failure rolls back,
//   6. starts Vibrium again.
//
// Where the build comes from:
//   * default: the rolling GitHub release "vibrium-latest" that CI refreshes on
//     every successful build of main (public, no sign-in needed);
//   * if the environment variable VIBRIUM_GITHUB_TOKEN is set (a token with
//     read access to Actions on the repo): the newest non-expired Actions
//     artifact of the latest main build. GitHub requires a token for artifacts.
//
// Usage (started by the browser):
//   vibrium_updater.exe --pid <browser pid> --dir "<install dir>" --exe vibrium.exe

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <bcrypt.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../ico_format.h"

namespace {

// ---- Configuration ---------------------------------------------------------
constexpr wchar_t kOwner[] = L"spooky8823";
constexpr wchar_t kRepo[] = L"Vibifiy";
constexpr wchar_t kReleaseTag[] = L"vibrium-latest";
constexpr wchar_t kZipAsset[] = L"Vibrium_snapshot.zip";
constexpr wchar_t kHashAsset[] = L"Vibrium_snapshot.zip.sha256";
constexpr wchar_t kInfoAsset[] = L"BUILD_INFO.txt";
// Must match the artifact name in .github/workflows/vibrium.yml.
constexpr wchar_t kArtifactName[] = L"Vibrium_snapshot2026-10-31";
constexpr wchar_t kArtifactBranch[] = L"main";
constexpr wchar_t kTokenEnv[] = L"VIBRIUM_GITHUB_TOKEN";
constexpr wchar_t kWorkDirName[] = L".update";

constexpr UINT WM_APP_STATUS = WM_APP + 1;    // lParam: new std::wstring*
constexpr UINT WM_APP_PROGRESS = WM_APP + 2;  // wParam: 0..100
constexpr UINT WM_APP_DONE = WM_APP + 3;

struct UpdateError {
  std::wstring message;
};

struct Options {
  DWORD pid = 0;
  std::wstring installDir;
  std::wstring exeName = L"vibrium.exe";
};

HWND g_window = nullptr;
HWND g_label = nullptr;
HWND g_bar = nullptr;
bool g_finished = false;
Options g_options;

// ---- Small utilities -------------------------------------------------------
std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return L"";
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
  return out;
}

std::string WideToUtf8(const std::wstring& s) {
  if (s.empty()) return "";
  const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
  std::string out(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring Lower(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
  return s;
}

std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) ++a;
  while (b > a && isspace((unsigned char)s[b - 1])) --b;
  return s.substr(a, b - a);
}

[[noreturn]] void Fail(const std::wstring& message) { throw UpdateError{message}; }

std::wstring Win32Message(DWORD code) {
  wchar_t* buffer = nullptr;
  FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, code, 0, (LPWSTR)&buffer, 0, nullptr);
  std::wstring text = buffer ? buffer : L"";
  if (buffer) LocalFree(buffer);
  while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) text.pop_back();
  return text + L" (error " + std::to_wstring(code) + L")";
}

std::wstring Join(const std::wstring& a, const std::wstring& b) {
  if (a.empty()) return b;
  if (a.back() == L'\\' || a.back() == L'/') return a + b;
  return a + L"\\" + b;
}

std::wstring Parent(const std::wstring& path) {
  const size_t i = path.find_last_of(L"\\/");
  return i == std::wstring::npos ? L"" : path.substr(0, i);
}

bool PathExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

void EnsureDir(const std::wstring& dir) {
  if (dir.empty() || PathExists(dir)) return;
  EnsureDir(Parent(dir));
  if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
    Fail(L"Could not create folder " + dir + L": " + Win32Message(GetLastError()));
  }
}

void RemoveTree(const std::wstring& path) {
  const DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return;
  if (!(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(path.c_str());
    else DeleteFileW(path.c_str());
    return;
  }
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(Join(path, L"*").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
      RemoveTree(Join(path, fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
  RemoveDirectoryW(path.c_str());
}

// Relative paths of every file under `root`.
void ListFiles(const std::wstring& root, const std::wstring& rel, std::vector<std::wstring>* out) {
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(Join(Join(root, rel), L"*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
    const std::wstring childRel = rel.empty() ? fd.cFileName : Join(rel, fd.cFileName);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ListFiles(root, childRel, out);
    else out->push_back(childRel);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

bool ReadFileBytes(const std::wstring& path, std::string* out) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  GetFileSizeEx(f, &size);
  if (size.QuadPart > (64 << 20)) {  // only used for small text files
    CloseHandle(f);
    return false;
  }
  out->resize((size_t)size.QuadPart);
  DWORD read = 0;
  const BOOL ok = size.QuadPart == 0 || ReadFile(f, out->data(), (DWORD)size.QuadPart, &read, nullptr);
  CloseHandle(f);
  return ok != 0;
}

// "key=value" lines (BUILD_INFO.txt).
std::string KeyValue(const std::string& text, const std::string& key) {
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    const std::string line = Trim(text.substr(pos, end - pos));
    if (line.size() > key.size() && line.compare(0, key.size(), key) == 0 && line[key.size()] == '=') {
      return Trim(line.substr(key.size() + 1));
    }
    pos = end + 1;
  }
  return "";
}

// ---- UI plumbing -----------------------------------------------------------
void SetStatus(const std::wstring& text) {
  if (g_window) PostMessageW(g_window, WM_APP_STATUS, 0, (LPARAM) new std::wstring(text));
}
void SetProgress(int percent) {
  if (g_window) PostMessageW(g_window, WM_APP_PROGRESS, (WPARAM)std::clamp(percent, 0, 100), 0);
}

// ---- Processes -------------------------------------------------------------
bool IsUnder(const std::wstring& path, const std::wstring& dir) {
  std::wstring p = Lower(path), d = Lower(dir);
  if (!d.empty() && d.back() != L'\\') d += L'\\';
  return p.size() > d.size() && p.compare(0, d.size(), d) == 0;
}

std::vector<DWORD> ProcessesRunningFrom(const std::wstring& dir) {
  std::vector<DWORD> pids;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return pids;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  if (Process32FirstW(snap, &pe)) {
    do {
      if (pe.th32ProcessID == GetCurrentProcessId()) continue;
      HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
      if (!p) continue;
      wchar_t buffer[32768];
      DWORD size = (DWORD)(sizeof(buffer) / sizeof(buffer[0]));
      if (QueryFullProcessImageNameW(p, 0, buffer, &size) && IsUnder(buffer, dir)) pids.push_back(pe.th32ProcessID);
      CloseHandle(p);
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return pids;
}

enum class ExitState { Closed, BrowserStillRunning, HelpersStillRunning };

// Waits for the browser, then for helper processes that run from the install
// folder. Never kills anything: the caller decides what to do if they linger.
ExitState WaitForBrowserToExit(const Options& o) {
  if (o.pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, o.pid);
    if (h) {
      const DWORD r = WaitForSingleObject(h, 60000);
      CloseHandle(h);
      if (r != WAIT_OBJECT_0) return ExitState::BrowserStillRunning;
    }
  }
  // CEF helper processes (GPU, renderer...) share the install folder and lock
  // its files; they normally exit moments after the browser process.
  for (int i = 0; i < 150 && !ProcessesRunningFrom(o.installDir).empty(); ++i) Sleep(200);  // up to 30 s
  return ProcessesRunningFrom(o.installDir).empty() ? ExitState::Closed : ExitState::HelpersStillRunning;
}

void Relaunch(const Options& o) {
  const std::wstring exe = Join(o.installDir, o.exeName);
  if (!PathExists(exe)) return;
  std::wstring cmd = L"\"" + exe + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, o.installDir.c_str(), &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
}

// ---- HTTP (WinHTTP) --------------------------------------------------------
struct Internet {
  HINTERNET h = nullptr;
  ~Internet() {
    if (h) WinHttpCloseHandle(h);
  }
};

struct HttpResult {
  DWORD status = 0;
  std::wstring location;
};
using Sink = std::function<void(const BYTE*, DWORD)>;
using ProgressFn = std::function<void(ULONGLONG received, ULONGLONG total)>;

HttpResult HttpGet(const std::wstring& url, const std::wstring& headers, bool followRedirects, const Sink& sink,
                   const ProgressFn& progress = nullptr) {
  wchar_t host[256]{}, path[2048]{}, extra[2048]{};
  URL_COMPONENTSW uc{};
  uc.dwStructSize = sizeof(uc);
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 2048;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) Fail(L"Bad URL: " + url);

  Internet session;
  session.h = WinHttpOpen(L"VibriumUpdater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session.h) Fail(L"Could not start network access: " + Win32Message(GetLastError()));

  Internet connection;
  connection.h = WinHttpConnect(session.h, host, uc.nPort, 0);
  if (!connection.h) Fail(L"Could not connect to " + std::wstring(host) + L": " + Win32Message(GetLastError()));

  const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
  const std::wstring target = std::wstring(path) + extra;
  Internet request;
  request.h = WinHttpOpenRequest(connection.h, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
  if (!request.h) Fail(L"Could not create request: " + Win32Message(GetLastError()));

  DWORD policy = followRedirects ? WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP
                                 : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
  if (!headers.empty()) {
    WinHttpAddRequestHeaders(request.h, headers.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
  }

  if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.h, nullptr)) {
    Fail(L"Network request failed: " + Win32Message(GetLastError()));
  }

  HttpResult result;
  DWORD size = sizeof(DWORD);
  WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &result.status, &size, WINHTTP_NO_HEADER_INDEX);

  if (result.status >= 300 && result.status < 400) {
    wchar_t loc[4096]{};
    DWORD locSize = sizeof(loc);
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &locSize,
                            WINHTTP_NO_HEADER_INDEX)) {
      result.location = loc;
    }
    return result;
  }
  if (result.status != 200) return result;

  ULONGLONG total = 0;
  {
    wchar_t len[64]{};
    DWORD lenSize = sizeof(len);
    if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &lenSize,
                            WINHTTP_NO_HEADER_INDEX)) {
      total = _wcstoui64(len, nullptr, 10);
    }
  }

  ULONGLONG received = 0;
  std::vector<BYTE> buffer(64 * 1024);
  for (;;) {
    DWORD got = 0;
    if (!WinHttpReadData(request.h, buffer.data(), (DWORD)buffer.size(), &got)) {
      Fail(L"Download interrupted: " + Win32Message(GetLastError()));
    }
    if (got == 0) break;
    sink(buffer.data(), got);
    received += got;
    if (progress) progress(received, total);
  }
  return result;
}

std::string HttpGetText(const std::wstring& url, const std::wstring& headers, DWORD* status) {
  std::string body;
  const HttpResult r = HttpGet(url, headers, true, [&](const BYTE* d, DWORD n) { body.append((const char*)d, n); });
  if (status) *status = r.status;
  return r.status == 200 ? body : "";
}

void HttpDownloadToFile(const std::wstring& url, const std::wstring& headers, bool follow, const std::wstring& file,
                        const ProgressFn& progress) {
  HANDLE out = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (out == INVALID_HANDLE_VALUE) Fail(L"Could not write " + file + L": " + Win32Message(GetLastError()));
  bool writeFailed = false;
  HttpResult r;
  try {
    r = HttpGet(url, headers, follow,
                [&](const BYTE* d, DWORD n) {
                  DWORD written = 0;
                  if (!WriteFile(out, d, n, &written, nullptr) || written != n) writeFailed = true;
                },
                progress);
  } catch (...) {
    CloseHandle(out);
    throw;
  }
  CloseHandle(out);
  if (writeFailed) Fail(L"Disk write failed while downloading (is the disk full?).");
  if (r.status != 200) Fail(L"Download failed: server answered HTTP " + std::to_wstring(r.status) + L".");
}

// ---- SHA-256 ---------------------------------------------------------------
std::string Sha256OfFile(const std::wstring& path) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (f == INVALID_HANDLE_VALUE) Fail(L"Could not read " + path);
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  std::string hex;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
      BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
    std::vector<BYTE> buffer(1 << 20);
    DWORD got = 0;
    while (ReadFile(f, buffer.data(), (DWORD)buffer.size(), &got, nullptr) && got) {
      BCryptHashData(hash, buffer.data(), got, 0);
    }
    BYTE digest[32]{};
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
      static const char* kHex = "0123456789abcdef";
      for (BYTE b : digest) {
        hex += kHex[b >> 4];
        hex += kHex[b & 15];
      }
    }
  }
  if (hash) BCryptDestroyHash(hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  CloseHandle(f);
  if (hex.empty()) Fail(L"Could not compute the SHA-256 of the download.");
  return hex;
}

void VerifySha256(const std::wstring& file, const std::string& expectedHex) {
  std::string expected = expectedHex;
  std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char c) { return (char)tolower(c); });
  if (expected.size() != 64) Fail(L"The published checksum is malformed.");
  if (Sha256OfFile(file) != expected) {
    Fail(L"The downloaded file does not match its published SHA-256. It may be corrupt or still being uploaded; "
         L"nothing was changed. Try again in a minute.");
  }
}

// ---- Unzip via the built-in tar.exe (Windows 10 1803+) ----------------------
// Symlinks/junctions in an archive could make later steps read or move files
// from outside the staging folder, so refuse them.
void RejectLinks(const std::wstring& dir) {
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(Join(dir, L"*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  std::vector<std::wstring> subdirs;
  bool bad = false;
  do {
    if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) bad = true;
    else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) subdirs.push_back(Join(dir, fd.cFileName));
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  if (bad) Fail(L"The update archive contains links, which is not allowed. Nothing was changed.");
  for (const auto& d : subdirs) RejectLinks(d);
}

void ExtractZip(const std::wstring& zip, const std::wstring& dest) {
  EnsureDir(dest);
  wchar_t system[MAX_PATH]{};
  GetSystemDirectoryW(system, MAX_PATH);
  const std::wstring tar = Join(system, L"tar.exe");
  if (!PathExists(tar)) Fail(L"Windows' built-in tar.exe was not found (needs Windows 10 version 1803 or later).");
  std::wstring cmd = L"\"" + tar + L"\" -xf \"" + zip + L"\" -C \"" + dest + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(tar.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    Fail(L"Could not start tar.exe: " + Win32Message(GetLastError()));
  }
  const DWORD wait = WaitForSingleObject(pi.hProcess, 10 * 60 * 1000);
  DWORD code = 1;
  if (wait == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
  else TerminateProcess(pi.hProcess, 1);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (code != 0) Fail(L"Unpacking the update failed (tar.exe exit code " + std::to_wstring(code) + L").");
  RejectLinks(dest);
}

// ---- Locating the newest build ----------------------------------------------
std::wstring ReleaseUrl(const wchar_t* asset) {
  return std::wstring(L"https://github.com/") + kOwner + L"/" + kRepo + L"/releases/download/" + kReleaseTag + L"/" +
         asset;
}

std::wstring EnvVar(const wchar_t* name) {
  wchar_t buffer[512]{};
  const DWORD n = GetEnvironmentVariableW(name, buffer, 512);
  return (n > 0 && n < 512) ? std::wstring(buffer) : L"";
}

// Value of "key" in a JSON object text (strings and bare literals only).
std::string JsonValue(const std::string& obj, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  size_t p = obj.find(needle);
  if (p == std::string::npos) return "";
  p = obj.find(':', p + needle.size());
  if (p == std::string::npos) return "";
  ++p;
  while (p < obj.size() && isspace((unsigned char)obj[p])) ++p;
  if (p < obj.size() && obj[p] == '"') {
    const size_t e = obj.find('"', p + 1);
    return e == std::string::npos ? "" : obj.substr(p + 1, e - p - 1);
  }
  size_t e = p;
  while (e < obj.size() && obj[e] != ',' && obj[e] != '}' && !isspace((unsigned char)obj[e])) ++e;
  return obj.substr(p, e - p);
}

// Splits the objects of the "artifacts" array (brace matching, string aware).
std::vector<std::string> ArtifactObjects(const std::string& json) {
  std::vector<std::string> objects;
  size_t p = json.find("\"artifacts\"");
  if (p == std::string::npos) return objects;
  p = json.find('[', p);
  if (p == std::string::npos) return objects;
  int depth = 0;
  size_t start = 0;
  bool inString = false;
  for (size_t i = p + 1; i < json.size(); ++i) {
    const char c = json[i];
    if (inString) {
      if (c == '\\') ++i;
      else if (c == '"') inString = false;
      continue;
    }
    if (c == '"') inString = true;
    else if (c == '{') {
      if (depth++ == 0) start = i;
    } else if (c == '}') {
      if (--depth == 0) objects.push_back(json.substr(start, i - start + 1));
    } else if (c == ']' && depth == 0) break;
  }
  return objects;
}

struct Source {
  std::wstring zipUrl;
  std::wstring zipHeaders;
  bool zipFollowsRedirect = true;
  bool zipIsArtifact = false;     // artifact downloads wrap the build zip in another zip
  std::string expectedSha256;     // may be empty (artifact without a digest)
  std::string commit;             // commit the build was made from
};

Source FindFromRelease() {
  Source s;
  DWORD status = 0;
  const std::string info = HttpGetText(ReleaseUrl(kInfoAsset), L"", &status);
  if (status == 404) {
    Fail(L"No published build was found yet (release \"vibrium-latest\"). Let the CI workflow finish once on main, "
         L"or set the VIBRIUM_GITHUB_TOKEN environment variable to download the workflow artifact instead.");
  }
  if (status != 200) Fail(L"Could not read the build information (HTTP " + std::to_wstring(status) + L").");
  s.commit = KeyValue(info, "commit");
  const std::string hashText = HttpGetText(ReleaseUrl(kHashAsset), L"", &status);
  if (status != 200) Fail(L"Could not read the build checksum (HTTP " + std::to_wstring(status) + L").");
  s.expectedSha256 = Trim(hashText).substr(0, 64);
  s.zipUrl = ReleaseUrl(kZipAsset);
  return s;
}

Source FindFromArtifacts(const std::wstring& token) {
  const std::wstring headers = L"Authorization: Bearer " + token +
                               L"\r\nAccept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
  const std::wstring listUrl = std::wstring(L"https://api.github.com/repos/") + kOwner + L"/" + kRepo +
                               L"/actions/artifacts?per_page=50&name=" + kArtifactName;
  DWORD status = 0;
  const std::string json = HttpGetText(listUrl, headers, &status);
  if (status == 401 || status == 403) Fail(L"GitHub rejected VIBRIUM_GITHUB_TOKEN (HTTP " + std::to_wstring(status) + L").");
  if (status != 200) Fail(L"Could not list workflow artifacts (HTTP " + std::to_wstring(status) + L").");
  for (const std::string& obj : ArtifactObjects(json)) {  // newest first
    if (JsonValue(obj, "expired") != "false") continue;
    if (JsonValue(obj, "head_branch") != WideToUtf8(kArtifactBranch)) continue;
    const std::string url = JsonValue(obj, "archive_download_url");
    if (url.empty()) continue;
    Source s;
    s.zipUrl = Utf8ToWide(url);
    s.zipHeaders = headers;
    s.zipFollowsRedirect = false;  // do not send the token to the storage host
    s.zipIsArtifact = true;
    s.commit = JsonValue(obj, "head_sha");
    const std::string digest = JsonValue(obj, "digest");  // "sha256:<hex>"
    if (digest.rfind("sha256:", 0) == 0) s.expectedSha256 = digest.substr(7);
    return s;
  }
  Fail(L"No non-expired workflow artifact was found for branch main (artifacts expire after 30 days).");
}

// ---- Applying ---------------------------------------------------------------
void MoveWithRetry(const std::wstring& from, const std::wstring& to) {
  DWORD err = 0;
  for (int i = 0; i < 20; ++i) {
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) return;
    err = GetLastError();
    Sleep(250);  // antivirus scanners briefly hold freshly written files
  }
  Fail(L"Could not move " + from + L" to " + to + L": " + Win32Message(err));
}

struct Change {
  std::wstring dest, backup;
  bool hadOld = false, placed = false;
};

void Rollback(std::vector<Change>& changes) {
  for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
    if (it->placed) DeleteFileW(it->dest.c_str());
    if (it->hadOld) MoveFileExW(it->backup.c_str(), it->dest.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);
  }
}

void ApplyStaging(const std::wstring& staging, const std::wstring& installDir, const std::wstring& backupDir) {
  std::vector<std::wstring> files;
  ListFiles(staging, L"", &files);
  std::vector<Change> changes;
  changes.reserve(files.size());
  try {
    size_t index = 0;
    for (const std::wstring& rel : files) {
      Change c;
      c.dest = Join(installDir, rel);
      const std::wstring src = Join(staging, rel);
      EnsureDir(Parent(c.dest));
      if (PathExists(c.dest)) {
        c.backup = Join(backupDir, rel);
        EnsureDir(Parent(c.backup));
        MoveWithRetry(c.dest, c.backup);
        c.hadOld = true;
      }
      changes.push_back(c);
      MoveWithRetry(src, c.dest);
      changes.back().placed = true;
      SetProgress(75 + (int)(25 * ++index / files.size()));
    }
  } catch (...) {
    Rollback(changes);
    throw;
  }
}

// ---- The update itself ------------------------------------------------------
// Returns true when new files were installed, false when already up to date.
bool RunUpdate(const Options& o) {
  const std::wstring work = Join(o.installDir, kWorkDirName);
  RemoveTree(work);
  EnsureDir(work);

  SetStatus(L"Checking for the latest build...");
  const std::wstring token = EnvVar(kTokenEnv);
  const Source source = token.empty() ? FindFromRelease() : FindFromArtifacts(token);

  std::string installedInfo;
  ReadFileBytes(Join(o.installDir, L"BUILD_INFO.txt"), &installedInfo);
  const std::string installedCommit = KeyValue(installedInfo, "commit");
  if (!source.commit.empty() && source.commit == installedCommit) return false;

  SetStatus(L"Downloading the latest Vibrium build...");
  const std::wstring zip = Join(work, L"download.zip");
  HttpResult redirect;
  std::wstring url = source.zipUrl;
  if (!source.zipFollowsRedirect) {
    // First hop (authenticated) only yields the storage URL.
    redirect = HttpGet(url, source.zipHeaders, false, [](const BYTE*, DWORD) {});
    if (redirect.status < 300 || redirect.status >= 400 || redirect.location.empty()) {
      Fail(L"GitHub did not provide a download location (HTTP " + std::to_wstring(redirect.status) + L").");
    }
    url = redirect.location;
  }
  HttpDownloadToFile(url, L"", true, zip, [](ULONGLONG got, ULONGLONG total) {
    if (total) SetProgress((int)(60 * got / total));
  });

  if (!source.expectedSha256.empty()) {
    SetStatus(L"Verifying download...");
    VerifySha256(zip, source.expectedSha256);
  }

  SetStatus(L"Unpacking...");
  SetProgress(62);
  const std::wstring staging = Join(work, L"staging");
  ExtractZip(zip, staging);
  if (source.zipIsArtifact) {
    // actions/upload-artifact wrapped our zip in its own zip.
    std::vector<std::wstring> outer;
    ListFiles(staging, L"", &outer);
    std::wstring inner;
    for (const auto& f : outer) {
      if (Lower(f).size() > 4 && Lower(f).substr(Lower(f).size() - 4) == L".zip") inner = f;
    }
    if (inner.empty()) Fail(L"The workflow artifact did not contain a build zip.");
    const std::wstring innerStaging = Join(work, L"staging2");
    ExtractZip(Join(staging, inner), innerStaging);
    RemoveTree(staging);
    MoveWithRetry(innerStaging, staging);
  }
  SetProgress(74);

  if (!PathExists(Join(staging, o.exeName))) {
    Fail(L"The downloaded build does not contain " + o.exeName + L"; nothing was changed.");
  }

  SetStatus(L"Installing...");
  ApplyStaging(staging, o.installDir, Join(work, L"backup"));
  SetProgress(100);
  return true;
}

DWORD WINAPI Worker(LPVOID) {
  const Options o = g_options;
  std::wstring failure;
  bool browserGone = true;  // false: do not relaunch (something is still running)
  bool updated = false;
  try {
    SetStatus(L"Waiting for Vibrium to close...");
    for (;;) {
      const ExitState state = WaitForBrowserToExit(o);
      if (state == ExitState::Closed) break;
      if (state == ExitState::BrowserStillRunning) {
        browserGone = false;
        Fail(L"Vibrium did not close in time. Close it and run the update again.");
      }
      const int choice = MessageBoxW(
          g_window,
          L"Other Vibrium processes are still running from this folder (for example another Vibrium window).\n\n"
          L"Close them, then choose Retry. Cancel leaves everything unchanged.",
          L"Vibrium update", MB_RETRYCANCEL | MB_ICONWARNING | MB_TOPMOST);
      if (choice != IDRETRY) {
        browserGone = false;
        Fail(L"Update cancelled. Nothing was changed.");
      }
      SetStatus(L"Waiting for Vibrium to close...");
    }
    updated = RunUpdate(o);
  } catch (const UpdateError& e) {
    failure = e.message;
  } catch (...) {
    failure = L"An unexpected error occurred.";
  }
  RemoveTree(Join(o.installDir, kWorkDirName));

  if (browserGone) {
    SetStatus(failure.empty() ? (updated ? L"Updated. Starting Vibrium..." : L"Already up to date. Starting Vibrium...")
                              : L"Update failed. Starting your previous version...");
    Relaunch(o);
  }
  if (!failure.empty()) {
    MessageBoxW(nullptr, (failure + L"\n\nYour existing Vibrium version was left in place.").c_str(),
                L"Vibrium update failed", MB_OK | MB_ICONERROR | MB_TOPMOST);
  }
  PostMessageW(g_window, WM_APP_DONE, 0, 0);
  return 0;
}

// ---- Window -----------------------------------------------------------------
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_APP_STATUS: {
      std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));
      if (g_label) SetWindowTextW(g_label, text->c_str());
      return 0;
    }
    case WM_APP_PROGRESS:
      if (g_bar) SendMessageW(g_bar, PBM_SETPOS, wp, 0);
      return 0;
    case WM_APP_DONE:
      g_finished = true;
      DestroyWindow(hwnd);
      return 0;
    case WM_CLOSE:
      if (!g_finished) return 0;  // never abandon a half-applied update
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

bool ParseArgs(Options* o) {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return false;
  for (int i = 1; i < argc; ++i) {
    const std::wstring a = argv[i];
    if (a == L"--pid" && i + 1 < argc) o->pid = (DWORD)wcstoul(argv[++i], nullptr, 10);
    else if (a == L"--dir" && i + 1 < argc) o->installDir = argv[++i];
    else if (a == L"--exe" && i + 1 < argc) o->exeName = argv[++i];
  }
  LocalFree(argv);
  while (o->installDir.size() > 3 && (o->installDir.back() == L'\\' || o->installDir.back() == L'/')) o->installDir.pop_back();
  return !o->installDir.empty() && o->exeName.find_first_of(L"\\/") == std::wstring::npos;
}

enum class TempCopy { NotNeeded, Started, Failed };

// Running from inside the install folder would lock this exe against its own
// update, so continue from a temporary copy.
TempCopy RelaunchFromTemp(const Options& o) {
  wchar_t self[MAX_PATH]{};
  GetModuleFileNameW(nullptr, self, MAX_PATH);
  if (!IsUnder(self, o.installDir)) return TempCopy::NotNeeded;
  wchar_t temp[MAX_PATH]{};
  GetTempPathW(MAX_PATH, temp);
  const std::wstring dir = Join(temp, L"vibrium-updater-" + std::to_wstring(GetCurrentProcessId()));
  CreateDirectoryW(dir.c_str(), nullptr);
  const std::wstring copy = Join(dir, L"vibrium_updater.exe");
  if (!CopyFileW(self, copy.c_str(), FALSE)) return TempCopy::Failed;
  std::wstring cmd = L"\"" + copy + L"\" --pid " + std::to_wstring(o.pid) + L" --dir \"" + o.installDir +
                     L"\" --exe \"" + o.exeName + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(copy.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
    return TempCopy::Failed;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return TempCopy::Started;
}


// ---- Icon patch mode -------------------------------------------------------
// Started by the browser when it exits after the accent colour changed:
//   vibrium_updater.exe --patch-icon "<vibrium.exe>" "<accent.ico>" --pid <pid> --accent <COLORREF>
// A running exe cannot be modified, so this waits for the browser, replaces the
// RT_GROUP_ICON / RT_ICON resources inside vibrium.exe with the accent-coloured
// icon and records what it did so the browser does not repeat it.
constexpr int kAppIconGroupId = 101;  // IDI_VIBRIUM in c++/resource.h
constexpr WORD kFirstIconId = 1000;   // RT_ICON ids for the new images

struct PatchArgs {
  std::wstring exe;
  std::wstring ico;
  DWORD pid = 0;
  DWORD accent = 0;
};

bool ParsePatchArgs(PatchArgs* a) {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) return false;
  bool found = false;
  for (int i = 1; i < argc; ++i) {
    const std::wstring k = argv[i];
    if (k == L"--patch-icon" && i + 2 < argc) {
      a->exe = argv[++i];
      a->ico = argv[++i];
      found = true;
    } else if (k == L"--pid" && i + 1 < argc) {
      a->pid = (DWORD)wcstoul(argv[++i], nullptr, 10);
    } else if (k == L"--accent" && i + 1 < argc) {
      a->accent = (DWORD)wcstoul(argv[++i], nullptr, 10);
    }
  }
  LocalFree(argv);
  return found && !a->exe.empty() && !a->ico.empty();
}

bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>* out) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER size{};
  bool ok = GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (64ll << 20);
  if (ok) {
    out->resize((size_t)size.QuadPart);
    DWORD got = 0;
    ok = ReadFile(f, out->data(), (DWORD)out->size(), &got, nullptr) && got == out->size();
  }
  CloseHandle(f);
  return ok;
}

BOOL CALLBACK FirstLanguage(HMODULE, LPCWSTR, LPCWSTR, WORD lang, LONG_PTR param) {
  *reinterpret_cast<WORD*>(param) = lang;
  return FALSE;  // the first one is enough
}

// Reads the language and icon ids of the exe's current app icon so they can be
// replaced in place instead of leaving a second, stale icon behind.
void ReadExistingIcon(const std::wstring& exe, WORD* lang, std::vector<uint16_t>* oldIds) {
  *lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
  HMODULE module = LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
  if (!module) return;
  WORD found = 0;
  // FirstLanguage stops the enumeration on purpose, so the return value is ignored.
  EnumResourceLanguagesW(module, RT_GROUP_ICON, MAKEINTRESOURCEW(kAppIconGroupId), FirstLanguage,
                         reinterpret_cast<LONG_PTR>(&found));
  *lang = found;
  if (HRSRC res = FindResourceExW(module, RT_GROUP_ICON, MAKEINTRESOURCEW(kAppIconGroupId), found)) {
    if (HGLOBAL mem = LoadResource(module, res)) {
      if (const void* data = LockResource(mem)) {
        vibrium::ico::ParseGroupIconIds(static_cast<const uint8_t*>(data), SizeofResource(module, res), *oldIds);
      }
    }
  }
  FreeLibrary(module);
}

void RecordPatch(const std::wstring& exe, DWORD accent) {
  WIN32_FILE_ATTRIBUTE_DATA d{};
  if (!GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &d)) return;
  const unsigned long long stamp =
      ((unsigned long long)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Vibrium\\Appearance", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                      nullptr) != ERROR_SUCCESS) {
    return;
  }
  RegSetValueExW(key, L"ExeIconAccent", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&accent), sizeof(accent));
  RegSetValueExW(key, L"ExeIconStamp", 0, REG_QWORD, reinterpret_cast<const BYTE*>(&stamp), sizeof(stamp));
  RegCloseKey(key);
}

int RunIconPatch(const PatchArgs& a) {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\VibriumIconPatch");
  if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

  const size_t slash = a.exe.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return 2;
  Options o;
  o.pid = a.pid;
  o.installDir = a.exe.substr(0, slash);
  // If the user already started Vibrium again, leave the exe alone; the next
  // exit tries once more.
  if (WaitForBrowserToExit(o) != ExitState::Closed) return 1;

  std::vector<uint8_t> file;
  std::vector<vibrium::ico::Image> images;
  if (!ReadWholeFile(a.ico, &file) || !vibrium::ico::ParseIco(file.data(), file.size(), images)) return 3;

  WORD lang = 0;
  std::vector<uint16_t> oldIds;
  ReadExistingIcon(a.exe, &lang, &oldIds);

  HANDLE update = BeginUpdateResourceW(a.exe.c_str(), FALSE);
  if (!update) return 4;
  bool ok = true;
  for (uint16_t id : oldIds) ok = ok && UpdateResourceW(update, RT_ICON, MAKEINTRESOURCEW(id), lang, nullptr, 0);
  for (size_t i = 0; ok && i < images.size(); ++i) {
    ok = UpdateResourceW(update, RT_ICON, MAKEINTRESOURCEW(kFirstIconId + i), lang, images[i].data.data(),
                         (DWORD)images[i].data.size());
  }
  const std::vector<uint8_t> group = vibrium::ico::BuildGroupIcon(images, kFirstIconId);
  ok = ok && UpdateResourceW(update, RT_GROUP_ICON, MAKEINTRESOURCEW(kAppIconGroupId), lang,
                             const_cast<uint8_t*>(group.data()), (DWORD)group.size());
  // FALSE writes the changes; TRUE discards them, so a failure leaves the exe untouched.
  if (!EndUpdateResourceW(update, ok ? FALSE : TRUE) || !ok) return 5;

  RecordPatch(a.exe, a.accent);
  SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, a.exe.c_str(), nullptr);
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  // Headless mode used by the browser to refresh the icon inside vibrium.exe.
  PatchArgs patchArgs;
  if (ParsePatchArgs(&patchArgs)) return RunIconPatch(patchArgs);

  if (!ParseArgs(&g_options)) {
    MessageBoxW(nullptr, L"This helper is started by Vibrium (Settings > Updates).", L"Vibrium updater",
                MB_OK | MB_ICONINFORMATION);
    return 2;
  }
  switch (RelaunchFromTemp(g_options)) {
    case TempCopy::Started:
      return 0;
    case TempCopy::Failed:
      // The browser already closed itself: give the user their old version back.
      if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, g_options.pid)) {
        WaitForSingleObject(h, 30000);
        CloseHandle(h);
      }
      Relaunch(g_options);
      MessageBoxW(nullptr,
                  L"The updater could not prepare itself in the temporary folder, so nothing was changed.\n\n"
                  L"Your existing Vibrium was started again.",
                  L"Vibrium update failed", MB_OK | MB_ICONERROR | MB_TOPMOST);
      return 1;
    case TempCopy::NotNeeded:
      break;
  }

  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\VibriumUpdater");
  if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS};
  InitCommonControlsEx(&icc);

  WNDCLASSW wc{};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = L"VibriumUpdaterWindow";
  RegisterClassW(&wc);

  const int width = 440, height = 150;
  g_window = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Updating Vibrium", WS_CAPTION | WS_VISIBLE,
                             (GetSystemMetrics(SM_CXSCREEN) - width) / 2, (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
                             width, height, nullptr, nullptr, instance, nullptr);
  if (!g_window) return 1;
  HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  g_label = CreateWindowExW(0, L"STATIC", L"Starting...", WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 18, width - 56, 22,
                            g_window, nullptr, instance, nullptr);
  SendMessageW(g_label, WM_SETFONT, (WPARAM)font, TRUE);
  g_bar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 20, 52, width - 56, 18,
                          g_window, nullptr, instance, nullptr);
  SendMessageW(g_bar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

  HANDLE worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
  if (worker) CloseHandle(worker);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}