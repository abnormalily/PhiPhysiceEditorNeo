#pragma once
// Native folder picker used by the prefab export settings.

#include <string>

namespace ppn {

// Opens the system folder picker. Returns true and fills `path` only when the
// user confirms a folder. On non-Windows platforms this returns false.
bool browseFolderDialog(std::string& path);

} // namespace ppn
