# Liva Programming Language

A statically-typed programming language with Swift-like syntax and Rust-style ownership/borrowing semantics.

Liva compiles to native code via LLVM, offering memory safety without garbage collection, expressive pattern matching, generics with trait bounds, and a growing standard library.

## Features

- **Ownership & Borrowing** — Move semantics, references (`ref`, `ref mut`), lifetime analysis
- **Type System** — Generics, protocols (traits), optional types, result types, type aliases
- **Pattern Matching** — Exhaustive `match` expressions with nested patterns, enum associated values
- **Closures** — Capture by value/reference, trailing closure syntax, type inference
- **Async/Await** — Coroutine-based asynchronous programming
- **Modules** — Import system with standard library modules
- **LLVM Backend** — Native code generation with optimization levels (O0-O3)
- **Tooling** — LSP server, interactive REPL, project manifest (`liva.toml`)

## Quick Start

```liva
func fibonacci(n: i32) -> i32 {
    if n <= 1 {
        return n
    }
    return fibonacci(n - 1) + fibonacci(n - 2)
}

func main() {
    for i in 0..10 {
        println(fibonacci(i))
    }
}
```

## Building from Source

### Prerequisites

- **CMake** 3.20+
- **Ninja** build system
- **C++20** compiler (Clang 16+, GCC 13+, or MSVC 2022)
- **LLVM 21** (optional — required for code generation, not for tests)
- **GoogleTest** (fetched automatically by CMake)

### Windows (Recommended: Clang + MSVC ABI)

```batch
:: Requires LLVM/Clang installed at C:\LLVM and Visual Studio
build_clang.bat
ctest --test-dir build-clang --output-on-failure
```

### Windows (MinGW — tests only, no codegen)

```batch
cmake -G "MinGW Makefiles" -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

### Linux / macOS

```bash
# Requires Ninja: apt install ninja-build / brew install ninja
./build.sh --test

# Or manually:
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Usage

```bash
# Compile a single file
livac -o hello examples/hello.liva

# Compile and run
livac run

# Project build (reads liva.toml)
livac build --release

# Initialize a new project
livac init myproject

# Start LSP server (for editor integration)
livac lsp

# Interactive REPL
livac repl

# Diagnostic tools
livac --dump-tokens file.liva    # Show token stream
livac --dump-ast file.liva       # Show AST
livac --check-only file.liva     # Type-check without codegen
livac --emit-ir file.liva        # Output LLVM IR
```

## Language Overview

### Variables and Constants

```liva
let name: string = "Liva"      // immutable binding
var count: i32 = 0              // mutable binding
const MAX_SIZE: i32 = 100       // compile-time constant
```

### Functions

```liva
func add(a: i32, b: i32) -> i32 {
    return a + b
}

func greet(name: string, greeting: string = "Hello") {
    println("\(greeting), \(name)!")
}
```

### Structs and Methods

```liva
struct Point {
    var x: f64
    var y: f64
}

impl Point {
    func new(x: f64, y: f64) -> Point {
        return Point { x: x, y: y }
    }

    func distance(ref self) -> f64 {
        return sqrt(self.x * self.x + self.y * self.y)
    }

    func translate(ref mut self, dx: f64, dy: f64) {
        self.x = self.x + dx
        self.y = self.y + dy
    }
}
```

### Enums and Pattern Matching

```liva
enum Shape {
    case Circle(f64)
    case Rectangle(f64, f64)
    case Triangle(f64, f64, f64)
}

func area(shape: Shape) -> f64 {
    match shape {
        Circle(r) => 3.14159 * r * r
        Rectangle(w, h) => w * h
        Triangle(a, b, c) => {
            let s = (a + b + c) / 2.0
            return sqrt(s * (s - a) * (s - b) * (s - c))
        }
    }
}
```

### Generics and Protocols

```liva
protocol Printable {
    func toString(ref self) -> string
}

func printItem<T: Printable>(item: ref T) {
    println(item.toString())
}

struct Box<T> {
    var value: T
}

impl<T> Box<T> {
    func get(ref self) -> T {
        return self.value
    }
}
```

### Ownership and References

```liva
func take_ownership(s: string) {
    println(s)
    // s is dropped here
}

func borrow(s: ref string) {
    println(s)          // read-only borrow
}

func mutate(s: ref mut string) {
    s = "modified"      // mutable borrow
}
```

### Closures

```liva
let numbers = [1, 2, 3, 4, 5]
let doubled = numbers.map { |x| x * 2 }
let evens = numbers.filter { |x| x % 2 == 0 }
let sum = numbers.reduce(0) { |acc, x| acc + x }
```

### Error Handling

```liva
enum FileError {
    case NotFound
    case PermissionDenied
}

func readFile(path: string) -> Result<string, FileError> {
    // ...
}

func main() {
    let result = readFile("data.txt")
    match result {
        Ok(content) => println(content)
        Err(e) => println("Error reading file")
    }
}
```

### Optional Types

```liva
func find(arr: [i32], target: i32) -> i32? {
    for item in arr {
        if item == target {
            return item
        }
    }
    return nil
}

let value = find([1, 2, 3], 2) ?? 0    // nil coalescing
```

### Async/Await

```liva
async func fetchData(url: string) -> string {
    let response = await httpGet(url)
    return response
}
```

### Standard Library Modules

```liva
import std::math      // abs, sqrt, pow, sin, cos, ...
import std::io        // readLine, readFile, writeFile
import std::convert   // parseInt, parseFloat, toString
import std::os        // env, args, exit, exec
import std::random    // randInt, randFloat
import std::regex     // Regex, match, replace
import std::net       // httpGet, httpPost
```

## Project Manifest

Create a `liva.toml` file in your project root:

```toml
[project]
name = "myapp"
version = "1.0.0"
entry = "src/main.liva"

[build]
optimization = "release"

[dependencies]
json_parser = "^1.0.0"
```

## Architecture

```
Source Code (.liva)
        |
        v
    [ Lexer ]  -->  Token Stream (103 token types)
        |
        v
    [ Parser ] -->  AST (40+ node types)
        |
        v
    [ Sema ]   -->  Type Checking + Ownership Analysis + Lifetime Analysis
        |
        v
    [ IRGen ]  -->  LLVM IR
        |
        v
    [ CodeGen ] --> Native Executable
```

## Project Structure

```
liva-lang/
  include/liva/       # Public headers (AST, Lexer, Parser, Sema, IR, ...)
  src/                 # Implementation files
    AST/               # AST nodes and printer
    Lexer/             # Tokenizer
    Parser/            # Recursive-descent parser
    Sema/              # Type checker, ownership checker, lifetime analysis
    IR/                # LLVM IR generation (6 files)
    CodeGen/           # Native code generation
    LSP/               # Language Server Protocol
    REPL/              # Interactive REPL
    Driver/            # CLI driver, project config (TOML)
  tests/
    unit/              # GoogleTest unit tests (613 tests)
    integration/       # End-to-end .liva programs
    error/             # Expected-error test cases
  examples/            # Example Liva programs
  stdlib/              # Standard library and runtime
  cmake/               # CMake modules
```

## Test Suite

613 tests across 8 test files:

| Component | Tests | Coverage |
|-----------|-------|----------|
| Lexer | 41 | Tokens, literals, comments, positions, string interpolation |
| Parser | 82 | Declarations, expressions, generics, closures, protocols |
| Sema | 321 | Comprehensive semantic analysis for all language features |
| Type | 12 | Type compatibility, conversions, bit widths |
| Ownership | 9 | Move, borrow, use-after-move, lifetime |
| ProjectConfig | 74 | TOML parsing, SemVer, dependencies, lock files |
| LSP | 37 | JSON, lifecycle, sync, completion, hover, definition |
| REPL | 37 | Input classification, commands, multi-line, expression wrapping |

Run the full test suite:

```bash
ctest --test-dir build --output-on-failure
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions, coding guidelines, and how to submit pull requests.

## License

This project is provided as-is for educational and research purposes.
