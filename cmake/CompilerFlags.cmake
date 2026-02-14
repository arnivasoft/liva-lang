# CompilerFlags.cmake - Compiler warning and standard settings

function(liva_set_compiler_flags target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /WX-
            /permissive-
            /Zc:__cplusplus
            /utf-8
            /EHsc
        )
        target_compile_definitions(${target} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
            NOMINMAX
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wno-unused-parameter
            -Wno-unused-variable
            -fno-exceptions
        )
        # Sanitizer support (standalone, works with Clang and GCC)
        if(NOT LIVA_SANITIZER STREQUAL "none")
            target_compile_options(${target} PRIVATE
                -fsanitize=${LIVA_SANITIZER} -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=${LIVA_SANITIZER})
        endif()
        # Fuzzing fallback: if fuzzing enabled but no explicit sanitizer, add ASan+UBSan (Clang only)
        if(LIVA_ENABLE_FUZZING AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND LIVA_SANITIZER STREQUAL "none")
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()
