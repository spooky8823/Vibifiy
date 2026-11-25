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

// Fixed-size native profile chooser styled after Chrome's "Who's using
// Chrome?" screen: a grid of profile tiles (click one to launch it) and an
// "Add profile" pill in the footer. Profile folders are stored beside the
// Vibrium executable in "profiles/<profile name>".
class ProfilePicker final {
public:
    static std::optional<ProfileSelection> Show(HINSTANCE instance);

private:
    enum class Hit { None, Tile, Add, Create, Cancel };

    explicit ProfilePicker(HINSTANCE instance);
    ~ProfilePicker();
    ProfilePicker(const ProfilePicker&) = delete;
    ProfilePicker& operator=(const ProfilePicker&) = delete;

    bool Create();
    void AddControls();
    void PopulateProfiles();
    void SortProfiles();

    void ComputeLayout();
    void Paint(HDC target);
    Hit HitTest(POINT pt, int* tileIndex) const;
    void UpdateHover(POINT pt);

    void ClampScroll();
    void EnsureVisible(int index);
    void MoveFocus(int dx, int dy);

    void StartCreate();
    void EndCreate();
    void CreateProfileFromInput();
    void FinishSelection(int index);

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message,
                                      WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND nameEdit_ = nullptr;
    HFONT editFont_ = nullptr;
    HBRUSH editBrush_ = nullptr;

    std::vector<ProfileSelection> profiles_;
    std::optional<ProfileSelection> selected_;

    std::vector<RECT> tileRects_;
    RECT addRect_{};
    RECT editRect_{};
    RECT createRect_{};
    RECT cancelRect_{};

    int hover_ = -1;              // tile under the mouse
    int focus_ = -1;              // tile with keyboard focus
    Hit hoverButton_ = Hit::None; // footer button under the mouse
    bool keyboardFocus_ = false;  // draw the focus ring only after key use
    bool creating_ = false;       // footer is showing the name field
    bool tracking_ = false;       // WM_MOUSELEAVE requested
    int scrollY_ = 0;
};

} // namespace vibrium