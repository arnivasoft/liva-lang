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

# Performance regression tests
liva_add_test(benchmark_test
    unit/BenchmarkTest.cpp
)
target_link_libraries(benchmark_test PRIVATE liva_sema liva_parser)

# Integration tests (file-based + inline)
liva_add_test(integration_test
    unit/IntegrationTest.cpp
)
target_link_libraries(integration_test PRIVATE liva_sema liva_parser)
target_compile_definitions(integration_test PRIVATE
    LIVA_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

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
