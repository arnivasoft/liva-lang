# Liva Language Reference

A comprehensive reference for the Liva programming language — a statically typed, compiled language with Swift-like syntax and Rust-style ownership semantics.

---

## Table of Contents

1. [Lexical Structure](#1-lexical-structure)
2. [Types](#2-types)
3. [Variables and Constants](#3-variables-and-constants)
4. [Operators](#4-operators)
5. [Control Flow](#5-control-flow)
6. [Functions](#6-functions)
7. [Closures](#7-closures)
8. [Structs](#8-structs)
9. [Enums and Pattern Matching](#9-enums-and-pattern-matching)
10. [Protocols (Traits)](#10-protocols-traits)
11. [Generics](#11-generics)
12. [Ownership and Borrowing](#12-ownership-and-borrowing)
13. [Optional Types](#13-optional-types)
14. [Error Handling](#14-error-handling)
15. [Collections](#15-collections)
16. [Tuples](#16-tuples)
17. [Type Aliases](#17-type-aliases)
18. [String Operations](#18-string-operations)
19. [Modules and Imports](#19-modules-and-imports)
20. [Async/Await](#20-asyncawait)
21. [Operator Overloading](#21-operator-overloading)
22. [Custom Iterators](#22-custom-iterators)
23. [Drop Trait (Destructors)](#23-drop-trait-destructors)
24. [Standard Library](#24-standard-library)
25. [Built-in Functions](#25-built-in-functions)
26. [Project Configuration](#26-project-configuration)
27. [Compiler Options](#27-compiler-options)

---

## 1. Lexical Structure

### Comments

```liva
// Single-line comment

/* Block comment
   can span multiple lines */
```

### Identifiers

Identifiers start with a letter or underscore, followed by letters, digits, or underscores. Keywords cannot be used as identifiers.

### Keywords

**Control Flow:**
`if`, `else`, `while`, `for`, `in`, `match`, `case`, `break`, `continue`, `return`, `guard`

**Declarations:**
`func`, `let`, `var`, `const`, `struct`, `enum`, `impl`, `protocol`, `type`, `import`, `pub`

**Memory:**
`ref`, `mut`, `self`

**Values:**
`true`, `false`, `nil`

**Modifiers:**
`as`, `where`, `async`, `await`, `try`

### Literals

#### Integer Literals

```liva
let decimal = 42
let hex = 0xFF
let binary = 0b1010
let octal = 0o77
let negative = -10
```

#### Float Literals

```liva
let pi = 3.14159
let scientific = 1.5e10
let small = 2.5e-3
```

#### String Literals

```liva
let simple = "Hello, World!"
let escaped = "line1\nline2\ttab"
let unicode = "emoji: \u{1F600}"
let interpolated = "value is \(x + 1)"
```

**Escape sequences:** `\\`, `\"`, `\n`, `\r`, `\t`, `\0`, `\u{XXXX}`

**String interpolation:** `\(expression)` embeds any expression inside a string.

#### Multi-line Strings

```liva
let text = "first line
second line
third line"
```

#### Character Literals

```liva
let ch = 'A'
```

#### Boolean Literals

```liva
let yes = true
let no = false
```

---

## 2. Types

### Primitive Types

| Type | Description | Size |
|------|-------------|------|
| `i8` | Signed 8-bit integer | 1 byte |
| `i16` | Signed 16-bit integer | 2 bytes |
| `i32` | Signed 32-bit integer | 4 bytes |
| `i64` | Signed 64-bit integer | 8 bytes |
| `u8` | Unsigned 8-bit integer | 1 byte |
| `u16` | Unsigned 16-bit integer | 2 bytes |
| `u32` | Unsigned 32-bit integer | 4 bytes |
| `u64` | Unsigned 64-bit integer | 8 bytes |
| `f32` | 32-bit floating point | 4 bytes |
| `f64` | 64-bit floating point | 8 bytes |
| `bool` | Boolean | 1 byte |
| `string` | UTF-8 string | pointer |
| `void` | No value | 0 bytes |

### Composite Types

| Syntax | Description |
|--------|-------------|
| `[T]` | Dynamic array of T |
| `(T, U)` | Tuple of T and U |
| `T?` | Optional T (may be nil) |
| `Result<T, E>` | Success T or error E |
| `(T) -> U` | Function from T to U |
| `ref T` | Immutable reference to T |
| `ref mut T` | Mutable reference to T |
| `Map<K, V>` | Hash map from K to V |
| `Set<T>` | Hash set of T |

### Type Casting

```liva
let x: i32 = 42
let y = x as i64      // integer widening
let z = x as f64      // integer to float
```

The `as` keyword performs explicit type conversion between numeric types.

---

## 3. Variables and Constants

### Immutable Bindings (`let`)

```liva
let x: i32 = 42        // explicit type
let name = "Liva"      // inferred type
```

`let` bindings cannot be reassigned after initialization.

### Mutable Bindings (`var`)

```liva
var count: i32 = 0
count = count + 1       // OK: var is mutable

var name = "hello"
name = "world"          // OK
```

### Compile-time Constants (`const`)

```liva
const MAX_SIZE: i32 = 100
const PI: f64 = 3.14159
const GREETING: string = "Hello"
```

`const` values must be computable at compile time. The initializer is evaluated by the compiler and the value is inlined at every use site.

**Valid `const` initializers:**
- Integer, float, bool, and string literals
- Unary operations on constants (`-42`)
- Binary operations on constants (`10 + 20`, `3.14 * 2.0`)
- Cast expressions on constants (`42 as i64`)

### Tuple Destructuring

```liva
let (x, y) = (10, 20)
let (quotient, remainder) = divmod(17, 5)
```

---

## 4. Operators

### Arithmetic Operators

| Operator | Description | Example |
|----------|-------------|---------|
| `+` | Addition | `a + b` |
| `-` | Subtraction | `a - b` |
| `*` | Multiplication | `a * b` |
| `/` | Division | `a / b` |
| `%` | Modulo | `a % b` |
| `-` (unary) | Negation | `-a` |

### Comparison Operators

| Operator | Description |
|----------|-------------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `<=` | Less than or equal |
| `>` | Greater than |
| `>=` | Greater than or equal |

### Logical Operators

| Operator | Description |
|----------|-------------|
| `&&` | Logical AND (short-circuit) |
| `\|\|` | Logical OR (short-circuit) |
| `!` | Logical NOT |

### Bitwise Operators

| Operator | Description |
|----------|-------------|
| `&` | Bitwise AND |
| `\|` | Bitwise OR |
| `^` | Bitwise XOR |
| `~` | Bitwise NOT |
| `<<` | Left shift |
| `>>` | Right shift |

### Assignment Operators

| Operator | Equivalent |
|----------|------------|
| `=` | Simple assignment |
| `+=` | `a = a + b` |
| `-=` | `a = a - b` |
| `*=` | `a = a * b` |
| `/=` | `a = a / b` |
| `%=` | `a = a % b` |

### Special Operators

| Operator | Description | Example |
|----------|-------------|---------|
| `??` | Nil coalescing | `x ?? default` |
| `?.` | Optional chaining | `obj?.field` |
| `!` (postfix) | Force unwrap | `optional!` |
| `as` | Type cast | `x as i64` |
| `..` | Range | `0..10` |
| `...` | Inclusive range / variadic | `0...10` |
| `? :` | Ternary conditional | `cond ? a : b` |

### Operator Precedence (highest to lowest)

1. Postfix: `()`, `[]`, `.`, `?.`, `!`
2. Prefix: `-`, `!`, `~`, `ref`, `ref mut`
3. Cast: `as`
4. Multiplicative: `*`, `/`, `%`
5. Additive: `+`, `-`
6. Shift: `<<`, `>>`
7. Bitwise AND: `&`
8. Bitwise XOR: `^`
9. Bitwise OR: `|`
10. Comparison: `<`, `<=`, `>`, `>=`
11. Equality: `==`, `!=`
12. Logical AND: `&&`
13. Logical OR: `||`
14. Nil coalescing: `??`
15. Ternary: `? :`
16. Assignment: `=`, `+=`, `-=`, `*=`, `/=`, `%=`

---

## 5. Control Flow

### If / Else

```liva
if condition {
    // then branch
}

if x > 0 {
    println("positive")
} else if x == 0 {
    println("zero")
} else {
    println("negative")
}
```

Conditions must be of type `bool`. Parentheses around the condition are optional (but the braces are required).

### While Loop

```liva
var i = 0
while i < 10 {
    println(i)
    i = i + 1
}
```

### For-in Loop

```liva
// Iterate over a range
for i in 0..10 {
    println(i)
}

// Iterate over an array
let items = [10, 20, 30]
for item in items {
    println(item)
}

// Iterate over map with tuple destructuring
var m: Map<string, i32>
m.insert("a", 1)
for (key, value) in m {
    println(key)
}
```

### Break and Continue

```liva
var i = 0
while true {
    if i >= 10 { break }
    i = i + 1
    if i % 2 == 0 { continue }
    println(i)
}
```

### Guard Clause

```liva
func process(x: i32) {
    guard x > 0 else {
        println("must be positive")
        return
    }
    println(x)
}
```

The `guard` statement requires an `else` block that must exit the scope (via `return`, `break`, or `continue`).

### Match Expression

```liva
match value {
    0 => println("zero")
    1 => println("one")
    _ => println("other")
}
```

See [Enums and Pattern Matching](#9-enums-and-pattern-matching) for full details.

### Ternary Expression

```liva
let result = condition ? value_if_true : value_if_false
let abs_val = x < 0 ? -x : x
```

### If-let (Optional Binding)

```liva
let opt: i32? = 42

if let value = opt {
    println(value)       // value is i32 (unwrapped)
} else {
    println("was nil")
}
```

### While-let

```liva
while let item = queue.pop() {
    process(item)
}
```

---

## 6. Functions

### Basic Syntax

```liva
func functionName(param1: Type1, param2: Type2) -> ReturnType {
    // body
    return value
}
```

### Examples

```liva
// Simple function
func add(a: i32, b: i32) -> i32 {
    return a + b
}

// Void return (no return type annotation)
func greet(name: string) {
    println("Hello, \(name)!")
}

// No parameters
func getVersion() -> string {
    return "1.0.0"
}
```

### Default Parameters

```liva
func greet(name: string = "World") {
    println("Hello, \(name)!")
}

func add(a: i32, b: i32 = 10) -> i32 {
    return a + b
}

greet()            // "Hello, World!"
greet("Alice")     // "Hello, Alice!"
add(5)             // 15
add(5, 3)          // 8
```

### Variadic Functions

```liva
func sum(numbers: i32...) -> i32 {
    var total: i32 = 0
    for n in numbers {
        total = total + n
    }
    return total
}

let s = sum(1, 2, 3, 4, 5)  // 15
```

The variadic parameter must be the last parameter. Inside the function, it behaves as an array.

### Reference Parameters

```liva
// Immutable borrow — can read but not modify
func read(x: ref i32) -> i32 {
    return x
}

// Mutable borrow — can modify
func increment(x: ref mut i32) {
    x = x + 1
}

var n = 10
increment(ref mut n)   // n is now 11
let v = read(ref n)    // v = 11
```

### Public Functions

```liva
pub func publicAPI(x: i32) -> i32 {
    return x * 2
}
```

The `pub` keyword makes a function visible to other modules.

### Async Functions

```liva
async func fetchData(url: string) -> string {
    let response = await httpGet(url)
    return response
}
```

See [Async/Await](#20-asyncawait) for details.

---

## 7. Closures

### Closure Syntax

```liva
// Full syntax
let double = |x: i32| -> i32 { return x * 2 }

// Inferred return type
let triple = |x: i32| { return x * 3 }

// Multi-line closure
let process = |x: i32, y: i32| -> i32 {
    let sum = x + y
    return sum * 2
}
```

### Function Type Annotations

```liva
func apply(x: i32, f: (i32) -> i32) -> i32 {
    return f(x)
}

let result = apply(5, |x: i32| -> i32 { return x * 2 })
```

### Higher-Order Functions

```liva
let numbers = [1, 2, 3, 4, 5]

// forEach
numbers.forEach(|x: i32| {
    println(x)
})

// map — transform each element
let doubled = numbers.map(|x: i32| -> i32 { return x * 2 })

// filter — keep matching elements
let evens = numbers.filter(|x: i32| -> bool { return x % 2 == 0 })

// reduce — accumulate a value
let sum = numbers.reduce(0, |acc: i32, x: i32| -> i32 { return acc + x })
```

### Trailing Closure Syntax

When the last parameter of a function is a closure, you can pass it after the parentheses:

```liva
numbers.forEach { |x|
    println(x)
}

numbers.map { |x: i32| -> i32
    return x * 2
}
```

### Capture Semantics

Closures capture variables from their enclosing scope:

```liva
var counter = 0
let increment = || {
    counter = counter + 1   // captures 'counter' by reference
}
```

---

## 8. Structs

### Declaration

```liva
struct Point {
    var x: f64
    var y: f64
}
```

Fields declared with `var` are mutable; fields declared with `let` are immutable.

### Instantiation

```liva
let p = Point { x: 3.0, y: 4.0 }
```

All fields must be provided at creation.

### Impl Blocks (Methods)

```liva
impl Point {
    // Static method (no self)
    func new(x: f64, y: f64) -> Point {
        return Point { x: x, y: y }
    }

    // Immutable method (ref self)
    func magnitude(ref self) -> f64 {
        return sqrt(self.x * self.x + self.y * self.y)
    }

    // Mutable method (ref mut self)
    func translate(ref mut self, dx: f64, dy: f64) {
        self.x = self.x + dx
        self.y = self.y + dy
    }

    // Consuming method (self — takes ownership)
    func consume(self) {
        println(self.x)
    }

    // Mutable self (mut self)
    func drop(mut self) {
        // cleanup
    }
}
```

### Self Parameters

| Parameter | Access | Ownership |
|-----------|--------|-----------|
| `self` | Read-only | Takes ownership (move) |
| `ref self` | Read-only | Borrows immutably |
| `ref mut self` | Read/write | Borrows mutably |
| `mut self` | Read/write | Takes ownership (can mutate) |

### Calling Methods

```liva
var p = Point.new(3.0, 4.0)
let dist = p.magnitude()     // auto-borrows ref self
p.translate(1.0, 2.0)        // auto-borrows ref mut self
```

### Generic Structs

```liva
struct Box<T> {
    var value: T
}

impl<T> Box<T> {
    func new(val: T) -> Box<T> {
        return Box { value: val }
    }

    func get(ref self) -> T {
        return self.value
    }
}

let intBox = Box { value: 42 }
let strBox = Box { value: "hello" }
```

### Public Structs

```liva
pub struct Config {
    var debug: bool
    var port: i32
}
```

---

## 9. Enums and Pattern Matching

### Simple Enums

```liva
enum Direction {
    case North
    case South
    case East
    case West
}

let d = Direction.North
```

### Enums with Associated Values

```liva
enum Shape {
    case Circle(f64)             // radius
    case Rectangle(f64, f64)     // width, height
    case Empty
}

let s = Shape.Circle(3.14)
let r = Shape.Rectangle(10.0, 20.0)
```

### Match Expressions

```liva
match shape {
    Shape.Circle(r) => {
        println("Circle with radius \(r)")
    }
    Shape.Rectangle(w, h) => {
        let area = w * h
        println("Area: \(area)")
    }
    Shape.Empty => println("Empty shape")
}
```

### Match with Wildcards

```liva
match value {
    0 => println("zero")
    1 => println("one")
    _ => println("something else")    // wildcard matches anything
}
```

### Match with Guards

```liva
match x {
    n if n > 0 => println("positive")
    n if n < 0 => println("negative")
    _ => println("zero")
}
```

### Nested Pattern Matching

```liva
enum Wrapper {
    case Some(Shape)
    case None
}

match wrapper {
    Wrapper.Some(Shape.Circle(r)) => println(r)
    Wrapper.Some(Shape.Rectangle(w, h)) => println(w * h)
    Wrapper.None => println("nothing")
    _ => println("other")
}
```

### Exhaustiveness

The compiler checks that all enum cases are covered. If not all cases are listed, a wildcard `_` arm is required.

---

## 10. Protocols (Traits)

### Declaration

```liva
protocol Printable {
    func toString(ref self) -> string
}

protocol Shape {
    func area(ref self) -> f64
    func perimeter(ref self) -> f64
}
```

### Implementation

```liva
struct Circle {
    var radius: f64
}

impl Circle: Shape {
    func area(ref self) -> f64 {
        return 3.14159 * self.radius * self.radius
    }

    func perimeter(ref self) -> f64 {
        return 2.0 * 3.14159 * self.radius
    }
}
```

### Default Implementations

```liva
protocol Describable {
    func name(ref self) -> string

    // Default implementation
    func describe(ref self) -> string {
        return "Object: " + self.name()
    }
}
```

Types implementing `Describable` only need to provide `name()`; `describe()` is inherited unless overridden.

### Protocol as Type Constraint

```liva
func printShape<T: Shape>(s: ref T) {
    println(s.area())
}
```

### Multiple Trait Bounds

```liva
func display<T: Printable + Shape>(item: ref T) {
    println(item.toString())
    println(item.area())
}
```

### Where Clauses

```liva
func process<T>(item: ref T) where T: Printable, T: Shape {
    println(item.toString())
}
```

### Associated Types

```liva
protocol Iterator {
    type Item
    func next(ref mut self) -> Item?
}
```

---

## 11. Generics

### Generic Functions

```liva
func identity<T>(x: T) -> T {
    return x
}

let a = identity(42)       // T inferred as i32
let b = identity("hello")  // T inferred as string
```

### Generic Structs

```liva
struct Pair<A, B> {
    var first: A
    var second: B
}

impl<A, B> Pair<A, B> {
    func new(a: A, b: B) -> Pair<A, B> {
        return Pair { first: a, second: b }
    }
}
```

### Constrained Generics

```liva
func largest<T: Comparable>(a: T, b: T) -> T {
    if a > b {
        return a
    }
    return b
}
```

### Where Clauses

```liva
func combine<T, U>(a: T, b: U) -> string where T: Printable, U: Printable {
    return a.toString() + " " + b.toString()
}
```

---

## 12. Ownership and Borrowing

Liva uses an ownership system inspired by Rust to ensure memory safety without a garbage collector.

### Ownership Rules

1. Every value has exactly one owner
2. When the owner goes out of scope, the value is dropped
3. Ownership can be transferred (moved) to another variable or function

### Move Semantics

```liva
struct Buffer {
    var size: i32
}

func consume(buf: Buffer) {
    println(buf.size)
    // buf is dropped at the end of this function
}

var buf = Buffer { size: 1024 }
consume(buf)          // ownership moves to consume()
// println(buf.size)  // ERROR: use of moved value 'buf'
```

Passing a struct to a function by value **moves** ownership. After the move, the original variable is no longer valid.

### Copy Types

Primitive types (`i32`, `f64`, `bool`, etc.) are implicitly copied, not moved:

```liva
let x = 42
let y = x      // x is copied, both x and y are valid
println(x)     // OK
println(y)     // OK
```

### Borrowing

Borrowing lets you use a value without taking ownership:

```liva
// Immutable borrow (read-only)
func read(data: ref Buffer) {
    println(data.size)     // can read
    // data.size = 10      // ERROR: cannot modify through immutable reference
}

// Mutable borrow (read + write)
func modify(data: ref mut Buffer) {
    data.size = data.size + 1   // OK
}

var buf = Buffer { size: 100 }
read(ref buf)           // immutable borrow
modify(ref mut buf)     // mutable borrow
println(buf.size)       // buf is still valid (101)
```

### Borrowing Rules

1. You can have **multiple immutable borrows** (`ref T`) simultaneously
2. You can have **exactly one mutable borrow** (`ref mut T`) at a time
3. You cannot have a mutable borrow while immutable borrows exist

### Lifetime Analysis

The compiler verifies that references do not outlive the values they point to:

```liva
// ERROR: borrow outlives value
func bad() -> ref i32 {
    let x = 42
    return ref x    // x is dropped at end of function
}
```

---

## 13. Optional Types

### Declaration

```liva
let x: i32? = 42       // optional containing 42
let y: i32? = nil       // optional containing nothing
```

### Nil Coalescing

```liva
let value = x ?? 0     // 42 (uses x's value since it's not nil)
let other = y ?? -1    // -1 (uses default since y is nil)
```

### Optional Chaining

```liva
struct User {
    var name: string
    var age: i32
}

let user: User? = getUser()
let name = user?.name       // string? — nil if user is nil
let age = user?.age ?? 0   // chain + coalesce
```

### Force Unwrap

```liva
let value = x!     // panics at runtime if x is nil
```

Use force unwrap only when you are certain the value is not nil.

### If-let Binding

```liva
if let value = optionalValue {
    // value is the unwrapped non-nil value
    println(value)
} else {
    println("was nil")
}
```

### While-let Binding

```liva
while let item = iterator.next() {
    process(item)
}
```

---

## 14. Error Handling

### Result Type

```liva
enum FileError {
    case NotFound
    case PermissionDenied
}

func readFile(path: string) -> Result<string, FileError> {
    // return Ok(...) or Err(...)
}
```

### Pattern Matching on Results

```liva
let result = readFile("data.txt")
match result {
    Ok(content) => println(content)
    Err(FileError.NotFound) => println("File not found")
    Err(FileError.PermissionDenied) => println("Access denied")
}
```

### Try Expression

```liva
func processFile() -> Result<string, FileError> {
    let content = try readFile("input.txt")   // propagates error if Err
    return Ok(content)
}
```

---

## 15. Collections

### Arrays

```liva
// Array literal
let numbers: [i32] = [1, 2, 3, 4, 5]
let empty: [i32] = []

// Type inference
let names = ["Alice", "Bob", "Charlie"]

// Access by index (0-based)
let first = numbers[0]      // 1
let last = numbers[4]       // 5

// Length
let count = len(numbers)     // 5

// Slicing
let slice = numbers[1..4]    // [2, 3, 4]
```

#### Array Methods

```liva
var arr = [1, 2, 3]

arr.push(4)              // [1, 2, 3, 4]
arr.pop()                // [1, 2, 3]
arr.contains(2)          // true
arr.indexOf(3)           // 2
arr.reverse()            // [3, 2, 1]
let n = arr.length       // 3
let empty = arr.isEmpty  // false
```

#### Higher-Order Array Methods

```liva
let nums = [1, 2, 3, 4, 5]

nums.forEach(|x| { println(x) })
let doubled = nums.map(|x: i32| -> i32 { return x * 2 })
let evens = nums.filter(|x: i32| -> bool { return x % 2 == 0 })
let sum = nums.reduce(0, |acc: i32, x: i32| -> i32 { return acc + x })
```

### Maps (Hash Maps)

```liva
var m: Map<string, i32>

m.insert("alice", 30)
m.insert("bob", 25)

let age = m.get("alice")           // i32? — optional
let exists = m.contains("charlie") // false
m.remove("bob")

let count = m.size                 // 1
let empty = m.isEmpty              // false

// Iterate
for (key, value) in m {
    println("\(key): \(value)")
}
```

### Sets (Hash Sets)

```liva
var s: Set<i32>

s.insert(10)
s.insert(20)
s.insert(30)

let has = s.contains(20)   // true
s.remove(10)

let count = s.size          // 2
let empty = s.isEmpty       // false

// Iterate
for item in s {
    println(item)
}
```

---

## 16. Tuples

### Creation

```liva
let pair = (42, "hello")
let triple = (1, 2.0, true)
```

### Access

```liva
let x = pair.0        // 42
let y = pair.1        // "hello"
```

### Destructuring

```liva
let (a, b) = pair      // a = 42, b = "hello"
```

### Multi-Return Functions

```liva
func divmod(a: i32, b: i32) -> (i32, i32) {
    return (a / b, a % b)
}

let (quotient, remainder) = divmod(17, 5)
// quotient = 3, remainder = 2
```

### For-in with Tuple Destructuring

```liva
var m: Map<string, i32>
for (key, value) in m {
    println(key)
}
```

---

## 17. Type Aliases

```liva
type Int = i32
type Str = string
type Point2D = Point
type IntArray = [i32]

let x: Int = 42
let s: Str = "hello"
```

Type aliases create a new name for an existing type. The alias is interchangeable with the original type.

---

## 18. String Operations

### Built-in Methods

```liva
let s = "Hello World"

s.length                    // i64: string length
s.contains("World")         // bool: substring check
s.startsWith("Hello")       // bool: prefix check
s.endsWith("World")         // bool: suffix check
s.indexOf("World")          // i64: first occurrence index (-1 if not found)
s.substring(0, 5)           // string: "Hello"
s.trim()                    // string: remove leading/trailing whitespace
s.toUpper()                 // string: "HELLO WORLD"
s.toLower()                 // string: "hello world"
s.replace("World", "Liva")  // string: "Hello Liva"
s.split(" ")                // [string]: ["Hello", "World"]
```

### String Indexing

```liva
let ch = s[0]              // first character
let sub = s[0..5]          // substring via range: "Hello"
```

### String Interpolation

```liva
let name = "Liva"
let version = 1
let msg = "Welcome to \(name) v\(version)!"
// "Welcome to Liva v1!"
```

Any expression can be embedded inside `\(...)`.

### Multi-argument Print

```liva
println("x =", x, "y =", y)
```

### Format Function

```liva
let msg = format("x = {}, y = {}", 10, 20)
// "x = 10, y = 20"
```

---

## 19. Modules and Imports

### Import Syntax

```liva
import std::math          // import the math module
import std::io            // import I/O functions
import std::convert       // import conversion functions
```

### Available Standard Modules

| Module | Provides |
|--------|----------|
| `std::math` | `abs`, `min`, `max`, `sqrt`, `pow`, `floor`, `ceil`, `round`, `log`, `log10`, `sin`, `cos`, `tan` |
| `std::io` | `print`, `println`, `readLine`, `format`, `File` |
| `std::convert` | `parseInt`, `parseInt64`, `parseFloat`, `toString` |
| `std::os` | `env`, `exit`, `args`, `clock`, `clockMs`, `sleep` |
| `std::random` | `randInt`, `randFloat` |
| `std::regex` | `regexMatch`, `regexFind`, `regexFindAll`, `regexReplace` |
| `std::net` | `httpGet`, `httpPost` |
| `std` | All of the above combined |

### User Modules

```liva
// math_utils.liva
pub func square(x: i32) -> i32 {
    return x * x
}

// main.liva
import math_utils

func main() {
    println(square(5))
}
```

### Public Visibility

By default, declarations are module-private. Use `pub` to export:

```liva
pub func publicFunction() {}
pub struct PublicStruct { var x: i32 }
```

---

## 20. Async/Await

### Async Functions

```liva
async func fetchUser(id: i32) -> string {
    let response = await httpGet(format("/users/{}", id))
    return response
}
```

### Await Expression

```liva
async func main() {
    let user = await fetchUser(42)
    println(user)
}
```

The `await` keyword can only be used inside `async` functions. It suspends execution until the asynchronous operation completes.

### Limitations

- `main()` cannot be declared `async` (the scheduler runs synchronously)
- Async functions are implemented using LLVM coroutines

---

## 21. Operator Overloading

Operator overloading is achieved by implementing specific protocols:

```liva
protocol Addable {
    func add(ref self, other: ref Self) -> Self
}

struct Vec2 {
    var x: f64
    var y: f64
}

impl Vec2: Addable {
    func add(ref self, other: ref Vec2) -> Vec2 {
        return Vec2 { x: self.x + other.x, y: self.y + other.y }
    }
}

// Now you can use + with Vec2:
let a = Vec2 { x: 1.0, y: 2.0 }
let b = Vec2 { x: 3.0, y: 4.0 }
let c = a + b   // Vec2 { x: 4.0, y: 6.0 }
```

### Supported Operator Protocols

| Protocol | Operator | Method |
|----------|----------|--------|
| `Addable` | `+` | `add(ref self, other: ref Self) -> Self` |
| `Subtractable` | `-` | `sub(ref self, other: ref Self) -> Self` |
| `Multipliable` | `*` | `mul(ref self, other: ref Self) -> Self` |
| `Dividable` | `/` | `div(ref self, other: ref Self) -> Self` |
| `Equatable` | `==`, `!=` | `eq(ref self, other: ref Self) -> bool` |
| `Comparable` | `<`, `<=`, `>`, `>=` | `lt(ref self, other: ref Self) -> bool` |

---

## 22. Custom Iterators

Implement the `Iterator` protocol to make a type iterable in `for-in` loops:

```liva
protocol Iterator {
    type Item
    func next(ref mut self) -> Item?
}

struct Counter {
    var current: i32
    var max: i32
}

impl Counter: Iterator {
    type Item = i32

    func next(ref mut self) -> i32? {
        if self.current >= self.max {
            return nil
        }
        let val = self.current
        self.current = self.current + 1
        return val
    }
}

// Usage:
var counter = Counter { current: 0, max: 5 }
for value in counter {
    println(value)    // prints 0, 1, 2, 3, 4
}
```

---

## 23. Drop Trait (Destructors)

The `Drop` protocol provides a destructor that runs when a value goes out of scope:

```liva
protocol Drop {
    func drop(mut self)
}

struct FileHandle {
    var fd: i32
}

impl FileHandle: Drop {
    func drop(mut self) {
        // cleanup: close file descriptor
        println("Closing file")
    }
}

func main() {
    let f = FileHandle { fd: 3 }
    // ... use f ...
}   // f.drop() called automatically here
```

### Rules

- `drop` takes `mut self` (consuming ownership)
- The compiler inserts `drop()` calls at scope exit
- Drop is called in reverse declaration order

---

## 24. Standard Library

### Math Functions

```liva
import std::math

abs(-42)              // 42 (i32)
abs(-3.14)            // 3.14 (f64)
min(3, 7)             // 3
max(3, 7)             // 7
sqrt(16.0)            // 4.0
pow(2.0, 10.0)        // 1024.0
floor(3.7)            // 3.0
ceil(3.2)             // 4.0
round(3.5)            // 4.0
log(2.718)            // ~1.0
log10(1000.0)         // 3.0
sin(0.0)              // 0.0
cos(0.0)              // 1.0
tan(0.0)              // 0.0
```

### I/O Functions

```liva
import std::io

print("no newline")
println("with newline")
let line = readLine()           // read from stdin
let msg = format("x={}", 42)   // string formatting

// File I/O
let f = File.open("path.txt", "r")
if let file = f {
    let content = file.readAll()
    file.close()
}

let out = File.open("out.txt", "w")
if let file = out {
    file.writeLine("Hello!")
    file.close()
}
```

### Type Conversion

```liva
import std::convert

let s = toString(42)              // "42"
let n = parseInt("123")           // 123 (i32)
let big = parseInt64("999999")    // 999999 (i64)
let f = parseFloat("3.14")       // 3.14 (f64)
```

### OS Functions

```liva
import std::os

let home = env("HOME")            // environment variable
let arguments = args()             // command-line arguments [string]
let now = clock()                  // seconds since epoch (f64)
let ms = clockMs()                 // milliseconds since epoch (i64)
sleep(1000)                        // sleep for 1000ms
exit(0)                            // exit process
```

### Random

```liva
import std::random

let n = randInt(1, 100)     // random integer in [1, 100]
let f = randFloat()          // random float in [0.0, 1.0)
```

### Regular Expressions

```liva
import std::regex

let matches = regexMatch("hello123", "[a-z]+[0-9]+")   // true
let found = regexFind("age: 25", "[0-9]+")              // "25"
let all = regexFindAll("a1 b2 c3", "[a-z][0-9]")       // ["a1", "b2", "c3"]
let replaced = regexReplace("foo bar", "bar", "baz")    // "foo baz"
```

### Networking

```liva
import std::net

let body = httpGet("https://example.com")
let response = httpPost("https://api.example.com/data", "{\"key\":\"value\"}")
```

On Windows, networking uses WinHTTP. On Linux/macOS, it uses libcurl (if available).

---

## 25. Built-in Functions

These functions are available globally without any `import`:

| Function | Signature | Description |
|----------|-----------|-------------|
| `println` | `(any...)` | Print values with newline |
| `print` | `(any...)` | Print values without newline |
| `len` | `([T]) -> i64` | Array or string length |
| `toString` | `(any) -> string` | Convert to string |
| `abs` | `(numeric) -> numeric` | Absolute value |
| `min` | `(T, T) -> T` | Minimum of two values |
| `max` | `(T, T) -> T` | Maximum of two values |
| `sqrt` | `(f64) -> f64` | Square root |
| `pow` | `(f64, f64) -> f64` | Exponentiation |
| `readLine` | `() -> string` | Read line from stdin |
| `format` | `(string, any...) -> string` | String formatting |
| `parseInt` | `(string) -> i32` | Parse integer |
| `parseFloat` | `(string) -> f64` | Parse float |
| `randInt` | `(i32, i32) -> i32` | Random integer |
| `randFloat` | `() -> f64` | Random float [0, 1) |

---

## 26. Project Configuration

### liva.toml

Create a `liva.toml` file in the project root:

```toml
[project]
name = "myapp"
version = "1.0.0"
entry = "src/main.liva"

[build]
optimization = "release"    # "debug" or "release"

[dependencies]
json_parser = "^1.0.0"     # SemVer constraint
utils = "~2.3"             # compatible with 2.3.x
```

### Version Constraints

| Syntax | Meaning |
|--------|---------|
| `"1.2.3"` | Exact version |
| `"^1.2.3"` | Compatible (>=1.2.3, <2.0.0) |
| `"~1.2.3"` | Patch-level (>=1.2.3, <1.3.0) |
| `">=1.0"` | Minimum version |

### Lock File

`liva.lock` is auto-generated and pins exact dependency versions. Commit this file for reproducible builds.

### Project Commands

```bash
livac init myproject        # create new project
livac build                 # build (debug)
livac build --release       # build (optimized)
livac run                   # build + run
```

---

## 27. Compiler Options

```
Usage: livac [options] <file>

Compilation:
  -o <output>          Output file path
  -O0, -O1, -O2, -O3  Optimization level
  -g                   Generate debug information
  --debug              Debug build (O0 + debug info)
  --release            Release build (O2, no debug info)

Diagnostics:
  --dump-tokens <file>   Print token stream
  --dump-ast <file>      Print AST tree
  --check-only <file>    Type-check without code generation
  --emit-ir <file>       Output LLVM IR

Project:
  init [name]            Create a new Liva project
  build [--release]      Build from liva.toml
  run [--release]        Build and execute

Tools:
  lsp                    Start Language Server Protocol server
  repl                   Start interactive REPL
```

### LSP Server

The `livac lsp` command starts a Language Server Protocol server over stdio, providing:

- **Diagnostics** — Real-time error and warning reporting
- **Completion** — Keywords, built-in functions, and document symbols
- **Hover** — Type information for functions, variables, structs, enums
- **Go to Definition** — Jump to declaration
- **Document Symbols** — Outline view of functions, types, variables
- **References** — Find all occurrences of a symbol
- **Rename** — Rename a symbol across the document
- **Signature Help** — Parameter hints when calling functions

### REPL

The `livac repl` command starts an interactive session:

```
>>> 1 + 2
3
>>> func double(x: i32) -> i32 { return x * 2 }
Declaration added.
>>> double(21)
42
>>> import std::math
Import added.
>>> :help
  :help, :h           Show this help
  :quit, :q           Exit the REPL
  :reset, :r          Clear all declarations
  :declarations, :decls  Show accumulated declarations
>>> :quit
Goodbye!
```

Features:
- Expression evaluation (auto-wrapped in `println`)
- Declaration accumulation (functions, structs, enums persist across inputs)
- Statement execution (`if`, `while`, `for`)
- Multi-line input (unclosed braces/parens continue on next line)
- Import support

---

## Appendix: Grammar Summary

```
program       = declaration*
declaration   = funcDecl | varDecl | structDecl | enumDecl
              | implDecl | protocolDecl | importDecl | typeAlias

funcDecl      = ["pub"] ["async"] "func" IDENT ["<" typeParams ">"]
                "(" params ")" ["->" type] block
varDecl       = ("let" | "var" | "const") IDENT [":" type] ["=" expr]
structDecl    = ["pub"] "struct" IDENT ["<" typeParams ">"] "{" fieldDecl* "}"
enumDecl      = ["pub"] "enum" IDENT "{" caseDecl* "}"
implDecl      = "impl" ["<" typeParams ">"] IDENT [":" IDENT] "{" funcDecl* "}"
protocolDecl  = ["pub"] "protocol" IDENT "{" funcDecl* "}"
importDecl    = "import" IDENT ("::" IDENT)*
typeAlias     = ["pub"] "type" IDENT "=" type

statement     = exprStmt | returnStmt | ifStmt | whileStmt | forStmt
              | breakStmt | continueStmt | guardStmt | block
block         = "{" (declaration | statement)* "}"

expr          = assignment
assignment    = ternary (("=" | "+=" | "-=" | "*=" | "/=" | "%=") ternary)?
ternary       = or ("?" expr ":" expr)?
or            = and ("||" and)*
and           = equality ("&&" equality)*
equality      = comparison (("==" | "!=") comparison)*
comparison    = bitOr (("<" | "<=" | ">" | ">=") bitOr)*
bitOr         = bitXor ("|" bitXor)*
bitXor        = bitAnd ("^" bitAnd)*
bitAnd        = shift ("&" shift)*
shift         = addition (("<<" | ">>") addition)*
addition      = multiplication (("+" | "-") multiplication)*
multiplication = cast (("*" | "/" | "%") cast)*
cast          = unary ("as" type)?
unary         = ("-" | "!" | "~" | "ref" ["mut"]) unary | postfix
postfix       = primary ("." IDENT | "?." IDENT | "[" expr "]"
              | "(" args ")" | "!" | trailing_closure)*

primary       = INTEGER | FLOAT | STRING | BOOL | "nil"
              | IDENT | "(" expr ")" | "[" elements "]"
              | "(" elements ")"  -- tuple
              | IDENT "{" fieldInits "}"  -- struct literal
              | "match" expr "{" matchArms "}"
              | "|" params "|" ["->" type] block  -- closure
              | "try" expr | "await" expr

type          = "i8" | "i16" | "i32" | "i64" | "u8" | "u16" | "u32" | "u64"
              | "f32" | "f64" | "bool" | "string" | "void"
              | IDENT ["<" type ("," type)* ">"]
              | "[" type "]"
              | "(" type ("," type)* ")" "->" type
              | "(" type ("," type)* ")"
              | "ref" ["mut"] type
              | type "?"
```
