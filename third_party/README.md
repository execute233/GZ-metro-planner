# Runtime C dependencies

- SQLite 3.50.4: https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
  - Archive SHA-256: `1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032`
  - Public domain; original declarations retained in sqlite3.c / sqlite3.h.
  - Static build, single-threaded, dynamic extension loading omitted, database opened read-only.
- zlib 1.3.1: https://zlib.net/fossils/zlib-1.3.1.tar.gz
  - Archive SHA-256: `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23`
  - zlib license retained in zlib/LICENSE. Only inflate and supporting C files are linked.
- PDCursesMod: https://github.com/Bill-Gray/PDCursesMod
  - Fixed commit `520adbae06981c8eb9c0222bc582b6435329335e`, fetched by CMake or supplied via `GZMP_PDCURSES_SOURCE_DIR`.
  - Core/WinCon public-domain declarations remain in upstream files. Only C core and WinCon backend are built.

No MetroShell code was copied. Its public architecture was used as a reference; the bounded MVT decoder and Braille renderer are implemented for this project.
