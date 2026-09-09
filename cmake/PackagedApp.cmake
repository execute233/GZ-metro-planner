# Non-bundled builds copy the project atlas beside the EXE. Bundles embed it instead.
option(GZMP_RELEASE_BUNDLE "Build a single-file release bundle with the map embedded" OFF)
set(gzmp_atlas "${PROJECT_SOURCE_DIR}/metro.mbtiles")
if(NOT EXISTS "${gzmp_atlas}")
    message(FATAL_ERROR "Project atlas is missing: ${gzmp_atlas}")
endif()

if(GZMP_RELEASE_BUNDLE)
    find_program(GZMP_OBJCOPY objcopy REQUIRED)
    set(gzmp_embed_obj "${CMAKE_CURRENT_BINARY_DIR}/embed/metro_embed.o")
    # Binary symbols derive from the input name; run inside embed/ for a stable name.
    add_custom_command(
        OUTPUT "${gzmp_embed_obj}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/embed"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${gzmp_atlas}" "${CMAKE_CURRENT_BINARY_DIR}/embed/metro.mbtiles"
        COMMAND "${CMAKE_COMMAND}" -E chdir "${CMAKE_CURRENT_BINARY_DIR}/embed"
                "${GZMP_OBJCOPY}" -I binary -O pe-x86-64 -B i386:x86-64
                --rename-section .data=.rodata,contents,alloc,load,readonly,data
                "metro.mbtiles" "metro_embed.o"
        DEPENDS "${gzmp_atlas}"
        VERBATIM)
    set_source_files_properties("${gzmp_embed_obj}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    target_sources(GZ_metro_planner PRIVATE "${gzmp_embed_obj}")
    target_compile_definitions(GZ_metro_planner PRIVATE GZMP_EMBEDDED_DATA)
else()
    # Run on each build, including when only the atlas changed or its copy was deleted.
    add_custom_target(gzmp_copy_atlas
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:GZ_metro_planner>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${gzmp_atlas}" "$<TARGET_FILE_DIR:GZ_metro_planner>/metro.mbtiles"
        VERBATIM)
    add_dependencies(GZ_metro_planner gzmp_copy_atlas)
endif()
