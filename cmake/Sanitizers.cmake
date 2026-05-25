include_guard(GLOBAL)

# commons_enable_sanitizers(<target>)
#
# Adds AddressSanitizer + UndefinedBehaviorSanitizer flags to <target> when
# COMMONS_ENABLE_SANITIZERS is ON and the toolchain is GCC or Clang.
function(commons_enable_sanitizers target)
    if(NOT COMMONS_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        message(STATUS "commons: sanitizers requested but skipped on MSVC")
        return()
    endif()

    set(_san_flags
        -fsanitize=address
        -fsanitize=undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all
    )

    target_compile_options(${target} PRIVATE ${_san_flags})
    target_link_options   (${target} PRIVATE ${_san_flags})
endfunction()
