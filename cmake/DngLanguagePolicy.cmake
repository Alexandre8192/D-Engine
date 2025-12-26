# D-Engine language policy enforcement helpers
# Applies force-include of LanguagePolicy.hpp and disables exceptions/RTTI per target.

get_filename_component(DNG_LANGUAGE_POLICY_HEADER
    "${PROJECT_SOURCE_DIR}/Source/Core/Policy/LanguagePolicy.hpp"
    REALPATH)

if(NOT EXISTS "${DNG_LANGUAGE_POLICY_HEADER}")
    message(FATAL_ERROR "Language policy header not found at ${DNG_LANGUAGE_POLICY_HEADER}")
endif()

if(MSVC)
    set(DNG_LANGUAGE_POLICY_COMPILE_OPTIONS
        "/FI\"${DNG_LANGUAGE_POLICY_HEADER}\""
        "/EHs-"
        "/GR-"
    )
else()
    set(DNG_LANGUAGE_POLICY_COMPILE_OPTIONS
        "-include"
        "${DNG_LANGUAGE_POLICY_HEADER}"
        "-fno-exceptions"
        "-fno-rtti"
    )
endif()

function(dng_enforce_language_policy target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "dng_enforce_language_policy: target '${target}' does not exist")
    endif()

    target_compile_options(${target} PRIVATE ${DNG_LANGUAGE_POLICY_COMPILE_OPTIONS})
endfunction()
