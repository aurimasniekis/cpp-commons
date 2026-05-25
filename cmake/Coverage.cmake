include_guard(GLOBAL)

# commons_enable_coverage(<target>)
#
# Adds Clang source-based coverage flags to <target> when
# COMMONS_ENABLE_COVERAGE is ON. No-op on MSVC.
function(commons_enable_coverage target)
    if(NOT COMMONS_ENABLE_COVERAGE)
        return()
    endif()
    if(MSVC)
        message(STATUS "commons: coverage requested but skipped on MSVC")
        return()
    endif()

    set(_cov_flags -fprofile-instr-generate -fcoverage-mapping)
    target_compile_options(${target} PRIVATE ${_cov_flags} -O0 -g)
    target_link_options   (${target} PRIVATE ${_cov_flags})
endfunction()
