#include "browsermenu.h"

namespace {

constexpr UINT ID_NEW_TAB = 1001;
constexpr UINT ID_NEW_WINDOW = 1002;
constexpr UINT ID_NEW_INCOGNITO_TAB = 1003;
constexpr UINT ID_NEW_INCOGNITO_WINDOW = 1004;
constexpr UINT ID_PASSWORDS = 1005;
constexpr UINT ID_HISTORY = 1006;
constexpr UINT ID_DOWNLOADS = 1007;
constexpr UINT ID_ZOOM_IN = 1008;
constexpr UINT ID_ZOOM_OUT = 1009;
constexpr UINT ID_ZOOM_RESET = 1010;
constexpr UINT ID_PRINT = 1011;
constexpr UINT ID_FIND = 1012;
constexpr UINT ID_HELP = 1013;
constexpr UINT ID_SETTINGS = 1014;
constexpr UINT ID_EXIT = 1015;

void AddItem(
    HMENU menu,
    UINT id,
    const wchar_t* text) {
    AppendMenuW(menu, MF_STRING, id, text);
}

} // namespace

void BrowserMenu::Show(
    HWND owner,
    const POINT& screenPosition,
    const CommandCallback& callback) {

    if (!owner || !callback) {
        return;
    }

    HMENU menu = CreatePopupMenu();
    HMENU zoomMenu = CreatePopupMenu();

    if (!menu || !zoomMenu) {
        if (menu) DestroyMenu(menu);
        if (zoomMenu) DestroyMenu(zoomMenu);
        return;
    }

    AddItem(menu, ID_NEW_TAB, L"New Tab");
    AddItem(menu, ID_NEW_WINDOW, L"New Window");
    AddItem(menu, ID_NEW_INCOGNITO_TAB, L"New Incognito Tab");
    AddItem(menu, ID_NEW_INCOGNITO_WINDOW, L"New Incognito Window");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AddItem(menu, ID_PASSWORDS, L"Passwords");
    AddItem(menu, ID_HISTORY, L"History");
    AddItem(menu, ID_DOWNLOADS, L"Downloads");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AddItem(zoomMenu, ID_ZOOM_IN, L"Zoom In");
    AddItem(zoomMenu, ID_ZOOM_OUT, L"Zoom Out");
    AddItem(zoomMenu, ID_ZOOM_RESET, L"Reset Zoom");

    AppendMenuW(
        menu,
        MF_POPUP,
        reinterpret_cast<UINT_PTR>(zoomMenu),
        L"Zoom");

    AddItem(menu, ID_PRINT, L"Print");
    AddItem(menu, ID_FIND, L"Find");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AddItem(menu, ID_HELP, L"Help");
    AddItem(menu, ID_SETTINGS, L"Settings");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AddItem(menu, ID_EXIT, L"Exit");

    SetForegroundWindow(owner);

    const UINT selected = TrackPopupMenu(
        menu,
        TPM_RETURNCMD |
        TPM_RIGHTBUTTON |
        TPM_NONOTIFY,
        screenPosition.x,
        screenPosition.y,
        0,
        owner,
        nullptr);

    DestroyMenu(menu);

    BrowserMenuCommand command;

    switch (selected) {
    case ID_NEW_TAB:
        command = BrowserMenuCommand::NewTab;
        break;
    case ID_NEW_WINDOW:
        command = BrowserMenuCommand::NewWindow;
        break;
    case ID_NEW_INCOGNITO_TAB:
        command = BrowserMenuCommand::NewIncognitoTab;
        break;
    case ID_NEW_INCOGNITO_WINDOW:
        command = BrowserMenuCommand::NewIncognitoWindow;
        break;
    case ID_PASSWORDS:
        command = BrowserMenuCommand::Passwords;
        break;
    case ID_HISTORY:
        command = BrowserMenuCommand::History;
        break;
    case ID_DOWNLOADS:
        command = BrowserMenuCommand::Downloads;
        break;
    case ID_ZOOM_IN:
        command = BrowserMenuCommand::ZoomIn;
        break;
    case ID_ZOOM_OUT:
        command = BrowserMenuCommand::ZoomOut;
        break;
    case ID_ZOOM_RESET:
        command = BrowserMenuCommand::ZoomReset;
        break;
    case ID_PRINT:
        command = BrowserMenuCommand::Print;
        break;
    case ID_FIND:
        command = BrowserMenuCommand::Find;
        break;
    case ID_HELP:
        command = BrowserMenuCommand::Help;
        break;
    case ID_SETTINGS:
        command = BrowserMenuCommand::Settings;
        break;
    case ID_EXIT:
        command = BrowserMenuCommand::Exit;
        break;
    default:
        return;
    }

    callback(command);
}