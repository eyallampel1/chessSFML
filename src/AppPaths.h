#ifndef APP_PATHS_H
#define APP_PATHS_H

#include <string>

// Where the app's data files live.
//
// Everything used to be opened by a path relative to the *working directory*,
// which only worked when the app happened to be started from the repository
// root. From a Start-menu shortcut, a desktop shortcut, or an installed copy,
// the board textures and the engine silently failed to load. These helpers
// resolve against the executable instead, keeping the in-repo layout working
// as a fallback so the debugger still runs from the source tree.
namespace apppaths {

// Directory containing the running executable, without a trailing separator.
const std::string& executableDir();

// Resolves a data file given as a repo-relative path, e.g.
// "src/assets/white_pawn.png" or "engines/stockfish.exe". Tried in order:
//   1. <exe dir>/<relative>              a packaged install
//   2. <relative>                        launched from the project root
//   3. <exe dir>/../../<relative>        build/bin, i.e. running from the IDE
// Returns the first that exists; if none do, returns candidate 1 so the
// caller's own error path reports the location we actually wanted.
std::string resource(const std::string& relative);

} // namespace apppaths

#endif /* APP_PATHS_H */
