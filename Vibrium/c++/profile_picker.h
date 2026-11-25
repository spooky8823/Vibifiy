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

namespace vibrium {

struct ProfileSelection {
    std::wstring name;
    std::filesystem::path path;
};

// Native profile chooser styled after Chrome's "Who's using Chrome?" screen.
//
//  * Main screen: a grid of profile tiles (click to launch, "..." menu on each
//    tile) followed by an "Add profile" tile, plus a settings button.
//  * Settings: Profile (rename / picture / archive / delete), Appearance
//    (picker theme, any 24-bit colour), Data controls (clear data per
//    profile) and Archived.
//
// Profiles live in "<exe folder>\profiles\<name>". Picker preferences are
// stored in "<exe folder>\profiles\picker.ini". The window is per-monitor
// DPI-aware for its own lifetime so text and shapes stay sharp.
class ProfilePicker final {
public:
    static std::optional<ProfileSelection> Show(HINSTANCE instance);

    ProfilePicker() = delete;
};

} // namespace vibrium