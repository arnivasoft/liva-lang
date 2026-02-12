# Contributing to Liva

Thank you for your interest in contributing to the Liva programming language!

## Getting Started

### Prerequisites

- **CMake** 3.20 or later
- **Ninja** build system (`winget install Ninja-build.Ninja` on Windows)
- **C++20 compiler**: Clang 16+, GCC 13+, or MSVC 2022
- **LLVM 21** (optional — only needed for code generation; tests work without it)

### Building

**Windows (Clang + MSVC ABI — recommended):**

```batch
build_clang.bat
```

**Windows (MinGW GCC — tests only):**

```batch
cmake -G "MinGW Makefiles" -B build
cmake --build build
```

**Linux / macOS:**

```bash
./build.sh
```

### Running Tests

```bash
# MinGW build
ctest --test-dir build --output-on-failure

# Clang build
ctest --test-dir build-clang --output-on-failure
```

All 613 tests must pass before submitting a pull request.

## Project Structure

| Directory | Description |
|-----------|-------------|
| `include/liva/` | Public header files for all components |
| `src/` | Implementation files organized by component |
| `tests/unit/` | GoogleTest unit tests |
| `tests/integration/` | End-to-end `.liva` test programs |
| `tests/error/` | Expected-error test cases |
| `examples/` | Example Liva programs |
| `stdlib/runtime/` | C++ runtime library |
| `cmake/` | CMake modules and toolchain files |

## Code Style

- **C++20** standard
- 4-space indentation
- `camelCase` for functions and variables, `PascalCase` for types
- Use `.clang-format` for automatic formatting
- No exceptions in MinGW builds (use error codes / `strtol` instead of `stoi`)
- Prefer `is_open()` over `good()` for file stream checks

## Adding a New Feature

1. **Parser**: Add new AST nodes in `include/liva/AST/` and parsing in `src/Parser/`
2. **Sema**: Add type-checking rules in `src/Sema/TypeChecker.cpp`
3. **IRGen**: Add code generation in the appropriate `src/IR/IRGen*.cpp` file
4. **Tests**: Add unit tests in the relevant test file under `tests/unit/`
5. **Examples**: Add a `.liva` example demonstrating the feature in `examples/`

## Adding Tests

Tests use GoogleTest. Each component has its own test file:

| Component | Test File |
|-----------|-----------|
| Lexer | `tests/unit/LexerTest.cpp` |
| Parser | `tests/unit/ParserTest.cpp` |
| Sema | `tests/unit/SemaTest.cpp` |
| Types | `tests/unit/TypeTest.cpp` |
| Ownership | `tests/unit/OwnershipTest.cpp` |
| Project Config | `tests/unit/ProjectConfigTest.cpp` |
| LSP | `tests/unit/LSPTest.cpp` |
| REPL | `tests/unit/REPLTest.cpp` |

To add a new test:

```cpp
TEST(SemaTest, MyNewFeature) {
    std::string code = R"(
func main() {
    // test code here
}
)";
    SourceManager sm("test", code);
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    Parser parser(lexer, diag);
    auto tu = parser.parseTranslationUnit();
    ASSERT_FALSE(diag.hasErrors());

    Sema sema(diag, nullptr);
    sema.analyze(*tu);
    EXPECT_FALSE(diag.hasErrors());
}
```

## Known Build Notes

- MinGW builds disable exceptions (`-fno-exceptions`) — use `strtol` instead of `stoi`/`try-catch`
- MinGW `livac.exe` linking fails with LLVM libraries (expected); unit tests still work
- Raw string literals with `\(` need custom delimiters: `R"--(...)--"`
- Multi-field inline structs (`struct Pt { var x: i32; var y: i32 }`) hang the parser — use multi-line declarations
- On Windows, `std::system()` commands with quoted paths need extra wrapping for `cmd.exe`

## Pull Request Process

1. Create a feature branch from `main`
2. Make your changes with clear, focused commits
3. Ensure all 613+ tests pass
4. Add new tests for any new functionality
5. Update `plan.md` if adding a milestone
6. Submit a pull request with a clear description

## Reporting Issues

Please report issues at the project's issue tracker with:
- Steps to reproduce
- Expected vs actual behavior
- Compiler and platform information
- Minimal `.liva` code reproducing the issue (if applicable)
