# Liva Cookbook

Common patterns, recipes, and idioms for the Liva programming language. Each recipe is self-contained and demonstrates a practical technique.

---

## Table of Contents

1. [Error Handling with `?` Operator](#1-error-handling-with--operator)
2. [Builder Pattern](#2-builder-pattern)
3. [Observer Pattern with Protocols](#3-observer-pattern-with-protocols)
4. [Custom Iterator](#4-custom-iterator)
5. [RAII with Drop Trait](#5-raii-with-drop-trait)
6. [Generic Container](#6-generic-container)
7. [Async HTTP Requests](#7-async-http-requests)
8. [Channel-Based Producer/Consumer](#8-channel-based-producerconsumer)
9. [FFI with C Libraries](#9-ffi-with-c-libraries)
10. [Cross-Compilation to WASM](#10-cross-compilation-to-wasm)

---

## 1. Error Handling with `?` Operator

The `?` operator unwraps a `Result` or propagates the error to the caller.

```liva
enum ParseError {
    case InvalidFormat
    case OutOfRange
}

func parseAge(input: string) -> Result<i32, ParseError> {
    let num = parseInt(input)
    guard num > 0 else {
        return Err(ParseError.OutOfRange)
    }
    guard num < 150 else {
        return Err(ParseError.OutOfRange)
    }
    return Ok(num)
}

func parseUser(name: string, ageStr: string) -> Result<string, ParseError> {
    let age = parseAge(ageStr)?   // propagates error if Err
    return Ok(format("{} is {} years old", name, age))
}

func main() {
    match parseUser("Alice", "30") {
        Ok(msg) => println(msg)
        Err(ParseError.InvalidFormat) => println("Bad format")
        Err(ParseError.OutOfRange) => println("Age out of range")
    }
}
```

**Key points:**
- `?` after a `Result<T, E>` expression unwraps `Ok(T)` or returns `Err(E)` early
- The calling function must also return a `Result` with a compatible error type
- Combine with `guard` for validation chains

---

## 2. Builder Pattern

Use method chaining to construct complex objects step by step.

```liva
struct QueryBuilder {
    var table: string
    var conditions: [string]
    var orderBy: string
    var limitVal: i32
}

impl QueryBuilder {
    func new(table: string) -> QueryBuilder {
        return QueryBuilder {
            table: table,
            conditions: [],
            orderBy: "",
            limitVal: 0
        }
    }

    func where_clause(ref mut self, condition: string) -> ref mut QueryBuilder {
        self.conditions.push(condition)
        return self
    }

    func order(ref mut self, field: string) -> ref mut QueryBuilder {
        self.orderBy = field
        return self
    }

    func limit(ref mut self, n: i32) -> ref mut QueryBuilder {
        self.limitVal = n
        return self
    }

    func build(ref self) -> string {
        var sql = "SELECT * FROM " + self.table
        if self.conditions.length > 0 {
            sql = sql + " WHERE "
            for i in 0..self.conditions.length {
                if i > 0 { sql = sql + " AND " }
                sql = sql + self.conditions[i]
            }
        }
        if self.orderBy != "" {
            sql = sql + " ORDER BY " + self.orderBy
        }
        if self.limitVal > 0 {
            sql = sql + " LIMIT " + toString(self.limitVal)
        }
        return sql
    }
}

func main() {
    var query = QueryBuilder.new("users")
    let sql = query
        .where_clause("age > 18")
        .where_clause("active = true")
        .order("name")
        .limit(10)
        .build()
    println(sql)
    // SELECT * FROM users WHERE age > 18 AND active = true ORDER BY name LIMIT 10
}
```

---

## 3. Observer Pattern with Protocols

Use protocols to define a notification interface.

```liva
protocol Observer {
    func onEvent(ref self, event: string)
}

struct Logger {}

impl Logger: Observer {
    func onEvent(ref self, event: string) {
        println("[LOG] \(event)")
    }
}

struct Counter {
    var count: i32
}

impl Counter: Observer {
    func onEvent(ref self, event: string) {
        println("[COUNT] Event #\(self.count)")
    }
}

struct EventBus {
    var listeners: [dyn Observer]
}

impl EventBus {
    func new() -> EventBus {
        return EventBus { listeners: [] }
    }

    func subscribe(ref mut self, listener: dyn Observer) {
        self.listeners.push(listener)
    }

    func emit(ref self, event: string) {
        for listener in self.listeners {
            listener.onEvent(event)
        }
    }
}

func main() {
    var bus = EventBus.new()
    bus.subscribe(Logger {})
    bus.subscribe(Counter { count: 1 })
    bus.emit("user_login")
}
```

---

## 4. Custom Iterator

Implement the `Iterator` protocol to create iterable types.

```liva
struct FibonacciIterator {
    var a: i32
    var b: i32
    var remaining: i32
}

impl FibonacciIterator {
    func new(count: i32) -> FibonacciIterator {
        return FibonacciIterator { a: 0, b: 1, remaining: count }
    }
}

impl FibonacciIterator: Iterator {
    type Item = i32

    func next(ref mut self) -> i32? {
        if self.remaining <= 0 {
            return nil
        }
        let current = self.a
        let temp = self.b
        self.b = self.a + self.b
        self.a = temp
        self.remaining = self.remaining - 1
        return current
    }
}

func main() {
    var fib = FibonacciIterator.new(10)
    for val in fib {
        println(val)
    }
    // Output: 0 1 1 2 3 5 8 13 21 34
}
```

---

## 5. RAII with Drop Trait

Use the `Drop` protocol for automatic resource cleanup.

```liva
struct TempFile {
    var path: string
}

impl TempFile {
    func new(path: string, content: string) -> TempFile {
        writeFile(path, content)
        println("Created: \(path)")
        return TempFile { path: path }
    }

    func read(ref self) -> string {
        return readFile(self.path)
    }
}

impl TempFile: Drop {
    func drop(mut self) {
        println("Deleting: \(self.path)")
        // In real code: delete the file
    }
}

func main() {
    let tmp = TempFile.new("/tmp/data.txt", "hello")
    let content = tmp.read()
    println(content)
}   // tmp.drop() called automatically — file cleaned up
```

**Key points:**
- `Drop.drop(mut self)` is called automatically at scope exit
- Drop is called in reverse declaration order
- Use for closing files, network connections, releasing locks

---

## 6. Generic Container

Build type-safe containers using generics.

```liva
struct Stack<T> {
    var items: [T]
}

impl<T> Stack<T> {
    func new() -> Stack<T> {
        return Stack { items: [] }
    }

    func push(ref mut self, item: T) {
        self.items.push(item)
    }

    func pop(ref mut self) -> T? {
        if self.items.isEmpty {
            return nil
        }
        return self.items.pop()
    }

    func peek(ref self) -> T? {
        if self.items.isEmpty {
            return nil
        }
        return self.items[self.items.length - 1]
    }

    func size(ref self) -> i64 {
        return self.items.length
    }
}

func main() {
    var intStack = Stack.new()
    intStack.push(10)
    intStack.push(20)
    intStack.push(30)

    while let val = intStack.pop() {
        println(val)
    }
    // Output: 30 20 10

    var strStack = Stack.new()
    strStack.push("hello")
    strStack.push("world")
    println(strStack.peek() ?? "empty")  // world
}
```

---

## 7. Async HTTP Requests

Fetch data from APIs using async/await.

```liva
import std::net
import std::json

async func fetchUser(id: i32) -> string {
    let url = format("https://api.example.com/users/{}", id)
    let body = await httpGet(url)
    return body
}

async func fetchMultipleUsers(ids: [i32]) -> [string] {
    var results: [string] = []
    for id in ids {
        let user = await fetchUser(id)
        results.push(user)
    }
    return results
}

func main() {
    let users = fetchMultipleUsers([1, 2, 3])
    for user in users {
        println(user)
    }
}
```

**Key points:**
- Mark functions with `async` to use `await`
- `await` suspends until the operation completes
- Async functions use LLVM coroutines under the hood

---

## 8. Channel-Based Producer/Consumer

Use channels for communication between tasks.

```liva
import std::channel
import std::task

func main() {
    let ch = channelCreate(100)
    let group = taskGroupCreate()

    // Producer
    taskGroupSpawn(group, || {
        for i in 0..10 {
            channelSend(ch, i * i)
        }
        channelClose(ch)
    })

    // Consumer
    taskGroupSpawn(group, || {
        var sum = 0
        for i in 0..10 {
            let val = channelRecv(ch)
            sum = sum + val
        }
        println("Sum of squares: \(sum)")
    })

    taskGroupAwaitAll(group)
}
```

**Key points:**
- Channels are typed and buffered
- `channelClose` signals no more values will be sent
- Use `taskGroupAwaitAll` to wait for all tasks

---

## 9. FFI with C Libraries

Call C functions from Liva using `extern "C"`.

```liva
extern "C" {
    func strlen(s: string) -> u64
    func strcmp(s1: string, s2: string) -> i32
    func printf(fmt: string, ...) -> i32
}

func main() {
    let s = "Hello, World!"
    let length = strlen(s)
    println("strlen: \(length)")

    let cmp = strcmp("abc", "abd")
    if cmp < 0 {
        println("abc comes before abd")
    }

    printf("C-style: %s has %d chars\n", s, length)
}
```

**Key points:**
- `extern "C"` declares functions with C linkage
- Use `...` for C variadic arguments (only in extern functions)
- Add `-l` flags in `liva.toml` for external libraries:

```toml
[build]
extra_flags = ["-lm", "-lpthread"]
```

---

## 10. Cross-Compilation to WASM

Compile Liva programs to WebAssembly.

### Source Code

```liva
// hello_wasm.liva
func add(a: i32, b: i32) -> i32 {
    return a + b
}

func main() {
    println("Hello from WASM!")
    println(add(40, 2))
}
```

### Compile

```bash
# Compile to WASM
livac build --target wasm32 -o hello.wasm hello_wasm.liva

# Or set target in liva.toml:
# [build]
# target = "wasm32"
```

### Other Cross-Compilation Targets

```bash
# Linux x86_64
livac build --target x86_64-linux-gnu

# Linux ARM64
livac build --target aarch64-linux-gnu

# macOS ARM64
livac build --target aarch64-apple-darwin
```

**Key points:**
- Use `--target <triple>` to cross-compile
- WASM output skips runtime library linking
- LLVM's AllTargets must be initialized for cross-compilation

---

*This cookbook covers common patterns in Liva. For a complete language reference, see [LANGUAGE-REFERENCE.md](LANGUAGE-REFERENCE.md). For the standard library API, see [API-REFERENCE.md](API-REFERENCE.md).*
