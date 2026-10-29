include_guard(GLOBAL)

function(db_enable_sanitizers target)
    if(DB_ENABLE_THREAD_SANITIZER AND
       (DB_ENABLE_ADDRESS_SANITIZER OR DB_ENABLE_UNDEFINED_SANITIZER))
        message(FATAL_ERROR "ThreadSanitizer must use its own build configuration")
    endif()

    if(DB_ENABLE_COVERAGE AND
       (DB_ENABLE_ADDRESS_SANITIZER OR
        DB_ENABLE_UNDEFINED_SANITIZER OR
        DB_ENABLE_THREAD_SANITIZER))
        message(FATAL_ERROR "Coverage and sanitizers must use separate build configurations")
    endif()

    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        if(DB_ENABLE_ADDRESS_SANITIZER OR
           DB_ENABLE_UNDEFINED_SANITIZER OR
           DB_ENABLE_THREAD_SANITIZER)
            message(FATAL_ERROR "Sanitizer configuration requires GNU g++")
        endif()
        return()
    endif()

    set(enabled_sanitizers)
    if(DB_ENABLE_ADDRESS_SANITIZER)
        list(APPEND enabled_sanitizers address)
    endif()
    if(DB_ENABLE_UNDEFINED_SANITIZER)
        list(APPEND enabled_sanitizers undefined)
    endif()
    if(DB_ENABLE_THREAD_SANITIZER)
        list(APPEND enabled_sanitizers thread)
    endif()

    if(NOT enabled_sanitizers)
        return()
    endif()

    list(JOIN enabled_sanitizers "," sanitizer_list)
    target_compile_options(
        ${target}
        INTERFACE
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all
            "-fsanitize=${sanitizer_list}"
    )
    target_link_options(${target} INTERFACE "-fsanitize=${sanitizer_list}")
endfunction()
