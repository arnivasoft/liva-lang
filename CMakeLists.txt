cmake_minimum_required(VERSION 3.20)

project(liva-lang
    VERSION 0.1.0
    DESCRIPTION "Liva Programming Language Compiler"
    LANGUAGES CXX
)

# Include CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CompilerFlags)
include(LLVM)
include(GoogleTest)

# Global include directory
include_directories(${PROJECT_SOURCE_DIR}/include)

# Pass version info
configure_file(
    ${PROJECT_SOURCE_DIR}/include/liva/Common/Version.h.in
    ${PROJECT_BINARY_DIR}/include/liva/Common/Version.h
)
include_directories(${PROJECT_BINARY_DIR}/include)

# === Static Libraries (one per compiler phase) ===

# liva_common
add_library(liva_common STATIC
    src/Common/SourceLocation.cpp
    src/Common/Diagnostics.cpp
)
liva_set_compiler_flags(liva_common)

# liva_lexer
add_library(liva_lexer STATIC
    src/Lexer/Token.cpp
    src/Lexer/Lexer.cpp
)
target_link_libraries(liva_lexer PUBLIC liva_common)
liva_set_compiler_flags(liva_lexer)

# liva_ast
add_library(liva_ast STATIC
    src/AST/ASTNode.cpp
    src/AST/Decl.cpp
    src/AST/Stmt.cpp
    src/AST/Expr.cpp
    src/AST/Type.cpp
    src/AST/ASTPrinter.cpp
)
target_link_libraries(liva_ast PUBLIC liva_common)
liva_set_compiler_flags(liva_ast)

# liva_parser
add_library(liva_parser STATIC
    src/Parser/Parser.cpp
    src/Parser/ParseDecl.cpp
    src/Parser/ParseStmt.cpp
    src/Parser/ParseExpr.cpp
    src/Parser/ParseType.cpp
)
target_link_libraries(liva_parser PUBLIC liva_lexer liva_ast)
liva_set_compiler_flags(liva_parser)

# liva_sema
add_library(liva_sema STATIC
    src/Sema/Sema.cpp
    src/Sema/TypeChecker.cpp
    src/Sema/OwnershipChecker.cpp
    src/Sema/LifetimeAnalysis.cpp
    src/Sema/Scope.cpp
    src/Sema/ModuleLoader.cpp
)
target_link_libraries(liva_sema PUBLIC liva_parser liva_ast liva_common)
liva_set_compiler_flags(liva_sema)

# liva_irgen (only if LLVM is available)
if(LIVA_HAS_LLVM)
    add_library(liva_irgen STATIC
        src/IR/IRGen.cpp
        src/IR/IRGenDecl.cpp
        src/IR/IRGenStmt.cpp
        src/IR/IRGenExpr.cpp
        src/IR/IRGenCall.cpp
        src/IR/IRGenMono.cpp
    )
    target_link_libraries(liva_irgen PUBLIC liva_ast liva_sema ${LIVA_LLVM_LIBS})
    liva_set_compiler_flags(liva_irgen)
    target_compile_definitions(liva_irgen PUBLIC LIVA_HAS_LLVM=1)

    add_library(liva_codegen STATIC
        src/CodeGen/CodeGen.cpp
        src/CodeGen/TargetInfo.cpp
    )
    target_link_libraries(liva_codegen PUBLIC liva_irgen ${LIVA_LLVM_LIBS})
    liva_set_compiler_flags(liva_codegen)
    target_compile_definitions(liva_codegen PUBLIC LIVA_HAS_LLVM=1)
endif()

# === Runtime Library (linked into compiled Liva programs) ===
if(LIVA_HAS_LLVM)
    add_library(liva_runtime STATIC stdlib/runtime/runtime.cpp)
    target_include_directories(liva_runtime PRIVATE stdlib/runtime)
    target_compile_definitions(liva_runtime PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(liva_runtime PRIVATE -Wno-deprecated-declarations)
    endif()
    # On Linux/macOS, link libcurl for HTTP support if available
    if(NOT WIN32)
        find_package(CURL)
        if(CURL_FOUND)
            target_link_libraries(liva_runtime PRIVATE CURL::libcurl)
            target_compile_definitions(liva_runtime PRIVATE LIVA_HAS_CURL)
        endif()
    endif()
    # Also link libstdc++ on Linux for std::regex and std::string
    if(NOT WIN32 AND NOT APPLE)
        target_link_libraries(liva_runtime PRIVATE stdc++)
    endif()
    # Copy runtime lib next to livac: lib/liva_runtime.lib or lib/libliva_runtime.a
    add_custom_command(TARGET liva_runtime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lib"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:liva_runtime>" "${CMAKE_BINARY_DIR}/lib/"
    )
endif()

# liva_lsp
add_library(liva_lsp STATIC
    src/LSP/LSPServer.cpp
)
target_link_libraries(liva_lsp PUBLIC liva_sema liva_parser liva_common)
liva_set_compiler_flags(liva_lsp)

# liva_driver (TOML parser, path utils)
add_library(liva_driver STATIC
    src/Driver/ProjectConfig.cpp
)
target_link_libraries(liva_driver PUBLIC liva_common)
liva_set_compiler_flags(liva_driver)

# === Main Executable ===
add_executable(livac
    src/Driver/main.cpp
    src/Driver/Driver.cpp
    src/Driver/CompilerInstance.cpp
)
target_link_libraries(livac PRIVATE
    liva_driver
    liva_lsp
    liva_parser
    liva_sema
)
if(LIVA_HAS_LLVM)
    target_link_libraries(livac PRIVATE liva_codegen liva_irgen)
    target_compile_definitions(livac PRIVATE LIVA_HAS_LLVM=1)
    add_dependencies(livac liva_runtime)
endif()
liva_set_compiler_flags(livac)

# === Tests ===
add_subdirectory(tests)

# === Install ===
install(TARGETS livac DESTINATION bin)
