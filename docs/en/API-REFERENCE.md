# Liva Standard Library API Reference

Complete reference for all standard library modules. Functions listed as "built-in" are available globally without any `import` statement.

---

## Table of Contents

1. [Core (Built-in)](#1-core-built-in)
2. [Math](#2-math)
3. [String Operations](#3-string-operations)
4. [Array Operations](#4-array-operations)
5. [Map Operations](#5-map-operations)
6. [I/O](#6-io)
7. [OS](#7-os)
8. [Time](#8-time)
9. [Regex](#9-regex)
10. [Networking](#10-networking)
11. [JSON (json::json)](#11-json-jsonjson)
12. [Random](#12-random)
13. [Channel](#13-channel)
14. [Task](#14-task)
15. [Bench](#15-bench)
16. [Convert](#16-convert)
17. [Crypto](#17-crypto)
18. [Async](#18-async)
19. [Path](#19-path)
20. [Testing](#20-testing)
21. [UI](#21-ui)
22. [HTTP Client](#22-http-client-httphttp)
23. [Sync Primitives](#23-sync-primitives-syncsync)
24. [File System](#24-file-system-fsfs)
25. [Regex (OOP)](#25-regex-regexregex)
26. [Networking (OOP)](#26-networking-netnet)
27. [SQLite](#27-sqlite-sqlitesqlite)
28. [PostgreSQL](#28-postgresql-postgrespostgres)
29. [DB Layer](#29-db-layer-dbdb)
30. [WebSocket](#30-websocket-websocketwebsocket)
31. [JWT](#31-jwt-jwtjwt)
32. [TOML](#32-toml-tomltoml)
33. [Encoding](#33-encoding-encodingencoding)

---

## 1. Core (Built-in)

These functions are available without any import.

### Output

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `(any...) -> void` | Print values without trailing newline |
| `println` | `(any...) -> void` | Print values with trailing newline |
| `format` | `(string, any...) -> string` | Format string with `{}` placeholders |

### Type Inspection

| Function | Signature | Description |
|----------|-----------|-------------|
| `len` | `([T]) -> i64` | Length of array or string |
| `toString` | `(any) -> string` | Convert any value to string |
| `assert` | `(bool, string?) -> void` | Assert condition; panics on failure |

### Type Conversion

| Function | Signature | Description |
|----------|-----------|-------------|
| `parseInt` | `(string) -> i32` | Parse string to i32 |
| `parseInt64` | `(string) -> i64` | Parse string to i64 |
| `parseFloat` | `(string) -> f64` | Parse string to f64 |

---

## 2. Math

```liva
import std::math
```

### Basic Math

| Function | Signature | Description |
|----------|-----------|-------------|
| `abs` | `(numeric) -> numeric` | Absolute value (works with i32, i64, f32, f64) |
| `min` | `(T, T) -> T` | Minimum of two values |
| `max` | `(T, T) -> T` | Maximum of two values |

### Powers and Roots

| Function | Signature | Description |
|----------|-----------|-------------|
| `sqrt` | `(f64) -> f64` | Square root |
| `pow` | `(f64, f64) -> f64` | Exponentiation: `pow(base, exp)` |

### Rounding

| Function | Signature | Description |
|----------|-----------|-------------|
| `floor` | `(f64) -> f64` | Round down to nearest integer |
| `ceil` | `(f64) -> f64` | Round up to nearest integer |
| `round` | `(f64) -> f64` | Round to nearest integer |

### Trigonometry

| Function | Signature | Description |
|----------|-----------|-------------|
| `sin` | `(f64) -> f64` | Sine (radians) |
| `cos` | `(f64) -> f64` | Cosine (radians) |
| `tan` | `(f64) -> f64` | Tangent (radians) |

### Logarithms

| Function | Signature | Description |
|----------|-----------|-------------|
| `log` | `(f64) -> f64` | Natural logarithm (base e) |
| `log10` | `(f64) -> f64` | Base-10 logarithm |

### Example

```liva
import std::math

func main() {
    println(sqrt(16.0))       // 4.0
    println(pow(2.0, 10.0))   // 1024.0
    println(abs(-42))          // 42
    println(sin(3.14159 / 2.0)) // ~1.0
    println(floor(3.7))       // 3.0
    println(ceil(3.2))        // 4.0
}
```

---

## 3. String Operations

String methods are available on all `string` values without import.

### Properties

| Method | Returns | Description |
|--------|---------|-------------|
| `.length` | `i64` | Number of characters |

### Search

| Method | Signature | Description |
|--------|-----------|-------------|
| `.contains` | `(string) -> bool` | Check if substring exists |
| `.startsWith` | `(string) -> bool` | Check prefix |
| `.endsWith` | `(string) -> bool` | Check suffix |
| `.indexOf` | `(string) -> i64` | Index of first occurrence (-1 if not found) |

### Transform

| Method | Signature | Description |
|--------|-----------|-------------|
| `.toUpper` | `() -> string` | Convert to uppercase |
| `.toLower` | `() -> string` | Convert to lowercase |
| `.trim` | `() -> string` | Remove leading/trailing whitespace |
| `.replace` | `(string, string) -> string` | Replace all occurrences |
| `.substring` | `(i64, i64) -> string` | Extract substring by start and end index |
| `.split` | `(string) -> [string]` | Split by delimiter |

### Example

```liva
func main() {
    let s = "Hello, World!"
    println(s.length)              // 13
    println(s.contains("World"))   // true
    println(s.toUpper())           // HELLO, WORLD!
    println(s.replace("World", "Liva"))  // Hello, Liva!
    println(s.split(", "))         // ["Hello", "World!"]
    println(s.substring(0, 5))     // Hello
}
```

---

## 4. Array Operations

Array methods are available on all `[T]` values without import.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `.length` | `i64` | Number of elements |
| `.isEmpty` | `bool` | True if array has no elements |

### Mutation

| Method | Signature | Description |
|--------|-----------|-------------|
| `.push` | `(T) -> void` | Append element to end |
| `.pop` | `() -> T` | Remove and return last element |
| `.reverse` | `() -> void` | Reverse array in place |
| `.sort` | `() -> void` | Sort array in place |

### Search

| Method | Signature | Description |
|--------|-----------|-------------|
| `.contains` | `(T) -> bool` | Check if element exists |
| `.indexOf` | `(T) -> i64` | Index of first occurrence (-1 if not found) |

### Higher-Order

| Method | Signature | Description |
|--------|-----------|-------------|
| `.map` | `((T) -> U) -> [U]` | Transform each element |
| `.filter` | `((T) -> bool) -> [T]` | Keep matching elements |
| `.reduce` | `(U, (U, T) -> U) -> U` | Accumulate into single value |
| `.forEach` | `((T) -> void) -> void` | Execute function for each element |

### Example

```liva
func main() {
    var nums = [5, 3, 1, 4, 2]
    nums.sort()
    println(nums)  // [1, 2, 3, 4, 5]

    let doubled = nums.map(|x: i32| -> i32 { return x * 2 })
    println(doubled)  // [2, 4, 6, 8, 10]

    let sum = nums.reduce(0, |acc: i32, x: i32| -> i32 { return acc + x })
    println(sum)  // 15
}
```

---

## 5. Map Operations

Map methods are available on all `Map<K, V>` values without import.

### Properties

| Property | Type | Description |
|----------|------|-------------|
| `.size` | `i64` | Number of key-value pairs |
| `.isEmpty` | `bool` | True if map has no entries |

### Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `.insert` | `(K, V) -> void` | Insert or update a key-value pair |
| `.get` | `(K) -> V?` | Get value by key (returns optional) |
| `.contains` | `(K) -> bool` | Check if key exists |
| `.remove` | `(K) -> void` | Remove key-value pair |
| `.keys` | `() -> [K]` | Get all keys as array |
| `.values` | `() -> [V]` | Get all values as array |

### Example

```liva
func main() {
    var scores: Map<string, i32>
    scores.insert("Alice", 95)
    scores.insert("Bob", 87)

    if let score = scores.get("Alice") {
        println("Alice: \(score)")  // Alice: 95
    }

    for (name, score) in scores {
        println("\(name): \(score)")
    }
}
```

---

## 6. I/O

```liva
import std::io
```

### Console

| Function | Signature | Description |
|----------|-----------|-------------|
| `readLine` | `() -> string` | Read a line from stdin |

### File Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `readFile` | `(string) -> string` | Read entire file contents |
| `writeFile` | `(string, string) -> void` | Write string to file (creates/overwrites) |
| `appendFile` | `(string, string) -> void` | Append string to file |
| `fileExists` | `(string) -> bool` | Check if file exists |

### Example

```liva
import std::io

func main() {
    // Write to file
    writeFile("output.txt", "Hello, World!\n")
    appendFile("output.txt", "Second line\n")

    // Read from file
    if fileExists("output.txt") {
        let content = readFile("output.txt")
        println(content)
    }

    // Console input
    print("Enter your name: ")
    let name = readLine()
    println("Hello, \(name)!")
}
```

---

## 7. OS

```liva
import std::os
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `env` | `(string) -> string` | Get environment variable |
| `exit` | `(i32) -> void` | Exit process with code |
| `args` | `() -> [string]` | Get command-line arguments |
| `exec` | `(string) -> i32` | Execute shell command, return exit code |
| `cwd` | `() -> string` | Get current working directory |

### Example

```liva
import std::os

func main() {
    let home = env("HOME")
    println("Home: \(home)")

    let arguments = args()
    println("Arg count: \(len(arguments))")

    let code = exec("echo hello")
    println("Exit code: \(code)")
}
```

---

## 8. Time

```liva
import std::time
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `now` | `() -> f64` | Current time in seconds since epoch |
| `sleep` | `(i32) -> void` | Sleep for given milliseconds |
| `clock` | `() -> f64` | High-resolution clock (seconds) |
| `clockMs` | `() -> i64` | High-resolution clock (milliseconds) |

### Calendar Types (time::time)

```liva
import time::time
```

- `Date.parse(s) -> Date` (parses `"YYYY-MM-DD"`), `.year()/.month()/.day() -> i32`, `.toString() -> String`.
- `Time.parse(s) -> Time` (parses `"HH:MM:SS"`), `.hour()/.minute()/.second() -> i32`, `.toString() -> String`.
- `DateTime` combines both; used as a return type by the typed DB accessors.

### Example

```liva
import std::time

func main() {
    let start = clockMs()
    sleep(100)
    let elapsed = clockMs() - start
    println("Elapsed: \(elapsed)ms")
}
```

---

## 9. Regex

```liva
import std::regex
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `regexMatch` | `(string, string) -> bool` | Test if string matches pattern |
| `regexFind` | `(string, string) -> string` | Find first match |
| `regexFindAll` | `(string, string) -> [string]` | Find all matches |
| `regexReplace` | `(string, string, string) -> string` | Replace matches with string |

### Example

```liva
import std::regex

func main() {
    let text = "Order #42 has 3 items at $9.99 each"

    println(regexMatch(text, "[0-9]+"))  // true

    let first = regexFind(text, "[0-9]+")
    println(first)  // 42

    let all = regexFindAll(text, "[0-9]+\\.?[0-9]*")
    println(all)  // ["42", "3", "9.99"]

    let cleaned = regexReplace(text, "[0-9]+", "N")
    println(cleaned)  // Order #N has N items at $N.N each
}
```

---

## 10. Networking

> **Note:** The low-level `std::net` builtins (`httpGet`, `httpPost`, etc.) have been
> replaced by the `http::http` and `net::net` wrapper modules. Use those instead.

See [§22 HTTP Client (http::http)](#22-http-client-httphttp) and
[§26 Networking (net::net)](#26-networking-netnet) for the current API.

---

## 11. JSON (json::json)

```liva
import json::json
```

`json::json` is a parse-tree (DOM) JSON library. Parse a document once and navigate the in-memory tree at full speed using typed accessors, `obj["key"]`/`arr[i]` indexers, dot-path reads, and auto-vivifying path writes. Fluent building lets you construct fresh objects and arrays with a single call to `Json.object()` or `Json.array()` and then layer in values with `set*`/`add*` helpers.

### Ownership contract

The value returned by `Json.parse`, `Json.object`, or `Json.array` is the **sole owner** of the document tree and must be bound to a `let` or `var`. Every value reached from the owner — via `obj["k"]`, `arr[i]`, `.getObject()`, `.getArray()`, `.object()`, `.array()`, `.path()` — is a **borrow** that is valid only while the owner is alive. The document is freed automatically when the owner goes out of scope (the `Drop` protocol). Do **not** chain off a parse temporary (e.g. `Json.parse(s).object()`) — the temporary is the owner, it is dropped immediately, and the resulting borrow is a dangling reference (memory leak/use-after-free).

```liva
// CORRECT — owner bound to a variable
let doc = Json.parse(myJson)
let root = doc.object()          // borrow: valid for the lifetime of doc
println(root.getString("name"))

// WRONG — parse result is a temporary; owner destroyed before use
// let root = Json.parse(myJson).object()   // do NOT do this
```

### enum JsonKind

| Case | Description |
|------|-------------|
| `Null` | JSON `null` |
| `Bool` | JSON boolean |
| `Int` | JSON integer number |
| `Double` | JSON floating-point number |
| `Str` | JSON string |
| `Arr` | JSON array |
| `Obj` | JSON object |

### struct Json

Static factory — creates or parses a document. The returned value owns the document.

| Method | Signature | Description |
|--------|-----------|-------------|
| `parse` | `(s: String) -> JsonValue` | Parse a JSON string; returns the root node as the owner |
| `object` | `() -> JsonObject` | Create a new empty JSON object document |
| `array` | `() -> JsonArray` | Create a new empty JSON array document |

### struct JsonValue

A single JSON node. When `owns` is `true` it is the document owner and frees the tree on drop; otherwise it is a borrow.

**Kind checks**

| Method | Signature | Description |
|--------|-----------|-------------|
| `kind` | `() -> JsonKind` | Return the node's kind |
| `isNull` | `() -> bool` | True if the node is JSON null |
| `isBool` | `() -> bool` | True if the node is a boolean |
| `isInt` | `() -> bool` | True if the node is an integer |
| `isDouble` | `() -> bool` | True if the node is a float |
| `isString` | `() -> bool` | True if the node is a string |
| `isArray` | `() -> bool` | True if the node is an array |
| `isObject` | `() -> bool` | True if the node is an object |

**Scalar reads** (degrade to default on wrong kind: `""`, `0`, `0.0`, `false`)

| Method | Signature | Description |
|--------|-----------|-------------|
| `asString` | `() -> String` | Read as string |
| `asInt` | `() -> i64` | Read as integer |
| `asFloat` | `() -> f64` | Read as float (also accepts JSON ints) |
| `asBool` | `() -> bool` | Read as boolean |

**Navigation (borrows)**

| Method | Signature | Description |
|--------|-----------|-------------|
| `object` | `() -> JsonObject` | Reinterpret this node as an object borrow |
| `array` | `() -> JsonArray` | Reinterpret this node as an array borrow |

**Serialization**

| Method | Signature | Description |
|--------|-----------|-------------|
| `toString` | `() -> String` | Compact JSON string |
| `toStringPretty` | `(indent: i32) -> String` | Indented JSON string |

### struct JsonObject

A borrowed (or owning) handle to a JSON object node.

**Subscript indexer** — `obj["key"]` returns a `JsonValue` borrow.

**Inspection**

| Method | Signature | Description |
|--------|-----------|-------------|
| `has` | `(key: String) -> bool` | True if the key exists |
| `count` | `() -> i32` | Number of keys |
| `keys` | `() -> [String]` | All keys as a string array |

**Read — get\* (degrade to default on missing/wrong kind)**

| Method | Signature | Description |
|--------|-----------|-------------|
| `get` | `(key: String) -> JsonValue` | Raw node borrow for the key |
| `getString` | `(key: String) -> String` | String value or `""` |
| `getInt` | `(key: String) -> i64` | Integer value or `0` |
| `getFloat` | `(key: String) -> f64` | Float value or `0.0` (accepts JSON ints too) |
| `getBool` | `(key: String) -> bool` | Boolean value or `false` |
| `getObject` | `(key: String) -> JsonObject` | Object borrow for the key |
| `getArray` | `(key: String) -> JsonArray` | Array borrow for the key |

**Read — try\* (return nil on missing/wrong kind)**

| Method | Signature | Description |
|--------|-----------|-------------|
| `tryString` | `(key: String) -> String?` | String or `nil` |
| `tryInt` | `(key: String) -> i64?` | Integer or `nil` |
| `tryFloat` | `(key: String) -> f64?` | Float or `nil` (accepts JSON ints too) |
| `tryBool` | `(key: String) -> bool?` | Boolean or `nil` |

**Write**

| Method | Signature | Description |
|--------|-----------|-------------|
| `setString` | `(key: String, val: String)` | Set a string value |
| `setInt` | `(key: String, val: i64)` | Set an integer value |
| `setFloat` | `(key: String, val: f64)` | Set a float value |
| `setBool` | `(key: String, val: bool)` | Set a boolean value |
| `setNull` | `(key: String)` | Set a null value |
| `setObject` | `(key: String) -> JsonObject` | Create/replace with an empty object; returns borrow |
| `setArray` | `(key: String) -> JsonArray` | Create/replace with an empty array; returns borrow |
| `remove` | `(key: String)` | Remove a key |

**Path access**

| Method | Signature | Description |
|--------|-----------|-------------|
| `path` | `(p: String) -> JsonValue` | Read by dot-separated path, e.g. `"user.name"` |
| `setPathString` | `(p: String, val: String)` | Write by path; auto-creates intermediate objects |
| `setPathInt` | `(p: String, val: i64)` | Write integer by path |
| `setPathFloat` | `(p: String, val: f64)` | Write float by path |
| `setPathBool` | `(p: String, val: bool)` | Write boolean by path |

**Serialization**

| Method | Signature | Description |
|--------|-----------|-------------|
| `toString` | `() -> String` | Compact JSON string |
| `toStringPretty` | `(indent: i32) -> String` | Indented JSON string |

### struct JsonArray

A borrowed (or owning) handle to a JSON array node.

**Subscript indexer** — `arr[i]` (where `i: i64`) returns a `JsonValue` borrow.

**Inspection**

| Method | Signature | Description |
|--------|-----------|-------------|
| `count` | `() -> i32` | Number of elements |
| `length` | `() -> i32` | Alias for `count` |

**Read**

| Method | Signature | Description |
|--------|-----------|-------------|
| `at` | `(i: i64) -> JsonValue` | Raw node borrow at index |
| `getString` | `(i: i64) -> String` | String at index or `""` |
| `getInt` | `(i: i64) -> i64` | Integer at index or `0` |
| `getFloat` | `(i: i64) -> f64` | Float at index or `0.0` |
| `getBool` | `(i: i64) -> bool` | Boolean at index or `false` |
| `getObject` | `(i: i64) -> JsonObject` | Object borrow at index |
| `getArray` | `(i: i64) -> JsonArray` | Array borrow at index |

**Write**

| Method | Signature | Description |
|--------|-----------|-------------|
| `addString` | `(val: String)` | Append a string element |
| `addInt` | `(val: i64)` | Append an integer element |
| `addFloat` | `(val: f64)` | Append a float element |
| `addBool` | `(val: bool)` | Append a boolean element |
| `addNull` | `()` | Append a null element |
| `addObject` | `() -> JsonObject` | Append an empty object; returns borrow |
| `addArray` | `() -> JsonArray` | Append an empty array; returns borrow |

**Serialization**

| Method | Signature | Description |
|--------|-----------|-------------|
| `toString` | `() -> String` | Compact JSON string |
| `toStringPretty` | `(indent: i32) -> String` | Indented JSON string |

### Degrade-to-default vs. try\* semantics

`getString`/`getInt`/`getFloat`/`getBool` on `JsonObject` and the equivalent indexed reads on `JsonArray` always return a value — `""`, `0`, `0.0`, or `false` when the key is absent or the node has the wrong kind. Use the `try*` variants (`tryString`, `tryInt`, `tryFloat`, `tryBool`) when you need to distinguish "key absent" from "key present with default value". `asFloat`/`tryFloat` also accept JSON integer nodes (they convert automatically).

### Example

```liva
import json::json

func main() {
    // Parse and read
    let doc = Json.parse("{\"user\":{\"name\":\"liva\",\"age\":3},\"tags\":[\"a\",\"b\"]}")
    let root = doc.object()
    println(root.path("user.name").asString())        // liva
    println(root.getObject("user").getInt("age"))      // 3
    println(root.getArray("tags").getString(0 as i64)) // a
    println(root["user"]["name"].asString())           // liva (indexer)

    // Build and serialize
    var out = Json.object()
    out.setString("name", "yeni")
    var tags = out.setArray("tags")
    tags.addString("x")
    out.setPathInt("meta.count", 1)
    println(out.toString())
}
```

### v1 limitations

- Strict JSON only: comments and trailing commas are parse errors.
- `setPath*` auto-creates intermediate nodes as **objects**; numeric path segments (e.g. `"items.0.name"`) are treated as object keys, not array indices.
- `\uXXXX` surrogate pairs in string literals are not combined into a single code point.
- Subscript assignment (`obj["k"] = v`) is not supported; use `setString`, `setInt`, etc.

---

## 12. Random

```liva
import std::random
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `random` | `() -> f64` | Random float in [0.0, 1.0) |
| `randInt` | `(i32, i32) -> i32` | Random integer in [min, max] |
| `randFloat` | `() -> f64` | Alias for `random()` |
| `randomChoice` | `([T]) -> T` | Random element from array |

### Example

```liva
import std::random

func main() {
    let dice = randInt(1, 6)
    println("Dice: \(dice)")

    let coin = randomChoice(["heads", "tails"])
    println("Coin: \(coin)")

    let probability = random()
    println("Random: \(probability)")
}
```

---

## 13. Channel

```liva
import std::channel
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `channelCreate` | `(i32) -> Channel` | Create buffered channel with given capacity |
| `channelSend` | `(Channel, any) -> void` | Send a value into the channel |
| `channelRecv` | `(Channel) -> any` | Receive a value from the channel (blocks if empty) |
| `channelClose` | `(Channel) -> void` | Close the channel (no more sends allowed) |

### Example

```liva
import std::channel

func main() {
    let ch = channelCreate(10)

    channelSend(ch, "hello")
    channelSend(ch, "world")

    println(channelRecv(ch))  // hello
    println(channelRecv(ch))  // world

    channelClose(ch)
}
```

### Properties

- **Buffered**: Channels have a fixed capacity; sends block when full
- **Ring buffer**: Internal implementation uses a ring buffer
- **Thread-safe**: Uses spin-wait synchronization

---

## 14. Task

```liva
import std::task
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `taskGroupCreate` | `() -> TaskGroup` | Create a new task group |
| `taskGroupSpawn` | `(TaskGroup, () -> void) -> void` | Spawn a task in the group |
| `taskGroupAwaitAll` | `(TaskGroup) -> void` | Wait for all tasks to complete |
| `taskGroupCancelAll` | `(TaskGroup) -> void` | Cancel all running tasks |

### Example

```liva
import std::task

func main() {
    let group = taskGroupCreate()

    taskGroupSpawn(group, || {
        println("Task A")
    })

    taskGroupSpawn(group, || {
        println("Task B")
    })

    taskGroupAwaitAll(group)
    println("All tasks done")
}
```

---

## 15. Bench

```liva
import std::bench
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `benchStart` | `(string) -> void` | Start a named benchmark timer |
| `benchIter` | `(string) -> void` | Record one iteration of a benchmark |
| `benchDone` | `(string) -> void` | End a named benchmark |
| `benchReport` | `() -> void` | Print results for all benchmarks |
| `benchReset` | `() -> void` | Reset all benchmark data |

### Example

```liva
import std::bench

func main() {
    benchStart("sort")
    for i in 0..100 {
        benchIter("sort")
        var arr = [5, 3, 1, 4, 2]
        arr.sort()
    }
    benchDone("sort")

    benchReport()
    // Output: sort: 100 iterations, avg 0.005ms, total 0.5ms
}
```

---

## 16. Convert

```liva
import std::convert
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `toString` | `(any) -> string` | Convert value to string representation |
| `parseInt` | `(string) -> i32` | Parse string to 32-bit integer |
| `parseInt64` | `(string) -> i64` | Parse string to 64-bit integer |
| `parseFloat` | `(string) -> f64` | Parse string to 64-bit float |
| `toBool` | `(string) -> bool` | Flexible boolean parse: `1/t/true/yes/on` (any case) → `true`; anything else → `false` (convert::convert) |

### Example

```liva
import std::convert

func main() {
    let s = toString(42)
    println(s)              // "42"

    let n = parseInt("123")
    println(n + 1)          // 124

    let f = parseFloat("3.14")
    println(f * 2.0)        // 6.28
}
```

---

## 17. Crypto

```liva
import std::crypto
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `sha256` | `(string) -> string` | SHA-256 hash of input string |
| `md5` | `(string) -> string` | MD5 hash of input string |
| `hmacSha256` | `(string, string) -> string` | HMAC-SHA256 with key and message |

### Example

```liva
import std::crypto

func main() {
    let hash = sha256("hello world")
    println(hash)  // b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9

    let mac = hmacSha256("secret-key", "message")
    println(mac)
}
```

---

## 18. Async

```liva
import std::async
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `taskSelect` | `([Task]) -> Task` | Wait for first task to complete |
| `withTimeout` | `(Task, i64) -> Task` | Run task with timeout (ms) |
| `schedulerInit` | `(i32) -> void` | Initialize thread pool with N workers |
| `schedulerShutdown` | `() -> void` | Shut down the thread pool |
| `schedulerWorkerCount` | `() -> i32` | Get number of worker threads |
| `asyncFileRead` | `(string) -> string` | Async file read |
| `asyncFileWrite` | `(string, string) -> void` | Async file write |

### For Await

Iterate over async streams:

```liva
import std::async

async func processItems() {
    for await item in asyncStream {
        println(item)
    }
}
```

---

## 19. Path

```liva
import std::path
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `pathJoin` | `(string, string) -> string` | Join two path segments |
| `pathExtension` | `(string) -> string` | Get file extension |
| `pathBasename` | `(string) -> string` | Get filename from path |
| `pathDirname` | `(string) -> string` | Get directory from path |
| `pathIsAbsolute` | `(string) -> bool` | Check if path is absolute |

---

## 20. Testing

```liva
import std::testing
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `assertEqual` | `(any, any) -> void` | Assert two values are equal |
| `assertNotEqual` | `(any, any) -> void` | Assert two values differ |
| `assertTrue` | `(bool) -> void` | Assert condition is true |
| `assertFalse` | `(bool) -> void` | Assert condition is false |

---

## 21. UI

```liva
import std::ui
```

The UI module provides a raylib-based widget system with 12 development phases:

- **Canvas**: Window management, drawing primitives (rect, circle, line, text)
- **Widgets**: Button, Label, TextInput, Checkbox, Slider, ScrollView, ProgressBar, RadioGroup, TabView, Dropdown, Dialog, TextArea, Tooltip, Popover
- **Layout**: VStack, HStack, Grid, Aligned/Spaced/Padded containers
- **Theming**: Theme struct with dark/light presets, themed widget factories
- **Animation**: Easing functions, Tween, ColorTransition, HoverAnimator
- **Focus**: FocusManager (Tab/Arrow navigation), FocusRing, KeyAction shortcuts
- **Events**: Callback fields (onClick, onToggle, onValueChange)

### Example

```liva
import std::ui

func main() {
    initWindow(800, 600, "My App")
    let theme = Theme.dark()

    while !windowShouldClose() {
        beginDrawing()
        clearBackground(theme.background)

        let btn = Button.themed(theme, 100.0, 50.0, 200.0, 40.0, "Click Me")
        btn.draw()

        endDrawing()
    }
    closeWindow()
}
```

---

## 22. HTTP Client (http::http)

```liva
import http::http
import json::json

// One-shot requests via HttpRequest fluent builder
let resp = HttpRequest.get("https://api.example.com/users")
    .header("Authorization", "Bearer TOKEN")
    .query("page", "2")
    .timeout(5000)
    .send()                        // -> HttpResponse (eager-copy; holds no native handle)

if resp.is2xx() {
    let body = resp.text()
    let ct = resp.header("Content-Type")   // -> String?
    let doc = resp.json()                  // -> JsonValue (bind to let; owns its DOM)
}

// POST with a JSON body
let r2 = HttpRequest.post("https://api.example.com/users")
    .json("{\"name\":\"alice\"}")
    .send()

// Reusable HttpClient with default base URL, timeout, and headers
let client = HttpClient.withBaseUrl("https://api.example.com")
    .withTimeout(5000)
    .withHeader("Authorization", "Bearer TOKEN")
let r3 = client.get("/users")                 // -> HttpResponse
let r4 = client.post("/users", "{}")          // -> HttpResponse
let r5 = client.request("GET", "/items").query("page", "1").send()
```

### Ownership / lifecycle

`HttpResponse` is an **eager-copy value**: `send()` reads the status, body, and headers
from the native layer immediately, then closes the connection. The returned
`HttpResponse` holds no native handle and is safe to copy, return, or chain without
worrying about lifetimes or `Drop`.

`resp.json()` parses the response body and returns a `JsonValue` that **owns its DOM**.
Bind it to a `let` before calling methods on it — do not chain off the call expression.

### struct HttpRequest

Static constructors (all return an `HttpRequest` builder):

| Method | Description |
|--------|-------------|
| `get(url: String) -> HttpRequest` | Start a GET request |
| `post(url: String) -> HttpRequest` | Start a POST request |
| `put(url: String) -> HttpRequest` | Start a PUT request |
| `patch(url: String) -> HttpRequest` | Start a PATCH request |
| `delete(url: String) -> HttpRequest` | Start a DELETE request |

Builder methods (each returns a new `HttpRequest`):

| Method | Description |
|--------|-------------|
| `header(name, value) -> HttpRequest` | Add a request header |
| `query(key, value) -> HttpRequest` | Append a query parameter (URL-encoded) |
| `body(content: String) -> HttpRequest` | Set the raw request body |
| `json(content: String) -> HttpRequest` | Set body and add `Content-Type: application/json` |
| `timeout(ms: i64) -> HttpRequest` | Set timeout in milliseconds (default 30 000) |
| `send() -> HttpResponse` | Execute the request and return an eager-copy response |

### struct HttpResponse

| Method | Description |
|--------|-------------|
| `statusCode() -> i32` | HTTP status code (0 on network failure) |
| `text() -> String` | Response body as a string |
| `header(name: String) -> String?` | Case-insensitive header lookup; `nil` if absent |
| `json() -> JsonValue` | Parse body as JSON — bind the result to a `let` |
| `isOk() -> bool` | `true` if `200 ≤ statusCode < 300` (synonym for `is2xx()`) |
| `is2xx() -> bool` | `true` if `200 ≤ statusCode < 300` |
| `is3xx() -> bool` | `true` if `300 ≤ statusCode < 400` |
| `is4xx() -> bool` | `true` if `400 ≤ statusCode < 500` |
| `is5xx() -> bool` | `true` if `500 ≤ statusCode < 600` |

### struct HttpClient

| Method | Description |
|--------|-------------|
| `new() -> HttpClient` | Create a client with no base URL (empty defaults) |
| `withBaseUrl(url: String) -> HttpClient` | Create a client with a base URL |
| `withTimeout(ms: i64) -> HttpClient` | Set default timeout |
| `withHeader(name, value) -> HttpClient` | Add a default header applied to every request |
| `get(path: String) -> HttpResponse` | GET `baseUrl + path` |
| `post(path: String, body: String) -> HttpResponse` | POST `baseUrl + path` with body |
| `put(path: String, body: String) -> HttpResponse` | PUT convenience |
| `patch(path: String, body: String) -> HttpResponse` | PATCH convenience |
| `delete(path: String) -> HttpResponse` | DELETE convenience |
| `request(method, path) -> HttpRequest` | Seed a builder with base URL + defaults |

---

## 23. Sync Primitives (sync::sync)

```liva
import sync::sync

var m = Mutex.new()
m.lock()
m.unlock()
m.free()

var a = AtomicI64.new(0)
a.store(42)
let v = a.load()
a.add(1)
a.free()

var ch = Channel.new(10)
ch.send(42)
let val = ch.receive()
ch.free()

var g = TaskGroup.new()
g.awaitAll()
g.free()
```

---

## 24. File System (fs::fs)

```liva
import fs::fs

let info = FileInfo.new("/path/to/file")
let name = info.name()
let ext = info.extension()
let parent = info.parent()
let exists = info.exists()

let dir = Dir.new("/path")
let files = dir.list()
```

---

## 25. Regex (regex::regex)

```liva
import regex::regex

let re = Regex.new("[0-9]+")
let matched = re.isMatch("hello 123")
let found = re.find("abc 42 def")
let all = re.findAll("1 and 2 and 3")
let replaced = re.replace("abc123", "NUM")
let groups = re.groups("(hello) (world)")
```

---

## 26. Networking (net::net)

```liva
import net::net

// Parse a URL and read its components
let u = Url.parse("https://api.example.com:8080/path?page=2#top")
// u.scheme -> "https", u.host -> "api.example.com", u.port -> 8080
// u.path -> "/path", u.query -> "page=2", u.fragment -> "top"

// Immutable builder methods
let u2 = u.withQuery("q", "hello world").withPath("/v2")
let s  = u2.toString()

// Percent-encoding helpers
let enc = Url.encode("a b&c")    // -> "a%20b%26c"
let dec = Url.decode(enc)        // -> String?  (nil if malformed)
```

### struct Url

| Method | Description |
|--------|-------------|
| `parse(s: String) -> Url` | Parse a URL string into its components |
| `encode(s: String) -> String` | Percent-encode a string |
| `decode(s: String) -> String?` | Decode a percent-encoded string; `nil` on error |
| `toString() -> String` | Reconstruct the URL as a string |
| `withScheme(v: String) -> Url` | Return a new Url with the scheme replaced |
| `withHost(v: String) -> Url` | Return a new Url with the host replaced |
| `withPort(v: i32) -> Url` | Return a new Url with the port replaced |
| `withPath(v: String) -> Url` | Return a new Url with the path replaced |
| `withQuery(key, value: String) -> Url` | Append a query parameter (url-encoded; joined with `&` if a query already exists) |
| `withFragment(v: String) -> Url` | Return a new Url with the fragment replaced |

Fields: `scheme: String`, `host: String`, `port: i32`, `path: String`,
`query: String`, `fragment: String`.

---

## 27. SQLite (sqlite::sqlite)

Embedded SQL database. On Windows 10+ the runtime dynamically loads
`winsqlite3.dll`; on platforms without a system SQLite, every entry
point fails closed (`open` returns `nil`, `exec` returns `false`,
queries return `nil`/empty).

```liva
import sqlite::sqlite

if let d = SqliteDB.openMemory() {
    var db = d
    db.exec("CREATE TABLE u (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")

    // Prepared statement with parameter binding (1-indexed).
    if let p = db.prepare("INSERT INTO u (name, age) VALUES (?, ?)") {
        var ins = p
        ins.bindText(1, "Alice")
        ins.bindInt(2, 30 as i64)
        ins.step()
        ins.finalize()
    }

    // Iterate result rows (column indices are 0-based).
    if let p = db.prepare("SELECT name, age FROM u WHERE age > ?") {
        var q = p
        q.bindInt(1, 18 as i64)
        while q.step() {
            println(q.columnText(0))
            println(q.columnInt(1))
        }
        q.finalize()
    }
    db.close()
}
```

### Structs

| Struct | Methods | Description |
|--------|---------|-------------|
| `SqliteDB` | `open`, `openMemory`, `exec`, `queryString`, `queryInt`, `queryColumn`, `prepare`, `lastInsertId`, `changes`, `errorMessage`, `close` | A single database connection (use `":memory:"` or a file path) |
| `Stmt` | `bindText`, `bindInt`, `bindDouble`, `bindNull`, `step`, `reset`, `columnCount`, `columnText`, `columnInt`, `columnDouble`, `finalize` | Compiled SQL statement with parameter binding and row iteration |

`step()` returns `true` if a row is available, `false` once the
statement is done or hit an error. Bind indices are **1-based**;
column indices are **0-based** (matching the SQLite C API).
`bindText` uses `SQLITE_TRANSIENT` internally, so SQLite copies the
input — Liva strings can move or be freed without breaking the
prepared statement. This makes prepared statements safe against SQL
injection even with values containing `'; DROP TABLE; --`.

### New SQLite methods

- `Stmt.columnName(col) -> String` — result column name.
- `Stmt.columnType(col) -> i32` — 1=INTEGER, 2=FLOAT, 3=TEXT, 4=BLOB, 5=NULL.
- `Stmt.columnIsNull(col) -> bool` — true when the cell is NULL.
- `Stmt.bindByName(name, val) -> bool` — bind text to `:name`/`@name`/`$name`.
- `Stmt.bindBlob(idx, [u8]) -> bool`, `Stmt.columnBlob(col) -> [u8]` — binary data.
- `SqliteDB.begin()/commit()/rollback() -> bool` — transaction control.
- `Stmt.columnBool(col) -> bool`, `columnDate(col) -> Date`, `columnTime(col) -> Time`, `columnDateTime(col) -> DateTime` — typed column reads parsed from the cell text (`columnDouble` already exists).

Typed accessors are non-optional and return a default (`0.0`/`false`/epoch) on unparseable text; `columnBool` accepts `1/t/true/yes/on` (any case).

---

## 28. PostgreSQL (postgres::postgres)

libpq-backed client, resolved dynamically at runtime. If libpq or a server is
absent, `PgConn.open` returns `nil`, `exec` returns `false`, queries return `nil`
(fail-closed). On Windows the loader probes `C:\Program Files\PostgreSQL\<ver>\bin\libpq.dll`
(newest first) and `$LIVA_LIBPQ_PATH`.

- `PgConn.open(connString) -> PgConn?` — `"host=... dbname=... user=..."`.
- `PgConn.exec(sql) -> bool` — no-result command.
- `PgConn.query(sql) -> PgResult?` / `queryParams(sql, [String]) -> PgResult?`.
- `PgConn.errorMessage() -> String`, `close()`.
- `PgResult.rowCount()/colCount() -> i32`, `getText(r,c)/getInt(r,c)`,
  `isNull(r,c) -> bool`, `columnName(c) -> String`, `clear()`.
  Plus typed reads: `getDouble(r,c) -> f64`, `getBool(r,c) -> bool`, `getDate(r,c) -> Date`, `getTime(r,c) -> Time`, `getDateTime(r,c) -> DateTime`.

---

## 29. DB Layer (db::db)

Driver-agnostic layer. Write `?` placeholders everywhere; the PostgreSQL
adapter rewrites them to `$1,$2,...`. The rewriter skips `?` inside single-quoted
string literals (`'...'`, `''` escape), line comments (`-- ...`), block comments
(`/* ... */`), and dollar-quoted strings (`$$...$$`, `$tag$...$tag$`); a `$`
followed by a digit (`$1`) is left as-is.

- `protocol Database { exec; query(sql, [String]) -> [Row]; lastInsertId;
  errorMessage; close }`
- `SqliteDatabase.open(path)? / openMemory()?` — `impl Database`.
- `PgDatabase.open(connString)?` — `impl Database` (`lastInsertId` returns 0;
  use `RETURNING`).
- `Row.getText(col)/getInt(col)/isNull(col)/byName(name) -> String?`.
  Plus typed reads: `getDouble(col) -> f64`, `getBool(col) -> bool`, `getDate(col) -> Date`, `getTime(col) -> Time`, `getDateTime(col) -> DateTime`.
- Use `dyn Database` for code that works across both drivers.

Typed accessors are non-optional and return a default (`0.0`/`false`/epoch) on unparseable text; `getBool` accepts `1/t/true/yes/on` (any case).

---

## 30. WebSocket (websocket::websocket)

Full-featured WebSocket client backed by WinHTTP on Windows. Supports
text and binary frames, custom headers, subprotocol negotiation,
WinHTTP auto-keepalive, transparent auto-reconnect, and JSON
integration. Non-Windows builds return a closed socket stub.

> **Migration note:** The previous API returned `WebSocket?` from
> `connect` and `String?` from `recv`. Both are now changed — see
> below.

```liva
import websocket::websocket
import json::json

var c = WsClient.to("wss://example.com/socket")
    .header("Authorization", "Bearer TOKEN")
    .subprotocol("chat")
    .keepAlive(30000)        // WinHTTP auto-keepalive (min 15000 ms)
    .autoReconnect(3, 1000)  // 3 retries, 1 s backoff
    .connect()               // -> WebSocket (non-optional); check isOpen()

if c.isOpen() {
    c.send("hello")                          // text frame -> bool
    let bin: [u8] = [1 as u8, 2 as u8, 3 as u8]
    c.sendBinary(bin)                        // binary frame -> bool
    if let m = c.recv() {                    // recv() -> WsMessage?
        if m.isText() {
            let doc = m.json()               // JsonValue — bind it (owns its DOM)
        }
        if m.isBinary() {
            let b = m.bytes()                // [u8]
        }
    }
    // closes automatically at scope end (Drop); or c.close()
}
```

### Structs

#### `WsClient` — fluent connect builder

| Method | Signature | Description |
|--------|-----------|-------------|
| `to` | `static to(url: String) -> WsClient` | Start building a connection to `url` |
| `header` | `header(name: String, value: String) -> WsClient` | Add a custom HTTP upgrade header |
| `subprotocol` | `subprotocol(proto: String) -> WsClient` | Set `Sec-WebSocket-Protocol` |
| `keepAlive` | `keepAlive(ms: i64) -> WsClient` | WinHTTP auto-keepalive interval (min 15000 ms; lower values are clamped to 15000) |
| `autoReconnect` | `autoReconnect(maxRetries: i32, backoffMs: i64) -> WsClient` | Transparent reconnect on disconnect |
| `connect` | `connect() -> WebSocket` | Open the connection; returns a **non-optional** `WebSocket` |

#### `WebSocket` — the connection

`connect()` and `WebSocket.connect(url)` both return a **non-optional
`WebSocket`**. Check `isOpen()` to detect connection failure — do NOT
use `if let`. The return type is non-optional so `Drop` auto-close
works unconditionally. **Do not copy a `WebSocket` value** (`let b =
ws`) — it is a single-owner handle; use it only through its methods.

| Method | Signature | Description |
|--------|-----------|-------------|
| `connect` | `static connect(url: String) -> WebSocket` | Quick connect (no builder) |
| `isOpen` | `isOpen() -> bool` | `true` if the connection is established |
| `send` | `send(text: String) -> bool` | Send a UTF-8 text frame |
| `sendBinary` | `sendBinary(data: [u8]) -> bool` | Send a binary frame |
| `sendJson` | `sendJson(json: JsonValue) -> bool` | Serialize `json` and send as a text frame |
| `recv` | `recv() -> WsMessage?` | Receive next message; `nil` when the peer closes |
| `reconnect` | `reconnect() -> bool` | Attempt manual reconnect |
| `close` | `close()` | Send close code 1000 |
| `closeWith` | `closeWith(status: i32, reason: String)` | Send a custom close code and reason |

`Drop` auto-closes the connection at scope end; explicit `close()` is
optional.

#### `WsMessage` — received message view

`recv()` returns a `WsMessage?`. A `WsMessage` is a **view into the
socket's last-received buffer** — it is valid only until the next
`recv()` call **or** until the socket is closed or dropped. Read all
data out of the message before calling `recv()` again or letting the
socket go out of scope. Retaining a `WsMessage` past those points is a
use-after-free.

| Method | Signature | Description |
|--------|-----------|-------------|
| `isText` | `isText() -> bool` | `true` for a UTF-8 text frame |
| `isBinary` | `isBinary() -> bool` | `true` for a binary frame |
| `text` | `text() -> String` | Frame payload as a UTF-8 string |
| `bytes` | `bytes() -> [u8]` | Frame payload as raw bytes |
| `json` | `json() -> JsonValue` | Parse the text payload as JSON (bind the result — it owns the DOM) |

`recv()` automatically reassembles fragmented messages until a final
frame arrives. Manual WS ping frames are not available via WinHTTP;
use `keepAlive` for heartbeating (minimum interval 15000 ms).

---

## 31. JWT (jwt::jwt)

JSON Web Tokens with HMAC-SHA256 (HS256) and HMAC-SHA512 (HS512).
Verify uses constant-time HMAC comparison via runtime `constTimeEq`.

```liva
import jwt::jwt

let token = Jwt.signHS256("my-secret", "{\"user\":\"alice\",\"exp\":1700000000}")
println(token.toString())

if let payload = Jwt.verifyHS256(token.toString(), "my-secret") {
    println(payload)
} else {
    println("invalid signature")
}
```

### Structs

| Struct | Methods | Description |
|--------|---------|-------------|
| `Jwt` | `signHS256`, `signHS512`, `toString`, `verifyHS256`, `verifyHS512` | Signed JWT token with header.payload.signature segments |

The free functions `jwtBuildHS256`, `jwtBuildHS512`, `jwtVerifyHS256`,
`jwtVerifyHS512` are also exported for use without the `Jwt` wrapper.
Verify returns the decoded payload JSON on success, `nil` on
signature/structure failure.

---

## 32. TOML (toml::toml)

TOML 1.0 parser with optional accessors. Returns a document that you
query by section and key.

```liva
import toml::toml

let doc = TomlDocument.parse("[server]\nhost = \"localhost\"\nport = 8080")
if doc.isValid() {
    if let host = doc.getString("server", "host") {
        println(host)
    }
    if let port = doc.getInt("server", "port") {
        println(port)
    }
    if doc.hasKey("server", "tls") {
        if let tls = doc.getBool("server", "tls") {
            println(tls)
        }
    }
}
```

### Structs

| Struct | Methods | Description |
|--------|---------|-------------|
| `TomlDocument` | `parse`, `isValid`, `getString`, `getInt`, `getBool`, `hasKey`, `free` | Parsed TOML document |

Use `getString` / `getInt` / `getBool` to read values from a section.
A missing section/key returns `nil`. The package manager itself uses
this module for `liva.toml`.

---

## 33. Encoding (encoding::encoding)

Text encodings (Base64, Base64URL, Hex, URL percent-encoding) and
RFC 1952 gzip compression with the encoder running LZ77 + a fixed
Huffman block.

```liva
import encoding::encoding

let b64 = toBase64("hello")
let back = fromBase64(b64)

let url = toUrl("hello world & friends")
let dec = fromUrl(url)

let hex = toHex("Hi")
let bin = fromHex(hex)

let urlSafe = toBase64Url("subjects?")

// gzip / gunzip via runtime built-ins
let bytes: [u8] = strToBytes("hello hello hello hello hello")
let gz: [u8] = gzipEncode(bytes)
if let plain = gzipDecode(gz) {
    println(bytesToStr(plain))
}
```

### Structs

| Struct | Methods | Description |
|--------|---------|-------------|
| `Base64` | `encode`, `fromEncoded`, `decode`, `toString` | Base64 (RFC 4648) wrapper |
| `Base64Url` | `encode`, `fromEncoded`, `decode`, `toString` | URL-safe Base64 (no padding) |
| `Hex` | `encode`, `fromEncoded`, `decode`, `toString` | Lowercase hex |
| `Url` | `encode`, `fromEncoded`, `decode`, `toString` | URL percent-encoding |

### Free functions

| Function | Returns | Description |
|----------|---------|-------------|
| `toBase64`, `fromBase64` | `String` / `String?` | Base64 round-trip |
| `toBase64Url`, `fromBase64Url` | `String` / `String?` | Base64URL (no padding) — used by JWT |
| `toHex`, `fromHex` | `String` / `String?` | Hex round-trip |
| `toUrl`, `fromUrl` | `String` / `String?` | Percent-encoding round-trip |
| `checksum` | `i64` | CRC32 |
| `gzipEncode(data: [u8]) -> [u8]` | `[u8]` | RFC 1952 gzip with LZ77 + fixed Huffman |
| `gzipDecode(data: [u8]) -> [u8]?` | `[u8]?` | gunzip; supports stored, fixed, and dynamic Huffman blocks; nil on bad input |

For binary data with embedded NUL bytes, use the `[u8]` (byte array)
variants: `hexEncodeBytes` / `hexDecodeBytes` /
`base64UrlEncodeBytes` / `base64UrlDecodeBytes` / `strToBytes` /
`bytesToStr`. The string-based encoders truncate at the first NUL.

---

*This API reference covers the Liva standard library as of version 0.3.0 with 31 modules. The library is under active development.*
