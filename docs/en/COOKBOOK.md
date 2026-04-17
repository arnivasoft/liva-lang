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
11. [Classes and Inheritance](#11-classes-and-inheritance)
12. [Async with For Await and Channels](#12-async-with-for-await-and-channels)
13. [Crypto: Hashing and HMAC](#13-crypto-hashing-and-hmac)
14. [Separate Compilation](#14-separate-compilation)

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

## 11. Classes and Inheritance

Use classes for reference-type objects with inheritance and virtual dispatch.

```liva
protocol Serializable {
    func toJSON(ref self) -> string
}

class Shape {
    var color: string

    init(color: string) {
        self.color = color
    }

    func area(ref self) -> f64 {
        return 0.0
    }
}

class Circle : Shape {
    var radius: f64

    init(radius: f64, color: string) {
        super.init(color)
        self.radius = radius
    }

    override func area(ref self) -> f64 {
        return 3.14159 * self.radius * self.radius
    }
}

class Rectangle : Shape {
    var width: f64
    var height: f64

    init(width: f64, height: f64, color: string) {
        super.init(color)
        self.width = width
        self.height = height
    }

    override func area(ref self) -> f64 {
        return self.width * self.height
    }
}

func main() {
    let shapes = [
        Circle(5.0, "red"),
        Rectangle(3.0, 4.0, "blue")
    ]
    for shape in shapes {
        println("Area: \(shape.area()), Color: \(shape.color)")
    }
}
```

**Key points:**
- Classes use heap allocation and reference semantics
- `override` is required when overriding parent methods
- `super.init(...)` calls the parent constructor
- `deinit` runs automatically when the object goes out of scope

---

## 12. Async with For Await and Channels

Combine channels and async iteration for streaming data.

```liva
import std::channel
import std::task
import std::async

func main() {
    let ch = channelCreate(100)
    let group = taskGroupCreate()

    // Producer: generate data
    taskGroupSpawn(group, || {
        for i in 0..10 {
            channelSend(ch, i * i)
        }
        channelClose(ch)
    })

    // Consumer with select
    taskGroupSpawn(group, || {
        var total = 0
        for i in 0..10 {
            let val = channelRecv(ch)
            total = total + val
            println("Received: \(val)")
        }
        println("Total: \(total)")
    })

    taskGroupAwaitAll(group)
}
```

**Key points:**
- `for await` iterates over async streams
- `taskSelect` waits for the first of multiple tasks to complete
- `withTimeout` adds a deadline to any async operation
- Channels use condition variables for efficient wakeup (no busy-waiting)

---

## 13. Crypto: Hashing and HMAC

Use the crypto module for hashing and message authentication.

```liva
import std::crypto

func main() {
    // SHA-256 hash
    let hash = sha256("hello world")
    println("SHA-256: \(hash)")

    // MD5 hash (for checksums, not security)
    let md5hash = md5("hello world")
    println("MD5: \(md5hash)")

    // HMAC-SHA256 for message authentication
    let mac = hmacSha256("my-secret-key", "important message")
    println("HMAC: \(mac)")

    // Verify: recompute and compare
    let verify = hmacSha256("my-secret-key", "important message")
    if mac == verify {
        println("Message is authentic")
    }
}
```

---

## 14. Separate Compilation

Compile individual files to object files and link them later.

```bash
# Compile each file to .o
livac --emit-obj module_a.liva
livac --emit-obj module_b.liva

# Link all object files
livac link module_a.o module_b.o -o myapp
```

```liva
// module_a.liva
pub func greet(name: string) {
    println("Hello, \(name)!")
}

// module_b.liva
import module_a

func main() {
    greet("World")
}
```

**Key points:**
- `--emit-obj` produces a `.o` object file without linking
- `livac link` links multiple object files into a final executable
- Useful for large projects where you want incremental compilation
- The `livac build` command handles this automatically with caching

---

## 14. Const Generics

```liva
// Compile-time size parameters
func repeat<const N: i32>(value: i32) -> i32 {
    return N * value
}

func fillArray<T, const SIZE: i32 = 10>(value: T) {
    // SIZE is known at compile time
    println(SIZE)
}

let result = repeat<5>(3)   // N = 5, result = 15
```

## 15. Explicit Lifetimes

```liva
// Lifetime annotations on references
func longest<'a>(x: ref 'a String, y: ref 'a String) -> ref 'a String {
    if x.length() > y.length() {
        return x
    }
    return y
}

// Multiple lifetimes
func first<'a, 'b>(x: ref 'a i32, y: ref 'b i32) -> ref 'a i32 {
    return x
}

// 'static lifetime
let global: ref 'static i32 = ref CONSTANT
```

## 16. Generator Functions (Yield)

```liva
// Generators produce values lazily with yield
func fibonacci() {
    var a = 0
    var b = 1
    while true {
        yield a        // produce value and suspend
        let tmp = a
        a = b
        b = tmp + b
    }
}
```

## 17. Enum Discriminant Values

```liva
// Explicit integer values for enum cases (useful for FFI)
enum HttpStatus {
    case OK = 200
    case NotFound = 404
    case InternalError = 500
}

enum Signal {
    case SIGINT = 2
    case SIGTERM = 15
    case SIGKILL = -9
}
```

## 18. Generic Associated Types (GATs)

```liva
// Associated types with generic parameters in protocols
protocol LendingIterator {
    type Item<'a>
    func next(mut self) -> i32
}

protocol Container {
    type Element<T>
}
```

---

## 19. Swift-Style Class Features

Recipes for the full class toolkit.

### 19.1 Static factory and counter

```liva
class Counter {
    static var total: i32

    static func make() -> Counter {
        Counter.total = Counter.total + 1
        return Counter()
    }
}
```

### 19.2 Computed properties

```liva
class Temperature {
    var celsius: f64

    init(c: f64) { self.celsius = c }

    var fahrenheit: f64 {
        get { return self.celsius * 1.8 + 32.0 }
        set { self.celsius = (newValue - 32.0) / 1.8 }
    }
}
```

### 19.3 Property observers for logging

```liva
class Volume {
    var level: i32 {
        willSet {
            println("changing from")
            println(self.level)
            println("to")
            println(newValue)
        }
        didSet { println("changed") }
    }
    init() { self.level = 0 }
}
```

### 19.4 Failable init — input validation

```liva
class Age {
    var value: i32
    init?(v: i32) {
        if v < 0 {
            return nil
        }
        self.value = v
    }
}

let a = Age(-5)     // nil
let b = Age(30)     // Age?
```

### 19.5 Designated + convenience init overloading

```liva
class Point {
    var x: i32
    var y: i32

    init(x: i32, y: i32) {
        self.x = x
        self.y = y
    }

    convenience init() {
        self.x = 0
        self.y = 0
    }
}

let a = Point(5, 10)   // designated
let b = Point()        // convenience
```

### 19.6 Lazy expensive computation

```liva
class Report {
    var rows: i32
    lazy var summary: i32 = self.rows * self.rows * 1000

    init(rows: i32) { self.rows = rows }
}

let r = Report(500)
println(r.summary)   // computed on first access
println(r.summary)   // cached second time
```

### 19.7 Subscript-based table

```liva
class Table {
    var base: i32
    init() { self.base = 0 }

    subscript(i: i32) -> i32 {
        get { return self.base + i }
        set { self.base = newValue - i }
    }
}

let t = Table()
let x = t[5]         // getter
t[10] = 100          // setter (newValue = 100)
```

### 19.8 Runtime type check with `is` / `as?`

```liva
class Shape {
    func name(ref self) -> string { return "Shape" }
}
class Circle : Shape { override func name(ref self) -> string { return "Circle" } }
class Square : Shape { override func name(ref self) -> string { return "Square" } }

func describe(s: Shape) {
    if s is Circle {
        println("it's a circle")
    }
    let sq = s as? Square
    if sq != nil {
        println(sq!.name())
    }
}
```

### 19.9 Final singleton

```liva
final class Config {
    var appName: string
    init(n: string) { self.appName = n }
}
```

### 19.10 Access levels

```liva
open class Widget { }          // subclassable
public class Sealed { }        // not subclassable
class Button : Widget { }      // OK

class Account {
    private var pin: i32        // only inside Account
    fileprivate var balance: i32 // hidden from other callers
    init() {
        self.pin = 0
        self.balance = 0
    }
}
```

### 19.11 Extension method

```liva
struct Point {
    var x: i32
    var y: i32
}

extension Point {
    func sum(self) -> i32 { return self.x + self.y }
}
```

---

*This cookbook covers common patterns in Liva as of version 1.0.0. For a complete language reference, see [LANGUAGE-REFERENCE.md](LANGUAGE-REFERENCE.md). For the standard library API, see [API-REFERENCE.md](API-REFERENCE.md).*
