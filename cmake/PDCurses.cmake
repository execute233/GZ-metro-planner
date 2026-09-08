# Build only the reviewed WinCon sources; do not execute upstream demo/GUI builds.
set(GZMP_PDCURSES_SOURCE_DIR "" CACHE PATH "Offline PDCursesMod source directory")
if(NOT GZMP_PDCURSES_SOURCE_DIR)
    include(FetchContent)
    FetchContent_Declare(pdc_source
        GIT_REPOSITORY https://github.com/Bill-Gray/PDCursesMod.git
        GIT_TAG 520adbae06981c8eb9c0222bc582b6435329335e
        SOURCE_SUBDIR gzmp-no-upstream-build
        GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(pdc_source)
    set(GZMP_PDCURSES_SOURCE_DIR "${pdc_source_SOURCE_DIR}")
endif()
file(GLOB pdc_core CONFIGURE_DEPENDS "${GZMP_PDCURSES_SOURCE_DIR}/pdcurses/*.c")
set(pdc_wincon)
foreach(module pdcclip pdcdisp pdcgetsc pdckbd pdcscrn pdcsetsc pdcutil)
    list(APPEND pdc_wincon "${GZMP_PDCURSES_SOURCE_DIR}/wincon/${module}.c")
endforeach()
add_library(gzmp_pdcurses STATIC ${pdc_core} ${pdc_wincon})
set_property(TARGET gzmp_pdcurses PROPERTY C_STANDARD 17)
target_compile_definitions(gzmp_pdcurses PUBLIC PDC_WIDE PDC_FORCE_UTF8)
target_include_directories(gzmp_pdcurses SYSTEM PUBLIC "${GZMP_PDCURSES_SOURCE_DIR}")
target_link_libraries(gzmp_pdcurses PUBLIC winmm)
