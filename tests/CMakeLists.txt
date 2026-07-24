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

liva_add_test(iterator_protocol_test
    unit/IteratorProtocolTest.cpp
)
target_link_libraries(iterator_protocol_test PRIVATE liva_sema liva_parser)

liva_add_test(hashable_protocol_test
    unit/HashableProtocolTest.cpp
)
target_link_libraries(hashable_protocol_test PRIVATE liva_sema liva_parser)

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

liva_add_test(plugin_test
    unit/PluginTest.cpp
)
target_link_libraries(plugin_test PRIVATE liva_plugin liva_sema liva_parser)

liva_add_test(ui_module_test
    unit/UIModuleTest.cpp
)
target_link_libraries(ui_module_test PRIVATE liva_sema liva_parser)
target_compile_definitions(ui_module_test PRIVATE
    LIVA_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

liva_add_test(stdlib_module_test
    unit/StdlibModuleTest.cpp
)
target_link_libraries(stdlib_module_test PRIVATE liva_sema liva_parser)
target_compile_definitions(stdlib_module_test PRIVATE
    LIVA_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

liva_add_test(diag_color_test
    unit/DiagColorTest.cpp
)
target_link_libraries(diag_color_test PRIVATE liva_common)

# Performance regression tests
liva_add_test(benchmark_test
    unit/BenchmarkTest.cpp
)
target_link_libraries(benchmark_test PRIVATE liva_sema liva_parser)

# Incremental compilation performance benchmarks (100+ files)
liva_add_test(incremental_benchmark_test
    unit/IncrementalBenchmarkTest.cpp
)
target_link_libraries(incremental_benchmark_test PRIVATE liva_driver)

# Property-based tests (random program generation)
liva_add_test(property_test
    unit/PropertyTest.cpp
)
target_link_libraries(property_test PRIVATE liva_sema liva_parser)

# Integration tests (file-based + inline)
liva_add_test(integration_test
    unit/IntegrationTest.cpp
)
target_link_libraries(integration_test PRIVATE liva_driver liva_sema liva_parser)
target_compile_definitions(integration_test PRIVATE
    LIVA_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)
if(LIVA_HAS_LLVM AND NOT MINGW)
    target_sources(integration_test PRIVATE ${PROJECT_SOURCE_DIR}/src/Driver/CompilerInstance.cpp)
    target_link_libraries(integration_test PRIVATE liva_codegen liva_irgen liva_jit liva_runtime liva_plugin)
    target_compile_definitions(integration_test PRIVATE LIVA_HAS_LLVM=1)
    if(WIN32)
        target_link_libraries(integration_test PRIVATE winhttp)
    endif()
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
        ${PROJECT_SOURCE_DIR}/src/Driver/Driver.cpp
    )
    target_link_libraries(selfhost_test PRIVATE
        liva_codegen liva_irgen liva_sema liva_parser liva_driver liva_plugin
        liva_lsp liva_dap liva_repl liva_jit liva_runtime)
    if(WIN32)
        target_link_libraries(selfhost_test PRIVATE winhttp)
    endif()
    target_compile_definitions(selfhost_test PRIVATE
        LIVA_HAS_LLVM=1
        LIVA_BUILD_DIR="${CMAKE_BINARY_DIR}")
    add_dependencies(selfhost_test liva_runtime)
endif()

# Runtime exec harness — compile .liva source -> exe -> run -> assert stdout.
# Foundational layer for generator/yield runtime tests (Tasks 2-8 of the
# generator-runtime plan depend on this). Mirrors selfhost_test linkage.
if(LIVA_HAS_LLVM AND NOT MINGW)
    liva_add_test(runtime_exec_test
        unit/RuntimeExecTest.cpp
        ${PROJECT_SOURCE_DIR}/src/Driver/CompilerInstance.cpp
        ${PROJECT_SOURCE_DIR}/src/Driver/Driver.cpp
    )
    target_link_libraries(runtime_exec_test PRIVATE
        liva_codegen liva_irgen liva_sema liva_parser liva_driver liva_plugin
        liva_lsp liva_dap liva_repl liva_jit liva_runtime)
    if(WIN32)
        target_link_libraries(runtime_exec_test PRIVATE winhttp)
    endif()
    target_compile_definitions(runtime_exec_test PRIVATE
        LIVA_HAS_LLVM=1
        LIVA_BUILD_DIR="${CMAKE_BINARY_DIR}")
    add_dependencies(runtime_exec_test liva_runtime)

    # UI/class codegen IR-emit regression for the class-based UI library and
    # class-codegen fixes. Isolated from RuntimeExecTest.cpp. Shells out to the
    # built `livac --emit-ir` (no link), so it needs no liva libs — only the
    # livac binary, which it depends on.
    liva_add_test(ui_codegen_exec_test unit/UICodegenExecTest.cpp)
    target_compile_definitions(ui_codegen_exec_test PRIVATE
        LIVA_HAS_LLVM=1
        LIVA_BUILD_DIR="${CMAKE_BINARY_DIR}")
    add_dependencies(ui_codegen_exec_test livac)
endif()

# examples/*.liva coverage harness — compile+run curated example programs
# and pin their stdout. Guards examples/ against silent bit-rot as the
# stdlib/language evolve. Mirrors runtime_exec_test linkage.
if(LIVA_HAS_LLVM AND NOT MINGW)
    liva_add_test(examples_test
        unit/ExamplesTest.cpp
        ${PROJECT_SOURCE_DIR}/src/Driver/CompilerInstance.cpp
        ${PROJECT_SOURCE_DIR}/src/Driver/Driver.cpp
    )
    target_link_libraries(examples_test PRIVATE
        liva_codegen liva_irgen liva_sema liva_parser liva_driver liva_plugin
        liva_lsp liva_dap liva_repl liva_jit liva_runtime)
    if(WIN32)
        target_link_libraries(examples_test PRIVATE winhttp)
    endif()
    target_compile_definitions(examples_test PRIVATE
        LIVA_HAS_LLVM=1
        LIVA_BUILD_DIR="${CMAKE_BINARY_DIR}"
        LIVA_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_dependencies(examples_test liva_runtime)
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
