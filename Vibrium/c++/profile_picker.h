#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vibrium {

struct ProfileSelection {
    std::wstring name;
    std::filesystem::path path;
};

// Fixed-size native profile chooser. Profile folders are stored beside the
// Vibrium executable in "profiles/<profile name>".
class ProfilePicker final {
public:
    static std::optional<ProfileSelection> Show(HINSTANCE instance);

private:
    explicit ProfilePicker(HINSTANCE instance);
    bool Create();
    void PopulateProfiles();
    void AddControls();
    void SelectCurrent();
    void CreateProfileFromInput();
    void FinishSelection(int index);
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message,
                                      WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND list_ = nullptr;
    HWND nameEdit_ = nullptr;
    HWND createButton_ = nullptr;
    HWND cancelCreateButton_ = nullptr;
    HWND useButton_ = nullptr;
    HWND newButton_ = nullptr;
    std::vector<ProfileSelection> profiles_;
    std::optional<ProfileSelection> selected_;
    bool creating_ = false;
};

} // namespace vibrium
