# Common settings for Harmonia's own targets: strict warnings, warnings as errors,
# and "src/" as the include root so headers are included as "core/Project.h".

function(harmonia_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(${target} PRIVATE juce::juce_recommended_warning_flags)

    if (MSVC)
        target_compile_options(${target} PRIVATE /WX /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Werror)
    endif()

    set_target_properties(${target} PROPERTIES FOLDER "Harmonia")
endfunction()
