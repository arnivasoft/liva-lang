# GoogleTest.cmake - Fetch and configure GoogleTest

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Suppress warnings from GoogleTest's own compilation
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(gtest PRIVATE -Wno-undef)
    target_compile_options(gtest_main PRIVATE -Wno-undef)
endif()

enable_testing()

# Use CMake's built-in GoogleTest module for gtest_discover_tests
# We save and restore CMAKE_MODULE_PATH to avoid recursive inclusion
set(_saved_module_path ${CMAKE_MODULE_PATH})
list(REMOVE_ITEM CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(GoogleTest)
set(CMAKE_MODULE_PATH ${_saved_module_path})

function(liva_add_test test_name test_source)
    add_executable(${test_name} ${test_source} ${ARGN})
    target_link_libraries(${test_name} PRIVATE GTest::gtest_main)
    target_include_directories(${test_name} PRIVATE
        ${PROJECT_SOURCE_DIR}/include
    )
    # Suppress warnings from GoogleTest headers
    get_target_property(_gtest_inc gtest INTERFACE_INCLUDE_DIRECTORIES)
    if(_gtest_inc)
        target_include_directories(${test_name} SYSTEM PRIVATE ${_gtest_inc})
    endif()
    liva_set_compiler_flags(${test_name})
    # Tests need exceptions for GoogleTest
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${test_name} PRIVATE -fexceptions)
    endif()
    gtest_discover_tests(${test_name})
endfunction()
