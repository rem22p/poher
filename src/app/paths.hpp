// Where the files that come with the game are.
#pragma once

#include <filesystem>

namespace poher::app {

// The folder the program itself is in. `argv0` is used if the system cannot tell.
std::filesystem::path program_folder(const char* argv0);

// The folder with avatar pictures: "avatars" next to the program; when the
// program is run from the build folder of the source tree, the one in the tree.
std::filesystem::path default_avatar_folder(const char* argv0);

// Where the profile, the settings and the history are kept: the place the
// system gives programs for their data. `argv0` is used if it gives none.
//   Linux: ~/.local/share/poher   macOS: ~/Library/Application Support/POHER   Windows: %APPDATA%\POHER
std::filesystem::path default_data_folder(const char* argv0);

}  // namespace poher::app
