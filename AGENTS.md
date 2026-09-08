# Repository Guidelines

## Project Structure & Module Organization

This is a C23 command-line Guangzhou Metro route planner. `src/main.c` starts the map UI or exports a snapshot. Code is layered under `src/`: `adt/` owns station, line, edge, and array-list data structures; `io/` reads, validates, and transactionally saves SQLite data and map tiles; `algo/` builds the graph and calculates routes; `render/` formats terminal output; and `ui/` handles menus and input. Runtime fixtures live in `data/`. Each module has a matching standalone test in `tests/test_<module>.c`. Design decisions and implementation plans are in `docs/superpowers/`.

## Build, Test, and Development Commands

Use CMake 4.0+, a C23-capable GCC toolchain, and Ninja on Windows:

```powershell
cmake -S . -B cmake-build-debug -G Ninja
cmake --build cmake-build-debug
ctest --test-dir cmake-build-debug --output-on-failure
./cmake-build-debug/GZ_metro_planner.exe data
```

The first command configures the build, the second compiles the application and tests, and the third runs all CTest targets. The executable accepts an optional data-directory argument; omit `data` to use the same default.

## Coding Style & Naming Conventions

Follow the existing C style: four-space indentation, opening braces on the declaration line, and short, single-purpose functions. Use `snake_case` for functions and variables, `PascalCase` for structs and enums, and `UPPER_SNAKE_CASE` for macros/constants. Keep public declarations in `.h` files and implementation details `static` in `.c` files. Preserve the current dependency direction: `adt` → `io`/`algo` → `render`/`ui` → `main`.

All source and text files must be UTF-8 without BOM. Do not truncate Chinese text by byte index; use the repository's UTF-8-aware display helpers. The sole runtime data source is `data/metro.mbtiles` (SQLite, gzmp_schema=2). Keep station, line, edge and tile updates atomic; synchronize schemas with the offline importer and migration tool. Do not reintroduce CSV or a standalone text mode.

## Testing Guidelines

Tests use a lightweight local `CHECK` macro rather than an external framework. Name files `test_<module>.c` and test functions `test_<behavior>`. Add each executable and `add_test` entry to `tests/CMakeLists.txt`. Cover success, invalid input, boundary cases, ownership/cleanup, and UTF-8 round trips. Run the full CTest suite before submitting; no formal coverage threshold is configured.

## Commit & Pull Request Guidelines

History follows Conventional Commit prefixes: `feat:`, `fix:`, and `docs:`. Write an imperative, specific subject, for example `fix: validate station IDs before graph build`. Keep commits focused. Pull requests should explain behavior and rationale, list verification commands, link relevant issues, and include terminal screenshots when rendering or menu output changes. Do not commit generated `cmake-build-*`, `.idea`, or temporary test directories.
