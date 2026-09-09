# 打包成一个自包含可执行文件（含地图数据）时使用。
# 用法（在项目根目录）：
#   cmake -S . -B release -G Ninja -DGZMP_RELEASE_BUNDLE=ON
#   cmake --build release
# 产物为 release/GZ_metro_planner.exe。首次运行会在 exe 所在文件夹生成
# metro.mbtiles（默认数据已编译进 exe）。若文件夹只读，则写入 %LOCALAPPDATA%\GZMetroPlanner。
option(GZMP_RELEASE_BUNDLE "Build a single-file release bundle with the map embedded" OFF)

if(GZMP_RELEASE_BUNDLE)
    find_program(GZMP_OBJCOPY objcopy)
    if(NOT GZMP_OBJCOPY)
        message(FATAL_ERROR "objcopy not found; required for the release bundle")
    endif()

    # GNU objcopy derives the _binary_* symbols from the OUTPUT file name and
    # honors the LAST path segment only when that segment contains no directory
    # separator. Emit the object inside embed/ as "metro_embed.o" (output
    # argument is a bare file name) to obtain exactly the _binary_metro_mbtiles_*
    # symbols main.c declares. Rename .data to .rodata so the embedded map does
    # not end up in the writable image.
    set(gzmp_embed_input "${CMAKE_CURRENT_BINARY_DIR}/embed/metro.mbtiles")
    set(gzmp_embed_obj "${CMAKE_CURRENT_BINARY_DIR}/embed/metro_embed.o")
    add_custom_command(
        OUTPUT "${gzmp_embed_obj}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/embed"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/data/metro.mbtiles" "${gzmp_embed_input}"
        COMMAND "${CMAKE_COMMAND}" -E chdir "${CMAKE_CURRENT_BINARY_DIR}/embed"
                "${GZMP_OBJCOPY}" -I binary -O pe-x86-64 -B i386:x86-64
                --rename-section .data=.rodata,contents,alloc,load,readonly,data
                "metro.mbtiles" "metro_embed.o"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/metro.mbtiles"
        VERBATIM)

    set(GZMP_EMBEDDED_TARGET gzmp_embedded_data)
    add_custom_target(${GZMP_EMBEDDED_TARGET} DEPENDS "${gzmp_embed_obj}")

    target_compile_definitions(GZ_metro_planner PRIVATE GZMP_EMBEDDED_DATA)
    target_link_options(GZ_metro_planner PRIVATE "${gzmp_embed_obj}")
    add_dependencies(GZ_metro_planner ${GZMP_EMBEDDED_TARGET})
endif()
