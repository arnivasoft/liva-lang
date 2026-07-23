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

Conform to `Iterator` to make a type usable with `for-in`.

```liva
struct Counter {
    var n: i32
}

impl Counter: Iterator {
    type Item = i32

    func next(mut self) -> i32? {
        if self.n <= 0 {
            return nil
        }
        self.n = self.n - 1
        return self.n + 1
    }
}

func main() {
    var c = Counter { n: 3 }
    for x in c {
        println(x)
    }
    // Output: 3, 2, 1
}
```

For async iteration, implement `AsyncIterator`:

```liva
struct AsyncCounter {
    var n: i32
}

impl AsyncCounter: AsyncIterator {
    type Item = i32

    async func next(mut self) -> i32? {
        if self.n <= 0 {
            return nil
        }
        self.n = self.n - 1
        return self.n + 1
    }
}

async func main() {
    var c = AsyncCounter { n: 3 }
    for await x in c {
        println(x)
    }
    // Output: 3, 2, 1
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
    // T is inferred from the first element. An argless `Stack.new()` cannot
    // resolve T yet (see roadmap), so the stack is seeded on creation.
    func new(first: T) -> Stack<T> {
        var s = Stack { items: [] }
        s.items.push(first)
        return s
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
    var intStack = Stack.new(10)
    intStack.push(20)
    intStack.push(30)

    while let val = intStack.pop() {
        println(val)
    }
    // Output: 30 20 10

    var strStack = Stack.new("hello")
    strStack.push("world")
    println(strStack.peek() ?? "empty")  // world
}
```

> **Note (2026-07):** generic `-> T?` methods and value-returning `pop()` now work
> (both were codegen gaps, fixed). One remaining limitation: an **argless** generic
> static method (`Stack.new()` with no parameter) cannot infer `T` and miscompiles
> silently — always give the type an inference source, as `new(first: T)` does
> above. Tracked in `roadmap.md`.

---

## 7. HTTP Requests

Fetch data from REST APIs using the `http::http` module.

```liva
import http::http
import json::json

// Simple GET
func fetchUser(id: i32) -> string {
    let url = format("https://api.example.com/users/{}", id)
    let resp = HttpRequest.get(url).send()
    if resp.is2xx() {
        return resp.text()
    }
    return ""
}

// POST with JSON body
func createUser(name: string) -> bool {
    let r = HttpRequest.post("https://api.example.com/users")
        .json(format("{{\"name\":\"{}\"}}", name))
        .send()
    return r.is2xx()
}

// Reusable client with shared base URL, auth, and timeout
func fetchMultipleUsers(ids: [i32]) -> [string] {
    let client = HttpClient.withBaseUrl("https://api.example.com")
        .withHeader("Authorization", "Bearer TOKEN")
        .withTimeout(5000)
    var results: [string] = []
    for id in ids {
        let r = client.get(format("/users/{}", id))
        if r.is2xx() {
            results.push(r.text())
        }
    }
    return results
}

// Parse a JSON response — bind to a let; json() owns the DOM
func fetchAndParse() {
    let resp = HttpRequest.get("https://api.example.com/items").send()
    if resp.is2xx() {
        let doc = resp.json()                 // JsonValue owns the DOM
        let root = doc.object()
        println(root.getString("name"))
    }
}

func main() {
    let users = fetchMultipleUsers([1, 2, 3])
    for user in users {
        println(user)
    }
}
```

**Key points:**
- `HttpRequest` is a fluent immutable builder; each method returns a new value
- `send()` performs the request eagerly and returns an `HttpResponse` value (no handle)
- `HttpResponse` holds no native resource — safe to copy, return, and chain
- `resp.json()` returns a `JsonValue` that **owns its DOM**; always bind it to a `let`
- `HttpClient` bundles reusable defaults (base URL, headers, timeout)

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

## 20. SQLite: Prepared Statements

Use prepared statements with parameter binding for safe, repeatable
queries. Bind indices are 1-based, column indices are 0-based.

```liva
import sqlite::sqlite

func main() {
    if let d = SqliteDB.openMemory() {
        var db = d
        db.exec("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")

        // Re-use one prepared INSERT for several rows.
        if let p = db.prepare("INSERT INTO users (name, age) VALUES (?, ?)") {
            var ins = p
            for pair in [("Alice", 30), ("Bob", 17), ("Charlie", 25)] {
                ins.bindText(1, pair.0)
                ins.bindInt(2, pair.1 as i64)
                ins.step()
                ins.reset()
            }
            ins.finalize()
        }

        // SELECT with a bound parameter — never interpolate user input
        // into SQL strings. Even `'; DROP TABLE users; --` round-trips
        // as a literal value when bound through bindText.
        if let p = db.prepare("SELECT name, age FROM users WHERE age > ? ORDER BY age") {
            var q = p
            q.bindInt(1, 18 as i64)
            while q.step() {
                println("\(q.columnText(0)): \(q.columnInt(1))")
            }
            q.finalize()
        }
        db.close()
    }
}
```

**Key points:**
- `prepare` returns `Stmt?` — `nil` if SQL fails to compile
- `step()` returns `true` while rows are available, `false` when done
- `reset()` rewinds for re-execution; bound parameters carry over
- `bindText` uses `SQLITE_TRANSIENT` so SQLite copies the input

---

## 21. WebSocket: Real-Time Echo

Connect to a WebSocket server, send a text frame, and read the reply.

```liva
import websocket::websocket

func main() {
    var ws = WsClient.to("wss://echo.websocket.events")
        .keepAlive(30000)
        .connect()           // -> WebSocket (non-optional)
    if !ws.isOpen() {
        println("connect failed")
        return
    }
    if !ws.send("hello from liva") {
        println("send failed")
        return
    }
    if let m = ws.recv() {
        println("server said: \(m.text())")
    }
    // ws closes automatically at scope end (Drop)
}
```

**Key points:**
- `connect()` returns a **non-optional** `WebSocket`; check `isOpen()` for failure
- `recv()` returns `WsMessage?` — `nil` when the peer closes the connection
- A `WsMessage` is a view into the socket buffer; read its data before calling `recv()` again or letting the socket close
- URLs use `ws://` (port 80) or `wss://` (port 443)
- `recv()` reassembles fragmented frames automatically
- `closeWith(status, reason)` lets you send a custom close code
- `keepAlive` minimum is 15000 ms (WinHTTP); manual ping frames are not available

---

## 22. JWT: Sign and Verify

Issue and verify HMAC-signed tokens. Verification uses constant-time
HMAC comparison.

```liva
import jwt::jwt

func main() {
    let secret = "my-shared-secret"
    let payload = "{\"sub\":\"alice\",\"role\":\"admin\",\"exp\":1700000000}"

    // Sign with HS256
    let token = Jwt.signHS256(secret, payload)
    println(token.toString())

    // Verify and recover the payload
    if let verified = Jwt.verifyHS256(token.toString(), secret) {
        println("payload: \(verified)")
    } else {
        println("invalid token")
    }

    // Tamper detection: a wrong secret rejects the token
    if let _ = Jwt.verifyHS256(token.toString(), "wrong-secret") {
        println("never")
    } else {
        println("rejected as expected")
    }
}
```

**Key points:**
- HS256 uses SHA-256, HS512 uses SHA-512
- Signature segment is Base64URL-encoded (no padding)
- Verify returns the payload JSON on success, `nil` on bad signature
- Compare with `constTimeEq` to avoid timing attacks (built-in)

---

## 23. TOML: Configuration File

Parse a configuration file and read typed values.

```liva
import path::path
import toml::toml

func main() {
    let text = Path.new("config.toml").read() ?? ""
    let doc = TomlDocument.parse(text)

    if doc.isValid() {
        let host = doc.getString("server", "host") ?? "localhost"
        let port = doc.getInt("server", "port") ?? 8080 as i64
        let debug = doc.getBool("server", "debug") ?? false
        println("listening on \(host):\(port) (debug=\(debug))")
    } else {
        println("invalid TOML")
    }
    doc.free()
}
```

`config.toml`:
```toml
[server]
host = "0.0.0.0"
port = 9000
debug = true
```

**Key points:**
- `getString` / `getInt` / `getBool` return `Optional<T>` — use `??` for defaults
- `hasKey(section, key)` checks presence without reading the value
- Used internally by the package manager for `liva.toml`

---

## 24. Gzip: Compress and Decompress

Round-trip binary data through gzip. The encoder uses LZ77 + a fixed
Huffman block; the decoder accepts all three deflate block types.

```liva
import std::compress

func main() {
    let original = "hello hello hello hello hello world world world"
    let bytes: [u8] = strToBytes(original)

    // Encode (RFC 1952 gzip with LZ77 + fixed Huffman)
    let gz: [u8] = gzipEncode(bytes)
    println("\(bytes.length) bytes -> \(gz.length) bytes")

    // Decode (handles stored, fixed, and dynamic Huffman blocks)
    if let plain = gzipDecode(gz) {
        let recovered = bytesToStr(plain)
        if recovered == original {
            println("round-trip OK")
        }
    } else {
        println("decode failed — not valid gzip")
    }
}
```

**Key points:**
- Inputs and outputs are `[u8]` byte arrays — use `strToBytes` /
  `bytesToStr` to convert from/to `String`
- `gzipDecode` returns `Optional<[u8]>` — `nil` for malformed input
- The encoder gets meaningful compression on redundant data
  (1000 identical bytes compresses to <100 bytes)

---

## 25. Pattern Matching: HTTP Status Classification

Combine range, or, and `@` binding patterns to classify an HTTP status code in a single `match`.

```liva
func classify(status: i32) -> string {
    let label = match status {
        200 | 201 | 204 => "success"
        n @ 300..400 => "redirect(\(n))"
        404 => "not found"
        n @ 400..500 => "client error(\(n))"
        500..=599 => "server error"
        _ => "unknown"
    }
    return label
}

func main() {
    println(classify(200))   // success
    println(classify(301))   // redirect(301)
    println(classify(404))   // not found
    println(classify(422))   // client error(422)
    println(classify(503))   // server error
    println(classify(999))   // unknown
}
```

**Key points:**
- `200 | 201 | 204` is an or-pattern: any of the three literals matches the arm
- `n @ 300..400` binds the whole status code to `n` while restricting the match to `[300, 400)` — the range is exclusive of `400`, so `404` falls through to its own dedicated arm below
- `500..=599` is inclusive, so `599` (and not `600`) is the last status covered
- Arms are tried top-to-bottom, so a more specific arm (`404`) must come before a broader range that would otherwise also match it (`n @ 400..500`)

---

*This cookbook covers common patterns in Liva as of version 1.0.0. For a complete language reference, see [LANGUAGE-REFERENCE.md](LANGUAGE-REFERENCE.md). For the standard library API, see [API-REFERENCE.md](API-REFERENCE.md).*
