# Liva Programming Language

A statically-typed programming language with Swift-like syntax and Rust-style ownership/borrowing semantics.

Liva compiles to native code via LLVM, offering memory safety without garbage collection, expressive pattern matching, generics with trait bounds, and a growing standard library.

## Features

- **Ownership & Borrowing** — Move semantics, references (`ref`, `ref mut`), lifetime analysis
- **Type System** — Generics, protocols (traits), optional types, result types, type aliases, const generics (`func foo<T, const N: i32>()`)
- **Explicit Lifetimes** — `'a` syntax (`ref 'a T`), lifetime elision (automatic inference)
- **Generators/Yield** — Generator functions with `yield expr`
- **GATs** — Generic associated types in protocols (`type Item<'a>`)
- **Enum Discriminants** — Custom enum case values (`case OK = 200`)
- **Pattern Matching** — Exhaustive `match` expressions with nested patterns, enum associated values
- **Closures** — Capture by value/reference, trailing closure syntax, type inference
- **Async/Await** — Coroutine-based asynchronous programming
- **Modules** — Import system with standard library modules
- **LLVM Backend** — Native code generation with optimization levels (O0-O3)
- **Tooling** — LSP server (18+ features), interactive REPL with JIT, `livac bench`, `livac test`, project manifest (`liva.toml`)
- **Classes** — Swift-style class system: single inheritance, vtable dispatch, `init`/`deinit`, `override`, `super`, `final`, `static`, computed properties, property observers (`willSet`/`didSet`), `is`/`as?` type checks, failable `init?`, `convenience init` with overload resolution, `lazy var`, subscripts (incl. generic), five access levels (`open`/`public`/`internal`/`fileprivate`/`private`), extensions
- **FFI** — `extern "C"` declarations for C interoperability
- **Comptime & Macros** — Compile-time evaluation blocks, pattern-based macros
- **dyn Protocol** — Trait objects with dynamic dispatch
- **Test Framework** — Built-in `test` blocks with `livac test`
- **Concurrency** — Channels (buffered), TaskGroups for structured concurrency
- **Benchmarking** — Built-in benchmarking with `livac bench`
- **JIT Compilation** — LLJIT-based just-in-time execution
- **Cross-compilation** — `--target` flag for multi-platform builds
- **Plugin System** — Compiler plugin API for custom analyses
- **Property-based Testing** — Automated property checks with shrinking
- **WASM** — WebAssembly target support (`--target wasm32`)
- **Online Playground** — Browser-based playground with JS interpreter
- **UI Framework** — raylib-based widget system (12 phases: widgets, layout, theming, animation, focus, tooltips)
- **Rich Diagnostics** — Rust-style underline spans, help suggestions, did-you-mean
- **Debug Adapter** — DAP server with conditional breakpoints, expression evaluator
- **Separate Compilation** — `--emit-obj` and `livac link` for incremental workflows
- **Security** — Slice bounds checking, parse overflow guards, FFI type safety warnings
- **Async Runtime** — Thread pool scheduler, channels, task groups, `for await`, async I/O
- **SemVer Constraints** — `^` and `~` version constraint operators in `liva.toml`
- **CI Benchmarks** — Benchmark regression tracking in CI

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

# Run benchmarks
livac bench

# Run tests
livac test

# Remove a dependency
livac remove <package>

# Cross-compile
livac build --target x86_64-linux-gnu

# Diagnostic tools
livac --dump-tokens file.liva    # Show token stream
livac --dump-ast file.liva       # Show AST
livac --check-only file.liva     # Type-check without codegen
livac --emit-ir file.liva        # Output LLVM IR
livac --emit-obj file.liva      # Output object file
livac --dump-timings file.liva  # Show per-phase compilation timing
livac --trace-macros file.liva  # Trace macro expansions
livac link a.o b.o -o app       # Link object files
livac format file.liva          # Format source code
livac lint file.liva            # Run linter
livac dap                       # Start Debug Adapter Protocol server
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
import http::http

async func fetchData(url: string) -> string {
    // Use http::http for network requests
    let resp = HttpRequest.get(url).send()
    return resp.text()
}
```

### Standard Library Modules

Built-in `std::*` modules (low-level builtins):

```liva
import std::math      // abs, sqrt, pow, sin, cos, floor, ceil, round, ...
import std::io        // print, println, readLine, File, fileRead/Write, path helpers
import std::convert   // parseInt, parseInt64, parseFloat, toString, charToString
import std::os        // env, args, exit, exec, processStart/Wait/Kill/Read
import std::random    // randInt, randFloat, randSeed, randI64, randUuid
import std::regex     // regexMatch, regexFind/FindAll, regexSplit, compiled regex
import std::net       // low-level HTTP builtins (httpRequestEx, httpStatus, httpBody, …)
import std::json      // jsonParse + DOM node builtins (jsonObjGet, jsonArrAt, jsonToString, ...)
import std::datetime  // dateNow, dateParse, dateAdd, dateDiff, dateFormat
import std::compress  // base64/hex/urlEncode+Decode, crc32
import std::crypto    // sha256, md5, hmacSha256, base64, hex
import std::sync      // mutex, atomic, channel, taskGroup primitives
import std::async     // schedulerInit, taskSelect, withTimeout, async I/O
import std::collections // Map, Set, forEach, enumerate, zip, sorted, ...
import std::strings   // str* helpers + UTF-8 (strCharCount, strCodepointAt, charIsAlpha, ...)
import std::test      // assert, assertEq, testRunClosure
import std::log       // logDebug/Info/Warn/Error, logSetLevel
import std::ui        // wxWidgets-based UI (widgets, layout, theming, canvas)
```

Higher-level wrapper modules (ergonomic structs on top of builtins):

```liva
import random::random         // Random struct + randBool/randPercent
import os::os                 // Process struct + getEnv/getArgs/runCommand
import log::log               // Logger struct with tag support
import math::math             // PI/TAU/E constants, clamp/sign/degToRad
import convert::convert       // toInt/toFloat + toIntOr/toFloatOr
import encoding::encoding     // Base64/Hex/Url structs + toBase64/toHex/toUrl
import errors::errors         // withContext/unwrapOr + ErrorChain
import collections::collections // Stack<T>, Queue<T>, Deque, HashSet + math/slice helpers
import strings::strings       // toCodepoints, countAlpha/Digit, isAlnum, isBlank
import io::io                 // LineReader, LineWriter, readLines, writeLines
import time::time             // Duration, Instant, Timer, DateTime (+ add/sub/diff days/hours)
import fs::fs                 // FileInfo (size, modifiedTime, isDir), Dir ops
import path::path             // Path manipulation
import http::http             // HttpRequest (fluent builder), HttpResponse (eager-copy), HttpClient
import net::net               // Url (parse/build/encode/decode)
import json::json             // Json/JsonValue/JsonObject/JsonArray parse-tree (typed get, path, obj["k"])
import crypto::crypto         // Hash/Hmac structs
import sync::sync             // Mutex, AtomicI64, Channel, TaskGroup
import async::async           // withTaskGroup, withTimeout, raceIndex
import regex::regex           // Regex struct (isMatch, find, findAll, split, groups)
import testing::testing       // TestSuite, TestGroup, Expect/ExpectStr/ExpectFloat
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
        |
        v
    [ JIT ]    -->  In-memory Execution (LLJIT)
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
    unit/              # GoogleTest unit tests (2064 tests)
    integration/       # End-to-end .liva programs
    error/             # Expected-error test cases
  examples/            # Example Liva programs
  stdlib/              # Standard library and runtime
  cmake/               # CMake modules
```

## Test Suite

2064 tests across 19 test files:

| Component | Tests | Coverage |
|-----------|-------|----------|
| Sema | 645 | Type checking, ownership, generics, classes, FFI, comptime, macros, UI |
| ProjectConfig | 241 | TOML parsing, SemVer, dependencies, lock files, remote registry |
| Integration | 196 | End-to-end programs, error recovery, module system |
| LSP | 153 | JSON, lifecycle, sync, completion, hover, definition, code actions, code lens, call hierarchy |
| Parser | 149 | Declarations, expressions, generics, closures, protocols, classes |
| UI Module | 111 | Widget types, layout, theming, animation, focus, tooltip |
| Ownership | 98 | Move, borrow, use-after-move, lifetime, class, closure |
| REPL | 57 | Input classification, commands, multi-line, expression wrapping |
| Lexer | 56 | Tokens, literals, comments, positions, string interpolation |
| Type | 53 | Type compatibility, conversions, bit widths |
| SelfHost | 48 | Self-hosting compilation, async runtime (Clang only) |
| DAP | 45 | Conditional breakpoints, expression evaluator, DWARF debug |
| Macro | 34 | Macro definition, expansion, hygiene, comptime |
| CodeGen | 21 | LLVM IR generation, cross-compilation targets |
| Plugin | 18 | Plugin API, naming convention, unused function detection |
| StdlibModule | 16 | JSON, time, path, testing, crypto wrappers |
| Benchmark | 14 | Bench builtins, bench runner, report formatting |
| DiagColor | 12 | Rich diagnostic formatting, underline spans, colored output |
| IncrementalBenchmark | 11 | 100+ file incremental build benchmarks |

Run the full test suite:

```bash
# MinGW build
ctest --test-dir build --output-on-failure

# Clang build (recommended, includes codegen + JIT tests)
ctest --test-dir build-clang --output-on-failure
```

## IDE Support

Liva has editor support for 5 editors:

| Editor | Features | Location |
|--------|----------|----------|
| **VS Code** | Syntax highlighting, LSP client, DAP client | `editors/vscode/` |
| **Neovim** | Syntax, ftdetect, indent, ftplugin + LSP/DAP guide | `editors/neovim/` |
| **Emacs** | liva-mode.el major mode + eglot/lsp-mode/dap-mode guide | `editors/emacs/` |
| **JetBrains** | TextMate grammar + LSP4IJ plugin guide | `editors/jetbrains/` |
| **Notepad++** | UDL XML syntax highlighting | `editors/notepadpp/` |

Additional tooling:
- **Tree-sitter grammar** for Neovim (`editors/neovim/tree-sitter-liva/`)
- **VS Code Test Explorer** integration for running Liva tests from the editor

LSP server: `livac lsp` (stdio JSON-RPC 2.0)
DAP server: `livac dap` (stdio Debug Adapter Protocol)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions, coding guidelines, and how to submit pull requests.

## License

This project is provided as-is for educational and research purposes.
