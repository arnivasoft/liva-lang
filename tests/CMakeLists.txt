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

liva_add_test(lsp_test
    unit/LSPTest.cpp
)
target_link_libraries(lsp_test PRIVATE liva_lsp liva_sema liva_parser)

liva_add_test(repl_test
    unit/REPLTest.cpp
)
target_link_libraries(repl_test PRIVATE liva_repl liva_sema liva_parser)
