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
11. [JSON](#11-json)
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

```liva
import std::net
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `httpGet` | `(string) -> string` | HTTP GET request, returns body |
| `httpPost` | `(string, string) -> string` | HTTP POST with body, returns response |
| `httpPut` | `(string, string) -> string` | HTTP PUT with body, returns response |
| `httpDelete` | `(string) -> string` | HTTP DELETE request, returns response |

### Example

```liva
import std::net

func main() {
    let body = httpGet("https://httpbin.org/get")
    println(body)

    let response = httpPost("https://httpbin.org/post", "{\"key\": \"value\"}")
    println(response)
}
```

**Platform notes:**
- Windows: Uses WinHTTP
- Linux/macOS: Uses libcurl (must be installed)

---

## 11. JSON

```liva
import std::json
```

| Function | Signature | Description |
|----------|-----------|-------------|
| `jsonParse` | `(string) -> any` | Parse JSON string to value |
| `jsonStringify` | `(any) -> string` | Convert value to JSON string |

### Example

```liva
import std::json

func main() {
    let data = jsonParse("{\"name\": \"Alice\", \"age\": 30}")
    println(data)

    let json = jsonStringify([1, 2, 3])
    println(json)  // [1,2,3]
}
```

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

let client = HttpClient.new()
let resp = client.get("/api/users")

let client2 = HttpClient.withBaseUrl("https://api.example.com")
let r = client2.post("/data", "{\"key\": 1}")
let r2 = client2.put("/data/1", "{}")
let r3 = client2.delete("/data/1")
```

### Structs

| Struct | Methods | Description |
|--------|---------|-------------|
| `HttpClient` | `new`, `withBaseUrl`, `get`, `post`, `put`, `patch`, `delete` | HTTP client for making requests |
| `HttpResponse` | `body`, `ok` | Response from an HTTP request |
| `HttpHeaders` | `new`, `set`, `toString` | HTTP header collection |

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

let url = Url.parse("https://example.com")
let s = url.toString()

let req = Request.get("https://api.example.com")
let req2 = Request.post("https://api.example.com", "data")
```

---

*This API reference covers the Liva standard library as of version 0.3.0 with 26 modules. The library is under active development.*
