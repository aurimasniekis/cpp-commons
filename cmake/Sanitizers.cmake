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

# commons_enable_tsan(<target>)
#
# Adds ThreadSanitizer flags to <target> when COMMONS_ENABLE_TSAN is ON and the
# toolchain is GCC or Clang. TSan is mutually exclusive with ASan, so it lives
# behind its own option and a separate build dir (see `make sanitize-tsan`). It
# is the key validation for the lock-free atomics, which ASan/UBSan cannot
# prove race-free.
function(commons_enable_tsan target)
    if(NOT COMMONS_ENABLE_TSAN)
        return()
    endif()

    if(MSVC)
        message(STATUS "commons: ThreadSanitizer requested but skipped on MSVC")
        return()
    endif()

    set(_tsan_flags
        -fsanitize=thread
        -fno-omit-frame-pointer
    )

    target_compile_options(${target} PRIVATE ${_tsan_flags})
    target_link_options   (${target} PRIVATE ${_tsan_flags})
endfunction()
