#pragma once
#include <windows.h>

// Keeps the icons Windows shows OUTSIDE the browser window in step with the
// accent colour: the pinned taskbar button, desktop / Start menu shortcuts and
// the icon embedded in vibrium.exe. (The caption icon and the running taskbar
// button are handled by MainWindow::UpdateWindowIcon.)
namespace vibrium::AccentIcon {

// Asks for the shell-visible icons to follow `accent`. Debounced and run on a
// worker thread, so it is safe to call on every colour change. GDI+ must stay
// initialised until Shutdown() returns.
void Request(COLORREF accent);

// Call once while GDI+ is still alive. Finishes pending work and stops the
// worker. When `patchExe` is true and vibrium.exe's embedded icon is out of
// date, also starts vibrium_updater.exe, which rewrites that icon after this
// process has exited (a running exe cannot be modified).
void Shutdown(COLORREF accent, bool patchExe);

}  // namespace vibrium::AccentIcon