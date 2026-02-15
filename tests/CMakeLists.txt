# Tests CMakeLists.txt

# Unit tests
liva_add_test(lexer_test
    unit/LexerTest.cpp
)
target_link_libraries(lexer_test PRIVATE liva_lexer)

liva_add_test(parser_test
    unit/ParserTest.cpp
)
target_link_libraries(parser_test PRIVATE liva_parser)

liva_add_test(sema_test
    unit/SemaTest.cpp
)
target_link_libraries(sema_test PRIVATE liva_sema liva_parser)

liva_add_test(type_test
    unit/TypeTest.cpp
)
target_link_libraries(type_test PRIVATE liva_ast)

liva_add_test(ownership_test
    unit/OwnershipTest.cpp
)
target_link_libraries(ownership_test PRIVATE liva_sema liva_parser)

liva_add_test(projectconfig_test
    unit/ProjectConfigTest.cpp
)
target_link_libraries(projectconfig_test PRIVATE liva_driver)
target_compile_definitions(projectconfig_test PRIVATE
    LIVA_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

liva_add_test(lsp_test
    unit/LSPTest.cpp
)
target_link_libraries(lsp_test PRIVATE liva_lsp liva_sema liva_parser)

liva_add_test(repl_test
    unit/REPLTest.cpp
)
target_link_libraries(repl_test PRIVATE liva_repl liva_sema liva_parser)

liva_add_test(dap_test
    unit/DAPTest.cpp
)
target_link_libraries(dap_test PRIVATE liva_dap liva_sema liva_parser)

liva_add_test(macro_test
    unit/MacroTest.cpp
)
target_link_libraries(macro_test PRIVATE liva_macro liva_sema liva_parser)

# Performance regression tests
liva_add_test(benchmark_test
    unit/BenchmarkTest.cpp
)
target_link_libraries(benchmark_test PRIVATE liva_sema liva_parser)

# Integration tests (file-based + inline)
liva_add_test(integration_test
    unit/IntegrationTest.cpp
)
target_link_libraries(integration_test PRIVATE liva_driver liva_sema liva_parser)
target_compile_definitions(integration_test PRIVATE
    LIVA_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)
if(LIVA_HAS_LLVM AND NOT MINGW)
    target_link_libraries(integration_test PRIVATE liva_codegen liva_irgen)
    target_compile_definitions(integration_test PRIVATE LIVA_HAS_LLVM=1)
endif()

# CodeGen cross-compilation target tests (LLVM only)
if(LIVA_HAS_LLVM AND NOT MINGW)
    liva_add_test(codegen_test
        unit/CodeGenTest.cpp
    )
    target_link_libraries(codegen_test PRIVATE liva_codegen liva_irgen liva_sema liva_parser liva_driver)
    target_compile_definitions(codegen_test PRIVATE LIVA_HAS_LLVM=1)
endif()

# Self-hosting end-to-end tests (only with LLVM backend, not MinGW — MSVC ABI linker needed)
if(LIVA_HAS_LLVM AND NOT MINGW)
    liva_add_test(selfhost_test
        unit/SelfHostTest.cpp
        ${PROJECT_SOURCE_DIR}/src/Driver/CompilerInstance.cpp
    )
    target_link_libraries(selfhost_test PRIVATE
        liva_codegen liva_irgen liva_sema liva_parser liva_driver)
    target_compile_definitions(selfhost_test PRIVATE
        LIVA_HAS_LLVM=1
        LIVA_BUILD_DIR="${CMAKE_BINARY_DIR}")
    add_dependencies(selfhost_test liva_runtime)
endif()

# Fuzz targets (requires Clang with libFuzzer: -DLIVA_ENABLE_FUZZING=ON)
if(LIVA_ENABLE_FUZZING)
    function(liva_add_fuzz_target name source)
        add_executable(${name} ${source})
        target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/include)
        target_compile_features(${name} PRIVATE cxx_std_20)
        target_compile_options(${name} PRIVATE
            -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -g -O1)
        target_link_options(${name} PRIVATE -fsanitize=fuzzer,address,undefined)
    endfunction()

    liva_add_fuzz_target(lexer_fuzz fuzz/LexerFuzz.cpp)
    target_link_libraries(lexer_fuzz PRIVATE liva_lexer)

    liva_add_fuzz_target(parser_fuzz fuzz/ParserFuzz.cpp)
    target_link_libraries(parser_fuzz PRIVATE liva_parser)

    liva_add_fuzz_target(sema_fuzz fuzz/SemaFuzz.cpp)
    target_link_libraries(sema_fuzz PRIVATE liva_sema liva_parser)
endif()

# Benchmark executable (not part of ctest — run manually with ./liva_bench)
add_executable(liva_bench bench/Benchmark.cpp)
target_link_libraries(liva_bench PRIVATE liva_sema liva_parser)
target_include_directories(liva_bench PRIVATE ${PROJECT_SOURCE_DIR}/include)
liva_set_compiler_flags(liva_bench)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(liva_bench PRIVATE -fexceptions -O2)
elseif(MSVC)
    target_compile_options(liva_bench PRIVATE /O2)
endif()
set_target_properties(liva_bench PROPERTIES EXCLUDE_FROM_ALL TRUE)
