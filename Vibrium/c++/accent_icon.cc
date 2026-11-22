#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "accent_icon.h"

#include <windows.h>
#include <algorithm>
using std::min;
using std::max;
#include <unknwn.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ico_format.h"

namespace vibrium::AccentIcon {
namespace {

namespace fs = std::filesystem;

constexpr wchar_t kRegKey[] = L"Software\\Vibrium\\Appearance";
constexpr wchar_t kRegExeAccent[] = L"ExeIconAccent";
constexpr wchar_t kRegExeStamp[] = L"ExeIconStamp";
constexpr int kSizes[] = {16, 20, 24, 32, 40, 48, 64, 256};

// ---- Tinting: identical to the window icon in main.cc ---------------------
struct Hsv { double h, s, v; };

Hsv RgbToHsv(BYTE r, BYTE g, BYTE b) {
    const double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
    const double mx = std::max({rf, gf, bf}), mn = std::min({rf, gf, bf}), d = mx - mn;
    Hsv out{0.0, mx == 0.0 ? 0.0 : d / mx, mx};
    if (d == 0.0) return out;
    if (mx == rf) out.h = 60.0 * std::fmod((gf - bf) / d, 6.0);
    else if (mx == gf) out.h = 60.0 * ((bf - rf) / d + 2.0);
    else out.h = 60.0 * ((rf - gf) / d + 4.0);
    if (out.h < 0.0) out.h += 360.0;
    return out;
}

void HsvToRgb(Hsv c, BYTE& r, BYTE& g, BYTE& b) {
    const double chroma = c.v * c.s;
    const double x = chroma * (1.0 - std::abs(std::fmod(c.h / 60.0, 2.0) - 1.0));
    const double m = c.v - chroma;
    double rf = 0, gf = 0, bf = 0;
    if (c.h < 60) { rf = chroma; gf = x; }
    else if (c.h < 120) { rf = x; gf = chroma; }
    else if (c.h < 180) { gf = chroma; bf = x; }
    else if (c.h < 240) { gf = x; bf = chroma; }
    else if (c.h < 300) { rf = x; bf = chroma; }
    else { rf = chroma; bf = x; }
    r = static_cast<BYTE>(std::clamp((rf + m) * 255.0, 0.0, 255.0));
    g = static_cast<BYTE>(std::clamp((gf + m) * 255.0, 0.0, 255.0));
    b = static_cast<BYTE>(std::clamp((bf + m) * 255.0, 0.0, 255.0));
}

fs::path ExePath() {
    wchar_t buf[MAX_PATH * 4]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return (n && n < std::size(buf)) ? fs::path(buf) : fs::path();
}

// Renders appicon.png at `size`, tinted to the accent. Fills `bgra` (top-down,
// non-premultiplied) and returns the tinted bitmap so it can be PNG-encoded.
std::unique_ptr<Gdiplus::Bitmap> RenderTinted(Gdiplus::Bitmap& source, int size,
                                              COLORREF accent, std::vector<uint8_t>& bgra) {
    auto image = std::make_unique<Gdiplus::Bitmap>(size, size, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(image.get());
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        if (g.DrawImage(&source, 0, 0, size, size) != Gdiplus::Ok) return nullptr;
    }
    Gdiplus::Rect rect(0, 0, size, size);
    Gdiplus::BitmapData data{};
    if (image->LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
                        PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return nullptr;
    }
    const Hsv target = RgbToHsv(GetRValue(accent), GetGValue(accent), GetBValue(accent));
    bgra.assign(static_cast<size_t>(size) * size * 4, 0);
    for (int y = 0; y < size; ++y) {
        BYTE* row = static_cast<BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
        for (int x = 0; x < size; ++x) {
            BYTE* p = row + x * 4;
            if (p[3]) {
                Hsv original = RgbToHsv(p[2], p[1], p[0]);
                if (!(original.s < 0.075 || original.v < 0.035)) {  // keep neutral detail
                    original.h = target.h;
                    original.s *= target.s;
                    original.v *= 0.72 + 0.28 * target.v;
                    BYTE r = 0, g = 0, b = 0;
                    HsvToRgb(original, r, g, b);
                    p[0] = b; p[1] = g; p[2] = r;
                }
            }
            std::copy(p, p + 4, bgra.begin() + (static_cast<size_t>(y) * size + x) * 4);
        }
    }
    image->UnlockBits(&data);
    return image;
}

bool PngClsid(CLSID* clsid) {
    UINT count = 0, bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || !bytes) return false;
    std::vector<BYTE> buf(bytes);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
    if (Gdiplus::GetImageEncoders(count, bytes, info) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(info[i].MimeType, L"image/png") == 0) { *clsid = info[i].Clsid; return true; }
    }
    return false;
}

bool EncodePng(Gdiplus::Bitmap& bitmap, std::vector<uint8_t>& out) {
    CLSID clsid{};
    if (!PngClsid(&clsid)) return false;
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return false;
    bool ok = false;
    if (bitmap.Save(stream, &clsid, nullptr) == Gdiplus::Ok) {
        STATSTG st{};
        HGLOBAL mem = nullptr;
        if (SUCCEEDED(stream->Stat(&st, STATFLAG_NONAME)) &&
            SUCCEEDED(GetHGlobalFromStream(stream, &mem)) && mem) {
            if (const void* p = GlobalLock(mem)) {
                out.assign(static_cast<const uint8_t*>(p),
                           static_cast<const uint8_t*>(p) + st.cbSize.QuadPart);
                GlobalUnlock(mem);
                ok = !out.empty();
            }
        }
    }
    stream->Release();
    return ok;
}

fs::path IconDir() {
    PWSTR raw = nullptr;
    fs::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) && raw) {
        dir = fs::path(raw) / L"Vibrium" / L"icons";
    }
    CoTaskMemFree(raw);
    return dir;
}

// One file per colour: Explorer caches icons by path, so a new colour needs a
// new path or the old picture would keep showing.
fs::path IcoPathFor(COLORREF accent) {
    wchar_t name[48]{};
    swprintf_s(name, L"vibrium_%02X%02X%02X.ico", GetRValue(accent), GetGValue(accent), GetBValue(accent));
    const fs::path dir = IconDir();
    return dir.empty() ? fs::path() : dir / name;
}

bool WriteAccentIco(COLORREF accent, const fs::path& target) {
    const fs::path png = ExePath().parent_path() / L"icons" / L"appicon.png";
    Gdiplus::Bitmap source(png.c_str());
    if (source.GetLastStatus() != Gdiplus::Ok) return false;

    std::vector<ico::Image> images;
    for (int size : kSizes) {
        std::vector<uint8_t> bgra;
        auto tinted = RenderTinted(source, size, accent, bgra);
        if (!tinted) return false;
        ico::Image im;
        im.width = im.height = size;
        if (size >= 256) {
            if (!EncodePng(*tinted, im.data)) return false;
        } else {
            im.data = ico::MakeDib(size, bgra.data());
        }
        images.push_back(std::move(im));
    }
    const std::vector<uint8_t> bytes = ico::BuildIco(images);

    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    const fs::path tmp = target.wstring() + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool wrote = WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                       written == bytes.size();
    CloseHandle(f);
    if (!wrote || !MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// ---- Shortcuts -------------------------------------------------------------
std::wstring Normalize(std::wstring p) {
    wchar_t expanded[MAX_PATH * 2]{};
    if (ExpandEnvironmentStringsW(p.c_str(), expanded, static_cast<DWORD>(std::size(expanded)))) p = expanded;
    p = fs::path(p).lexically_normal().wstring();
    std::transform(p.begin(), p.end(), p.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return p;
}

// Points the shortcut at `ico` if it launches this exe. True if it was changed.
bool RetargetShortcut(const fs::path& lnk, const std::wstring& exeNorm, const fs::path& ico) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
    IPersistFile* file = nullptr;
    bool changed = false;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file))) &&
        SUCCEEDED(file->Load(lnk.c_str(), STGM_READWRITE))) {
        wchar_t target[MAX_PATH * 2]{};
        if (SUCCEEDED(link->GetPath(target, static_cast<int>(std::size(target)), nullptr, SLGP_RAWPATH)) &&
            Normalize(target) == exeNorm) {
            wchar_t current[MAX_PATH * 2]{};
            int index = 0;
            link->GetIconLocation(current, static_cast<int>(std::size(current)), &index);
            if (!(index == 0 && Normalize(current) == Normalize(ico.wstring()))) {
                if (SUCCEEDED(link->SetIconLocation(ico.c_str(), 0)) &&
                    SUCCEEDED(file->Save(lnk.c_str(), TRUE))) {
                    changed = true;
                }
            }
        }
    }
    if (file) file->Release();
    link->Release();
    return changed;
}

bool RetargetShortcutsIn(const fs::path& dir, int maxDepth, const std::wstring& exeNorm, const fs::path& ico) {
    bool any = false;
    std::error_code ec;
    if (dir.empty() || !fs::exists(dir, ec)) return false;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (it.depth() >= maxDepth) it.disable_recursion_pending();
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        std::wstring ext = it->path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        if (ext != L".lnk") continue;
        if (RetargetShortcut(it->path(), exeNorm, ico)) {
            any = true;
            SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, it->path().c_str(), nullptr);
        }
    }
    return any;
}

fs::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    fs::path p;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw) p = raw;
    CoTaskMemFree(raw);
    return p;
}

void UpdateShortcuts(const fs::path& ico) {
    const std::wstring exeNorm = Normalize(ExePath().wstring());
    const fs::path roaming = KnownFolder(FOLDERID_RoamingAppData);
    const fs::path pins = roaming.empty() ? fs::path()
        : roaming / L"Microsoft" / L"Internet Explorer" / L"Quick Launch" / L"User Pinned";

    bool any = false;
    any |= RetargetShortcutsIn(KnownFolder(FOLDERID_Desktop), 1, exeNorm, ico);
    any |= RetargetShortcutsIn(KnownFolder(FOLDERID_PublicDesktop), 1, exeNorm, ico);
    any |= RetargetShortcutsIn(KnownFolder(FOLDERID_Programs), 2, exeNorm, ico);
    any |= RetargetShortcutsIn(KnownFolder(FOLDERID_CommonPrograms), 2, exeNorm, ico);
    any |= RetargetShortcutsIn(pins.empty() ? pins : pins / L"TaskBar", 0, exeNorm, ico);
    any |= RetargetShortcutsIn(pins.empty() ? pins : pins / L"StartMenu", 0, exeNorm, ico);
    // One broad notification makes Explorer and the taskbar re-read icons.
    if (any) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void RemoveOldIcons(const fs::path& keep) {
    std::error_code ec;
    for (fs::directory_iterator it(keep.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& p = it->path();
        const std::wstring name = p.filename().wstring();
        if (p != keep && name.rfind(L"vibrium_", 0) == 0 && p.extension() == L".ico") {
            std::error_code rec;
            fs::remove(p, rec);   // may fail while Explorer still holds it; harmless
        }
    }
}

void Process(COLORREF accent) {
    const fs::path ico = IcoPathFor(accent);
    if (ico.empty()) return;
    std::error_code ec;
    if (!fs::exists(ico, ec) && !WriteAccentIco(accent, ico)) return;
    UpdateShortcuts(ico);
    RemoveOldIcons(ico);
}

// ---- Worker ----------------------------------------------------------------
class Worker {
public:
    ~Worker() { Stop(); }

    void Request(COLORREF accent) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;
        latest_ = accent;
        pending_ = true;
        if (!thread_.joinable()) thread_ = std::thread([this] { Run(); });
        cv_.notify_all();
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

private:
    void Run() {
        const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            cv_.wait(lock, [this] { return stop_ || pending_; });
            if (!pending_) break;   // stopping with nothing left to do
            // Debounce: colour pickers fire continuously while dragging.
            COLORREF seen = latest_;
            while (!stop_ && cv_.wait_for(lock, std::chrono::milliseconds(800),
                                          [&] { return stop_ || latest_ != seen; })) {
                seen = latest_;
            }
            const COLORREF accent = latest_;
            pending_ = false;
            lock.unlock();
            Process(accent);
            lock.lock();
        }
        lock.unlock();
        if (com) CoUninitialize();
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    COLORREF latest_ = 0;
    bool pending_ = false;
    bool stop_ = false;
};

Worker& GetWorker() {
    static Worker worker;
    return worker;
}

// ---- vibrium.exe icon ------------------------------------------------------
uint64_t ExeStamp(const fs::path& exe) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(exe.c_str(), GetFileExInfoStandard, &d)) return 0;
    return (static_cast<uint64_t>(d.ftLastWriteTime.dwHighDateTime) << 32) | d.ftLastWriteTime.dwLowDateTime;
}

bool ExeIconIsCurrent(COLORREF accent, const fs::path& exe) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD a = 0, as = sizeof(a), at = 0;
    uint64_t stamp = 0;
    DWORD ss = sizeof(stamp), st = 0;
    const bool ok =
        RegQueryValueExW(key, kRegExeAccent, nullptr, &at, reinterpret_cast<BYTE*>(&a), &as) == ERROR_SUCCESS &&
        at == REG_DWORD &&
        RegQueryValueExW(key, kRegExeStamp, nullptr, &st, reinterpret_cast<BYTE*>(&stamp), &ss) == ERROR_SUCCESS &&
        st == REG_QWORD;
    RegCloseKey(key);
    // A new build/update replaces the exe (new timestamp) and its icon.
    return ok && a == static_cast<DWORD>(accent) && stamp == ExeStamp(exe);
}

void StartExePatch(COLORREF accent) {
    const fs::path exe = ExePath();
    const fs::path helper = exe.parent_path() / L"vibrium_updater.exe";
    std::error_code ec;
    if (exe.empty() || !fs::exists(helper, ec) || ExeIconIsCurrent(accent, exe)) return;

    const fs::path ico = IcoPathFor(accent);
    if (ico.empty() || (!fs::exists(ico, ec) && !WriteAccentIco(accent, ico))) return;

    std::wstring cmd = L"\"" + helper.wstring() + L"\" --patch-icon \"" + exe.wstring() + L"\" \"" +
                       ico.wstring() + L"\" --pid " + std::to_wstring(GetCurrentProcessId()) +
                       L" --accent " + std::to_wstring(static_cast<DWORD>(accent));
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(helper.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                       DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr,
                       exe.parent_path().c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

}  // namespace

void Request(COLORREF accent) { GetWorker().Request(accent); }

void Shutdown(COLORREF accent, bool patchExe) {
    GetWorker().Stop();
    if (patchExe) StartExePatch(accent);
}

}  // namespace vibrium::AccentIcon