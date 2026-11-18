#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <functional>

enum class BrowserMenuCommand {
    NewTab,
    NewWindow,
    NewIncognitoTab,
    NewIncognitoWindow,
    Passwords,
    History,
    Downloads,
    ZoomIn,
    ZoomOut,
    ZoomReset,
    Print,
    Find,
    Help,
    Settings,
    Exit
};

class BrowserMenu {
public:
    using CommandCallback = std::function<void(BrowserMenuCommand)>;

    static void Show(
        HWND owner,
        const POINT& screenPosition,
        const CommandCallback& callback);
};
