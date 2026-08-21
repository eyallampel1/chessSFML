# SQLite amalgamation

`sqlite3.c` / `sqlite3.h` / `sqlite3ext.h` are the official SQLite amalgamation as
published on sqlite.org (not vcpkg's copy, which patches in a
`sqlite3-vcpkg-config.h` that marks every entry point `__declspec(dllimport)`),
version **3.50.4**, vendored here so the build needs neither vcpkg nor a
`sqlite3.dll` beside the executable.

SQLite is in the public domain — see <https://sqlite.org/copyright.html>. There
is nothing to redistribute a licence for.

Compiled by `chessSFML.vcxproj` with warnings off (it is generated code) and:

| define | why |
|---|---|
| `SQLITE_OMIT_LOAD_EXTENSION` | the app never loads extensions; drops the loader |
| `SQLITE_DEFAULT_MEMSTATUS=0` | skips allocation bookkeeping we never read |
| `SQLITE_THREADSAFE=1` | bulk PGN analysis runs on a worker thread |
| `SQLITE_DQS=0` | rejects double-quoted string literals, so a typo'd column name is an error instead of a string |

To update: download a newer amalgamation from <https://sqlite.org/download.html>
and replace the three files. No project changes are needed.
