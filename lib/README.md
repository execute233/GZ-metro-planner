# Vendored C dependencies

These source trees are ordinary repository files, not Git submodules. CMake
builds them locally as static libraries. Configuration and compilation do not
download dependencies or require Python or Git. Build tools (CMake, Ninja,
MinGW GCC and its Windows system libraries) must still be installed.

- SQLite 3.50.4: https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
  - Archive SHA-256: `1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032`
  - Public domain; original declarations retained in sqlite3.c / sqlite3.h.
  - `lib/sqlite/`: unchanged amalgamated C source and header.
  - Static build, single-threaded, dynamic extension loading omitted. Map loading
    is read-only; maintenance writes use SQLite transactions.
- zlib 1.3.1: https://zlib.net/fossils/zlib-1.3.1.tar.gz
  - Archive SHA-256: `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23`
  - `lib/zlib/`: original source and zlib license retained in `zlib/LICENSE`.
  - Inflate, deflate and their supporting C files are linked for tile decoding
    and maintenance writes; upstream examples are not built.
- PDCursesMod: https://github.com/Bill-Gray/PDCursesMod
  - Fixed commit `520adbae06981c8eb9c0222bc582b6435329335e`.
  - Archive: https://codeload.github.com/Bill-Gray/PDCursesMod/zip/520adbae06981c8eb9c0222bc582b6435329335e
  - Archive SHA-256: `b0ee2d29d06c9a71918dc00bd52b32d6e004970acca69302e7679b0f106ecf6e`
  - `lib/pdcursesmod/`: full upstream source snapshot, including per-directory
    distribution notices. The core is public domain; some other parts have
    their own licenses, described in the upstream README files and sources.
  - Only C core and WinCon backend are built, with `PDC_WIDE` and `PDC_FORCE_UTF8`.
    Other included ports do not add SDL, OpenGL or X11 dependencies to this app.
  - Upstream CMake and demo/CI scripts are not invoked by this project's build.
  - Local documentation-only normalization: `psffonts/fntcol16/copyleft.txt`
    was converted from DOS Hebrew (CP862) to UTF-8 without BOM, preserving its
    notice and addresses. The vendored `.gitignore` adds a Makefile exception
    so upstream build recipes are not accidentally omitted from Git.
    All other snapshot files retain the archive bytes.

To update a library, select an explicit upstream version/commit, record its
archive SHA-256 and distribution notices here, and verify a fresh build and
the complete CTest suite. Keep local build integration in `cmake/`.
Python map tooling continues to use `tools/requirements*.txt`; its packages
are not vendored in this directory.

No MetroShell code was copied. Its public architecture was used as a reference; the bounded MVT decoder and Braille renderer are implemented for this project.
