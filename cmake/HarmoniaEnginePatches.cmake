# Fixes to Tracktion Engine that its official repository doesn't have yet, kept as patches in
# external/patches/tracktion_engine (applied in name order) and put into the submodule when the
# build is configured. A patch already in is left alone. One that goes in neither way means the
# engine changed: bring the patch up to date, or drop it if the engine has the fix now.

find_package(Git REQUIRED)

file(GLOB harmonia_engine_patches CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/external/patches/tracktion_engine/*.patch")
list(SORT harmonia_engine_patches)

foreach(patch IN LISTS harmonia_engine_patches)
    get_filename_component(patch_name "${patch}" NAME)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")

    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${patch}"
                    WORKING_DIRECTORY "${HARMONIA_TE_DIR}"
                    RESULT_VARIABLE not_applied OUTPUT_QUIET ERROR_QUIET)

    if (NOT not_applied)
        continue()
    endif()

    execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${patch}"
                    WORKING_DIRECTORY "${HARMONIA_TE_DIR}"
                    RESULT_VARIABLE failed ERROR_VARIABLE error)

    if (failed)
        message(FATAL_ERROR "Tracktion Engine patch ${patch_name} doesn't apply:\n${error}\n"
            "If the patch itself was changed, put the engine back as it was first:\n"
            "  git -C external/tracktion_engine checkout -- .")
    endif()

    message(STATUS "Applied Tracktion Engine patch ${patch_name}")
endforeach()
