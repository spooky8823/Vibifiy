#include "profile_picker.h"
#include "theme.h"

#include <algorithm>
#include <cwctype>
#include <system_error>
#include <vector>
#include <dwmapi.h>

namespace vibrium {
namespace {

constexpr wchar_t kPickerClass[] = L"VibriumProfilePicker";
constexpr int kWindowWidth = 620;
constexpr int kWindowHeight = 470;
COLORREF kBackground = RGB(0,0,0);
COLORREF kPanel = RGB(18,18,18);
COLORREF kAccent = RGB(80,80,80);
COLORREF kText = RGB(245,245,245);
COLORREF kMuted = RGB(165,165,165);
constexpr int kProfileListId = 1001;
constexpr int kUseProfileId = 1002;
constexpr int kNewProfileId = 1003;
constexpr int kNameEditId = 1004;
constexpr int kCreateProfileId = 1005;
constexpr int kCancelCreateId = 1006;

std::filesystem::path ProfilesRoot() {
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return {};
    executable.resize(length);
    return std::filesystem::path(executable).parent_path() / L"profiles";
}

bool IsValidProfileName(const std::wstring& name) {
    if (name.empty() || name.size() > 64 || name == L"." ||
        name == L"..") {
        return false;
    }
    if (name.back() == L'.' || name.back() == L' ') return false;
    for (wchar_t ch : name) {
        if (ch < 32 || ch == L'<' || ch == L'>' || ch == L':' ||
            ch == L'"' || ch == L'/' || ch == L'\\' ||
            ch == L'|' || ch == L'?' || ch == L'*') {
            return false;
        }
    }

    std::wstring base = name;
    const auto dot = base.find(L'.');
    if (dot != std::wstring::npos) base.resize(dot);
    std::transform(base.begin(), base.end(), base.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    if (base == L"CON" || base == L"PRN" || base == L"AUX" ||
        base == L"NUL") {
        return false;
    }
    if (base.size() == 4 &&
        (base.compare(0, 3, L"COM") == 0 ||
         base.compare(0, 3, L"LPT") == 0) &&
        base[3] >= L'1' && base[3] <= L'9') {
        return false;
    }
    return true;
}

} // namespace

ProfilePicker::ProfilePicker(HINSTANCE instance) : instance_(instance) {}

std::optional<ProfileSelection> ProfilePicker::Show(HINSTANCE instance) {
    ProfilePicker picker(instance);
    if (!picker.Create()) return std::nullopt;

    ShowWindow(picker.hwnd_, SW_SHOW);
    UpdateWindow(picker.hwnd_);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return picker.selected_;
}

bool ProfilePicker::Create() {
    const bool dark = Theme::IsSystemDark();
    kBackground = Theme::Surface();
    kPanel = Theme::Panel();
    kAccent = dark ? RGB(90,90,90) : RGB(100,100,100);
    kText = Theme::Text();
    kMuted = Theme::MutedText();
    const std::filesystem::path root = ProfilesRoot();
    if (root.empty()) return false;

    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error) {
        MessageBoxW(nullptr, L"Could not create the profiles folder.",
                    L"Vibrium Profiles", MB_OK | MB_ICONERROR);
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ProfilePicker::WindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPickerClass;
    RegisterClassExW(&wc);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - kWindowWidth) / 2;
    const int y = work.top + ((work.bottom - work.top) - kWindowHeight) / 2;

    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW, kPickerClass, L"Choose a Vibrium Profile",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, kWindowWidth, kWindowHeight, nullptr, nullptr, instance_, this);
    if (hwnd_) {
        const COLORREF caption = kBackground;
        DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        const COLORREF captionText = kText;
        DwmSetWindowAttribute(hwnd_, DWMWA_TEXT_COLOR, &captionText, sizeof(captionText));
    }
    return hwnd_ != nullptr;
}

void ProfilePicker::AddControls() {
    const auto make = [&](const wchar_t* klass, const wchar_t* text,
                          DWORD style, int id, int x, int y, int width,
                          int height) {
        HWND control = CreateWindowExW(
            0, klass, text, WS_CHILD | WS_VISIBLE | style,
            x, y, width, height, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_, nullptr);
        SendMessageW(control, WM_SETFONT,
                    reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                    TRUE);
        return control;
    };

    make(L"STATIC", L"Choose a profile to continue", SS_LEFT,
         0, 28, 44, 540, 20);
    list_ = make(L"LISTBOX", L"",
        LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
        kProfileListId, 28, 68, 548, 272);
    SendMessageW(list_, LB_SETITEMHEIGHT, 0, 54);

    newButton_ = make(L"BUTTON", L"+  New Profile",
        BS_OWNERDRAW, kNewProfileId, 28, 360, 170, 38);
    useButton_ = make(L"BUTTON", L"Use Profile",
        BS_OWNERDRAW, kUseProfileId, 422, 360, 154, 38);
    nameEdit_ = make(L"EDIT", L"",
        ES_AUTOHSCROLL | WS_BORDER, kNameEditId, 28, 412, 270, 30);
    createButton_ = make(L"BUTTON", L"Create",
        BS_OWNERDRAW, kCreateProfileId, 310, 412, 92, 30);
    cancelCreateButton_ = make(L"BUTTON", L"Cancel",
        BS_OWNERDRAW, kCancelCreateId, 414, 412, 92, 30);

    ShowWindow(nameEdit_, SW_HIDE);
    ShowWindow(createButton_, SW_HIDE);
    ShowWindow(cancelCreateButton_, SW_HIDE);
    EnableWindow(useButton_, FALSE);
    PopulateProfiles();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void ProfilePicker::PopulateProfiles() {
    profiles_.clear();
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);

    const auto root = ProfilesRoot();
    std::error_code error;
    for (std::filesystem::directory_iterator it(root, error), end;
         !error && it != end; it.increment(error)) {
        if (!it->is_directory(error) || error) continue;
        ProfileSelection profile;
        profile.name = it->path().filename().wstring();
        profile.path = it->path();
        profiles_.push_back(std::move(profile));
    }

    std::sort(profiles_.begin(), profiles_.end(),
              [](const ProfileSelection& a, const ProfileSelection& b) {
        std::wstring left = a.name;
        std::wstring right = b.name;
        std::transform(left.begin(), left.end(), left.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        std::transform(right.begin(), right.end(), right.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        return left < right;
    });

    for (size_t i = 0; i < profiles_.size(); ++i) {
        const LRESULT row = SendMessageW(
            list_, LB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(profiles_[i].name.c_str()));
        if (row != LB_ERR && row != LB_ERRSPACE) {
            SendMessageW(list_, LB_SETITEMDATA, row, static_cast<LPARAM>(i));
        }
    }
    if (!profiles_.empty()) {
        SendMessageW(list_, LB_SETCURSEL, 0, 0);
        EnableWindow(useButton_, TRUE);
    }
}

void ProfilePicker::SelectCurrent() {
    const LRESULT row = SendMessageW(list_, LB_GETCURSEL, 0, 0);
    if (row == LB_ERR) {
        EnableWindow(useButton_, FALSE);
        return;
    }
    const LRESULT index = SendMessageW(list_, LB_GETITEMDATA, row, 0);
    EnableWindow(useButton_, index >= 0 &&
        static_cast<size_t>(index) < profiles_.size());
}

void ProfilePicker::CreateProfileFromInput() {
    const int length = GetWindowTextLengthW(nameEdit_);
    if (length <= 0 || length > 64) {
        MessageBoxW(hwnd_, L"Enter a profile name (1 to 64 characters).",
                    L"Vibrium Profiles", MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring name(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(nameEdit_, name.data(), length + 1);
    name.resize(static_cast<size_t>(length));

    if (!IsValidProfileName(name)) {
        MessageBoxW(hwnd_,
            L"That name contains characters Windows cannot use in a folder.",
            L"Vibrium Profiles", MB_OK | MB_ICONWARNING);
        return;
    }

    const auto path = ProfilesRoot() / name;
    std::error_code error;
    if (!std::filesystem::create_directory(path, error)) {
        MessageBoxW(hwnd_,
            error ? L"Could not create the profile folder."
                  : L"A profile with that name already exists.",
            L"Vibrium Profiles", MB_OK | MB_ICONWARNING);
        return;
    }

    ProfileSelection profile{name, path};
    profiles_.push_back(profile);
    const LRESULT row = SendMessageW(
        list_, LB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(profiles_.back().name.c_str()));
    if (row != LB_ERR && row != LB_ERRSPACE) {
        SendMessageW(list_, LB_SETITEMDATA, row,
                     static_cast<LPARAM>(profiles_.size() - 1));
        SendMessageW(list_, LB_SETCURSEL, row, 0);
    }
    ShowWindow(nameEdit_, SW_HIDE);
    ShowWindow(createButton_, SW_HIDE);
    ShowWindow(cancelCreateButton_, SW_HIDE);
    ShowWindow(newButton_, SW_SHOW);
    EnableWindow(useButton_, TRUE);
    SetFocus(list_);
}

void ProfilePicker::FinishSelection(int index) {
    if (index < 0 || static_cast<size_t>(index) >= profiles_.size()) return;
    selected_ = profiles_[static_cast<size_t>(index)];
    DestroyWindow(hwnd_);
}

LRESULT CALLBACK ProfilePicker::WindowProc(HWND hwnd, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
    ProfilePicker* self = reinterpret_cast<ProfilePicker*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<ProfilePicker*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }

    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_CREATE:
        self->AddControls();
        return 0;

    case WM_ERASEBKGND: {
        RECT rc{}; GetClientRect(hwnd, &rc);
        HBRUSH brush = CreateSolidBrush(kBackground);
        FillRect(reinterpret_cast<HDC>(wParam), &rc, brush);
        DeleteObject(brush);
        return 1;
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, kText); SetBkColor(dc, kPanel);
        static HBRUSH editBrush = CreateSolidBrush(kPanel);
        return reinterpret_cast<LRESULT>(editBrush);
    }

    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (draw->CtlID == kProfileListId) {
            const int index = static_cast<int>(draw->itemData);
            if (draw->itemID == static_cast<UINT>(-1) || index < 0 ||
                static_cast<size_t>(index) >= self->profiles_.size()) return TRUE;
            const bool selected = (draw->itemState & ODS_SELECTED) != 0;
            const COLORREF card = selected ? Theme::RaisedPanel() : kPanel;
            HBRUSH brush = CreateSolidBrush(card);
            RECT r = draw->rcItem; InflateRect(&r, -3, -3);
            FillRect(draw->hDC, &r, brush); DeleteObject(brush);
            if (selected) {
                RECT stripe = r; stripe.right = stripe.left + 4;
                HBRUSH accent = CreateSolidBrush(kAccent);
                FillRect(draw->hDC, &stripe, accent); DeleteObject(accent);
            }
            const auto& name = self->profiles_[static_cast<size_t>(index)].name;
            SetBkMode(draw->hDC, TRANSPARENT); SetTextColor(draw->hDC, kText);
            HFONT font = CreateFontW(19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            HGDIOBJ old = SelectObject(draw->hDC, font);
            RECT text = r; text.left += 22;
            DrawTextW(draw->hDC, name.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            SelectObject(draw->hDC, old); DeleteObject(font);
            return TRUE;
        }
        if (draw->CtlID == kUseProfileId || draw->CtlID == kNewProfileId ||
            draw->CtlID == kCreateProfileId || draw->CtlID == kCancelCreateId) {
            const bool primary = draw->CtlID == kUseProfileId || draw->CtlID == kCreateProfileId;
            const bool disabled = (draw->itemState & ODS_DISABLED) != 0;
            const COLORREF fill = disabled ? kPanel : primary ? kAccent : Theme::RaisedPanel();
            HBRUSH brush = CreateSolidBrush(fill); FillRect(draw->hDC, &draw->rcItem, brush); DeleteObject(brush);
            SetBkMode(draw->hDC, TRANSPARENT); SetTextColor(draw->hDC, disabled ? kMuted : RGB(255,255,255));
            DrawTextW(draw->hDC, (draw->CtlID == kUseProfileId ? L"Launch Profile" :
                draw->CtlID == kNewProfileId ? L"+  New Profile" :
                draw->CtlID == kCreateProfileId ? L"Create" : L"Cancel"), -1,
                &draw->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{}; GetClientRect(hwnd, &rc);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, kMuted);
        RECT header{28, 18, rc.right - 28, 48};
        DrawTextW(dc, L"VIBRIUM  /  PROFILES", -1, &header, DT_SINGLELINE | DT_VCENTER);
        RECT footer{28, 442, rc.right - 28, 462};
        DrawTextW(dc, L"Your bookmarks, history and settings stay separate for each profile.", -1, &footer, DT_SINGLELINE | DT_VCENTER);
        EndPaint(hwnd, &ps); return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kProfileListId:
            if (HIWORD(wParam) == LBN_SELCHANGE) {
                self->SelectCurrent();
            } else if (HIWORD(wParam) == LBN_DBLCLK) {
                const LRESULT row = SendMessageW(
                    self->list_, LB_GETCURSEL, 0, 0);
                const LRESULT index = row == LB_ERR ? LB_ERR :
                    SendMessageW(self->list_, LB_GETITEMDATA, row, 0);
                if (index != LB_ERR) self->FinishSelection(
                    static_cast<int>(index));
            }
            return 0;

        case kUseProfileId: {
            const LRESULT row = SendMessageW(self->list_, LB_GETCURSEL, 0, 0);
            const LRESULT index = row == LB_ERR ? LB_ERR :
                SendMessageW(self->list_, LB_GETITEMDATA, row, 0);
            if (index != LB_ERR) self->FinishSelection(
                static_cast<int>(index));
            return 0;
        }

        case kNewProfileId:
            ShowWindow(self->nameEdit_, SW_SHOW);
            ShowWindow(self->createButton_, SW_SHOW);
            ShowWindow(self->cancelCreateButton_, SW_SHOW);
            ShowWindow(self->newButton_, SW_HIDE);
            SetFocus(self->nameEdit_);
            return 0;

        case kCreateProfileId:
            self->CreateProfileFromInput();
            return 0;

        case kCancelCreateId:
            ShowWindow(self->nameEdit_, SW_HIDE);
            ShowWindow(self->createButton_, SW_HIDE);
            ShowWindow(self->cancelCreateButton_, SW_HIDE);
            ShowWindow(self->newButton_, SW_SHOW);
            SetFocus(self->list_);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace vibrium
