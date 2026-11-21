// Starts vibrium_updater.exe (shipped next to vibrium.exe by CI) so the browser
// can exit and be replaced by the newest CI build. See c++/updater/.
#ifndef VIBRIUM_UPDATE_LAUNCHER_H_
#define VIBRIUM_UPDATE_LAUNCHER_H_

#include <string>

namespace vibrium {

// Checks that the install folder is writable and the updater exists, then
// launches it. On success the caller must close the browser right away.
// On failure returns false and fills `error` (the browser keeps running).
bool StartUpdater(std::wstring* error);

// Short description of the installed build, e.g. "commit 1a2b3c4".
std::wstring InstalledBuildLabel();

}  // namespace vibrium

#endif  // VIBRIUM_UPDATE_LAUNCHER_H_
