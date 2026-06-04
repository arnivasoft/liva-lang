# Liva Language Tutorial

A hands-on, step-by-step guide to learning the Liva programming language — from your first program to advanced features like generics, ownership, and async/await.

> **Prerequisites:** Basic programming experience in any language. See [README.md](README.md) for build and installation instructions.

---

## Table of Contents

1. [Hello, World!](#1-hello-world)
2. [Variables and Constants](#2-variables-and-constants)
3. [Primitive Types](#3-primitive-types)
4. [Operators](#4-operators)
5. [Control Flow](#5-control-flow)
6. [Functions](#6-functions)
7. [Strings](#7-strings)
8. [Arrays](#8-arrays)
9. [Structs and Methods](#9-structs-and-methods)
10. [Enums and Pattern Matching](#10-enums-and-pattern-matching)
11. [Closures](#11-closures)
12. [Generics](#12-generics)
13. [Protocols (Traits)](#13-protocols-traits)
14. [Ownership and Borrowing](#14-ownership-and-borrowing)
15. [Optional Types](#15-optional-types)
16. [Error Handling](#16-error-handling)
17. [Collections: Maps and Sets](#17-collections-maps-and-sets)
18. [Tuples](#18-tuples)
19. [Modules and Imports](#19-modules-and-imports)
20. [Async/Await](#20-asyncawait)
21. [Advanced Features](#21-advanced-features)
22. [Project Management](#22-project-management)
23. [Tooling](#23-tooling)
24. [What's Next?](#24-whats-next)
25. [Classes and OOP](#25-classes-and-oop)
26. [Testing Your Code](#26-testing-your-code)
27. [Compile-Time Features](#27-compile-time-features)

---

## 1. Hello, World!

Every Liva program starts with a `main` function. Let's write the simplest possible program:

```liva
func main() {
    println("Hello, World!")
}
```

Save this as `hello.liva` and compile it:

```bash
livac -o hello hello.liva
./hello
# Output: Hello, World!
```

**What's happening here:**
- `func` declares a function
- `main()` is the entry point — every executable needs one
- `println()` prints a value followed by a newline
- No semicolons needed — Liva uses newlines as statement terminators
- Curly braces `{ }` delimit blocks

You can also use `print()` which does not add a newline:

```liva
func main() {
    print("Hello, ")
    println("World!")
}
```

### Try it in the REPL

You can experiment without creating files using the interactive REPL:

```bash
livac repl
>>> println("Hello from REPL!")
Hello from REPL!
>>> 2 + 3
5
```

---

## 2. Variables and Constants

Liva has three ways to bind values to names:

### Immutable bindings with `let`

```liva
func main() {
    let name: string = "Liva"
    let year: i32 = 2025
    println(name)
    println(year)

    // name = "Other"   // ERROR: cannot assign to immutable variable
}
```

`let` bindings cannot be reassigned after initialization. This is the default — prefer `let` whenever possible.

### Mutable bindings with `var`

```liva
func main() {
    var count: i32 = 0
    println(count)    // 0

    count = count + 1
    println(count)    // 1

    count = 42
    println(count)    // 42
}
```

Use `var` when you need to change a value after creation.

### Compile-time constants with `const`

```liva
const MAX_SIZE: i32 = 100
const PI: f64 = 3.14159265

func main() {
    println(MAX_SIZE)
    println(PI)
}
```

`const` values are evaluated at compile time. They must be simple literal expressions or constant operations (arithmetic on other constants). Use `const` for values that are truly fixed and known at compile time.

### Type inference

You don't always need to write out the type — Liva can often figure it out:

```liva
func main() {
    let x = 42           // inferred as i32
    let pi = 3.14        // inferred as f64
    let greeting = "Hi"  // inferred as string
    let flag = true       // inferred as bool

    println(x)
    println(pi)
    println(greeting)
    println(flag)
}
```

**Rule of thumb:** Omit the type annotation when it's obvious from the right-hand side. Add it when it clarifies your intent or when you need a specific type (e.g., `let x: i64 = 42`).

---

## 3. Primitive Types

Liva provides a rich set of built-in types:

### Integer types

| Type | Size | Range |
|------|------|-------|
| `i8` | 8-bit | -128 to 127 |
| `i16` | 16-bit | -32,768 to 32,767 |
| `i32` | 32-bit | -2^31 to 2^31-1 |
| `i64` | 64-bit | -2^63 to 2^63-1 |
| `u8` | 8-bit | 0 to 255 |
| `u16` | 16-bit | 0 to 65,535 |
| `u32` | 32-bit | 0 to 2^32-1 |
| `u64` | 64-bit | 0 to 2^64-1 |

`i32` is the default integer type.

### Floating-point types

| Type | Size | Precision |
|------|------|-----------|
| `f32` | 32-bit | ~7 decimal digits |
| `f64` | 64-bit | ~15 decimal digits |

`f64` is the default floating-point type.

### Other types

| Type | Description | Example |
|------|-------------|---------|
| `bool` | Boolean | `true`, `false` |
| `string` | Text | `"Hello"` |
| `void` | No value | Used for functions that return nothing |

### Type casting with `as`

You can convert between numeric types using the `as` keyword:

```liva
func main() {
    let x: i32 = 42
    let y: i64 = x as i64        // widen i32 → i64
    let z: f64 = x as f64        // integer → float

    let pi: f64 = 3.14
    let rounded: i32 = pi as i32  // float → integer (truncates)

    println(y)        // 42
    println(z)        // 42.0
    println(rounded)  // 3
}
```

---

## 4. Operators

### Arithmetic

```liva
func main() {
    let a: i32 = 17
    let b: i32 = 5

    println(a + b)    // 22  — addition
    println(a - b)    // 12  — subtraction
    println(a * b)    // 85  — multiplication
    println(a / b)    // 3   — integer division
    println(a % b)    // 2   — modulo (remainder)
}
```

### Comparison

```liva
func main() {
    let x: i32 = 10

    println(x == 10)   // true
    println(x != 5)    // true
    println(x < 20)    // true
    println(x > 5)     // true
    println(x <= 10)   // true
    println(x >= 15)   // false
}
```

### Logical

```liva
func main() {
    let a = true
    let b = false

    println(a && b)    // false — logical AND
    println(a || b)    // true  — logical OR
    println(!a)        // false — logical NOT
}
```

### Compound assignment

```liva
func main() {
    var x: i32 = 10
    x += 5     // x = x + 5  → 15
    x -= 3     // x = x - 3  → 12
    x *= 2     // x = x * 2  → 24
    x /= 4     // x = x / 4  → 6
    x %= 4     // x = x % 4  → 2
    println(x) // 2
}
```

### Bitwise

```liva
func main() {
    let a: i32 = 0b1100   // 12
    let b: i32 = 0b1010   // 10

    println(a & b)    // 8   (0b1000) — AND
    println(a | b)    // 14  (0b1110) — OR
    println(a ^ b)    // 6   (0b0110) — XOR
    println(~a)       // bitwise NOT
    println(a << 2)   // 48  — left shift
    println(a >> 1)   // 6   — right shift
}
```

---

## 5. Control Flow

### if / else

```liva
func main() {
    let score: i32 = 85

    if score >= 90 {
        println("A")
    } else if score >= 80 {
        println("B")
    } else if score >= 70 {
        println("C")
    } else {
        println("F")
    }
}
```

**Note:** Conditions do NOT require parentheses (unlike C/Java), but braces are always required.

### while loops

```liva
func main() {
    var sum: i32 = 0
    var i: i32 = 1

    while i <= 10 {
        sum = sum + i
        i = i + 1
    }

    println(sum)  // 55
}
```

### for-in loops

Liva uses `for-in` for iteration — there is no C-style `for(;;)` loop.

```liva
func main() {
    // Range iteration (0 to 4)
    for i in 0..5 {
        println(i)
    }

    // Array iteration
    let fruits = ["apple", "banana", "cherry"]
    for fruit in fruits {
        println(fruit)
    }
}
```

The `..` operator creates a half-open range: `0..5` means 0, 1, 2, 3, 4.

### break and continue

```liva
func main() {
    // break exits the loop
    var i: i32 = 0
    while true {
        if i >= 5 {
            break
        }
        println(i)
        i = i + 1
    }
    // Prints: 0 1 2 3 4

    // continue skips to the next iteration
    for j in 0..10 {
        if j % 2 == 0 {
            continue
        }
        println(j)
    }
    // Prints: 1 3 5 7 9
}
```

### guard clauses

`guard` is an early-exit construct — it requires the condition to be true, otherwise the `else` block must return or break:

```liva
func process(value: i32) {
    guard value > 0 else {
        println("Invalid: must be positive")
        return
    }

    // If we reach here, value is guaranteed > 0
    println(value)
}

func main() {
    process(42)   // prints 42
    process(-1)   // prints "Invalid: must be positive"
}
```

### Ternary expressions

```liva
func main() {
    let x: i32 = 10
    let label = x > 0 ? "positive" : "non-positive"
    println(label)  // "positive"

    let abs_val = x < 0 ? 0 - x : x
    println(abs_val)  // 10
}
```

---

## 6. Functions

### Basic functions

```liva
func add(a: i32, b: i32) -> i32 {
    return a + b
}

func greet(name: string) {
    println("Hello, " + name + "!")
}

func main() {
    let sum = add(3, 4)
    println(sum)      // 7
    greet("World")    // Hello, World!
}
```

- Parameters are declared as `name: Type`
- Return type follows `->` (omit for `void` functions)
- Call with positional arguments: `add(3, 4)`

### Default parameters

```liva
func greet(name: string = "World") {
    println("Hello, " + name + "!")
}

func power(base: f64, exp: f64 = 2.0) -> f64 {
    return pow(base, exp)
}

func main() {
    greet("Alice")   // Hello, Alice!
    greet()           // Hello, World!

    println(power(3.0, 3.0))  // 27.0
    println(power(5.0))        // 25.0
}
```

Default parameters must come after non-default parameters.

### Recursion

```liva
func factorial(n: i32) -> i32 {
    if n <= 1 {
        return 1
    }
    return n * factorial(n - 1)
}

func fibonacci(n: i32) -> i32 {
    if n <= 1 {
        return n
    }
    return fibonacci(n - 1) + fibonacci(n - 2)
}

func main() {
    println(factorial(5))    // 120
    println(fibonacci(10))   // 55
}
```

### Variadic functions

Functions can accept a variable number of arguments using `...`:

```liva
func sum(numbers: i32...) -> i32 {
    var total: i32 = 0
    for n in numbers {
        total = total + n
    }
    return total
}

func main() {
    println(sum(1, 2, 3))        // 6
    println(sum(10, 20, 30, 40)) // 100
}
```

### Passing functions as arguments

Functions are first-class values — you can pass them to other functions:

```liva
func apply(x: i32, f: (i32) -> i32) -> i32 {
    return f(x)
}

func double_it(x: i32) -> i32 {
    return x * 2
}

func square(x: i32) -> i32 {
    return x * x
}

func main() {
    println(apply(5, double_it))  // 10
    println(apply(5, square))     // 25
}
```

The type `(i32) -> i32` describes a function that takes an `i32` and returns an `i32`.

---

## 7. Strings

Strings in Liva are UTF-8 encoded and support a rich set of operations.

### String basics

```liva
func main() {
    let greeting = "Hello, World!"
    println(greeting)

    // String length
    println(len(greeting))    // 13

    // Concatenation
    let first = "Hello"
    let second = "World"
    let combined = first + ", " + second + "!"
    println(combined)
}
```

### String interpolation

Use `\(expression)` to embed values inside strings:

```liva
func main() {
    let name = "Alice"
    let age: i32 = 30

    println("My name is \(name) and I am \(age) years old.")
    println("Next year I'll be \(age + 1).")
}
```

### String methods

```liva
func main() {
    let s = "Hello World"

    // Searching
    println(s.contains("World"))     // true
    println(s.startsWith("Hello"))   // true
    println(s.endsWith("World"))     // true
    println(s.indexOf("World"))      // 6

    // Transforming
    println(s.toUpper())             // HELLO WORLD
    println(s.toLower())             // hello world
    println(s.replace("World", "Liva"))  // Hello Liva

    // Extracting
    println(s.substring(0, 5))       // Hello
    println(s.trim())                // Hello World (removes whitespace)

    // Splitting
    let csv = "apple,banana,cherry"
    let parts = csv.split(",")
    for part in parts {
        println(part)
    }
}
```

### String indexing and slicing

```liva
func main() {
    let s = "Hello"

    // Single character access
    let ch = s[0]
    println(ch)    // H

    // Slicing with ranges
    let sub = s[0..3]
    println(sub)   // Hel
}
```

### Multi-argument print

`println` can take multiple arguments, separated by spaces:

```liva
func main() {
    let x: i32 = 10
    let y: i32 = 20
    println("x =", x, "y =", y)  // x = 10 y = 20
}
```

### Format function

```liva
func main() {
    let msg = format("Name: {}, Age: {}", "Alice", 30)
    println(msg)  // Name: Alice, Age: 30
}
```

---

## 8. Arrays

Arrays are ordered, dynamically-sized collections of elements with the same type.

### Creating arrays

```liva
func main() {
    // Array literal
    let numbers = [1, 2, 3, 4, 5]

    // With explicit type
    let names: [string] = ["Alice", "Bob", "Charlie"]

    // Mutable array
    var scores: [i32] = [90, 85, 92]

    println(len(numbers))  // 5
    println(numbers[0])    // 1
    println(numbers[4])    // 5
}
```

### Modifying arrays

```liva
func main() {
    var arr: [i32] = [10, 20, 30]

    // Index assignment
    arr[0] = 100
    println(arr[0])    // 100

    // Push to end
    arr.push(40)
    println(arr.length)  // 4

    // Pop from end
    let last = arr.pop()
    println(arr.length)  // 3
}
```

### Array methods

```liva
func main() {
    var arr: [i32] = [10, 20, 30, 40, 50]

    // Properties
    println(arr.length)     // 5
    println(arr.isEmpty)    // false

    // Searching
    println(arr.contains(30))  // true
    println(arr.indexOf(40))   // 3

    // Reversing
    arr.reverse()
    // arr is now [50, 40, 30, 20, 10]

    // Slicing
    let slice = arr[1..4]
    // slice is [40, 30, 20]
}
```

### Iterating over arrays

```liva
func main() {
    let fruits = ["apple", "banana", "cherry"]

    // for-in loop
    for fruit in fruits {
        println(fruit)
    }

    // With index using range
    for i in 0..len(fruits) {
        println(fruits[i])
    }
}
```

### Higher-order array methods

```liva
func main() {
    var numbers: [i32] = [1, 2, 3, 4, 5]

    // forEach: execute a function for each element
    numbers.forEach(|x| {
        println(x)
    })

    // map: transform each element
    let doubled = numbers.map(|x: i32| -> i32 { return x * 2 })
    // doubled is [2, 4, 6, 8, 10]

    // filter: keep elements that match a condition
    let evens = numbers.filter(|x: i32| -> bool { return x % 2 == 0 })
    // evens is [2, 4]

    // reduce: combine all elements into one value
    let sum = numbers.reduce(0) { |acc, x| acc + x }
    // sum is 15
}
```

---

## 9. Structs and Methods

Structs are the primary way to create custom data types in Liva.

### Defining a struct

```liva
struct Point {
    var x: f64
    var y: f64
}

func main() {
    // Create an instance
    let p = Point { x: 3.0, y: 4.0 }

    // Access fields
    println(p.x)   // 3.0
    println(p.y)   // 4.0

    // Mutable struct — fields can be modified
    var q = Point { x: 0.0, y: 0.0 }
    q.x = 10.0
    q.y = 20.0
    println(q.x)   // 10.0
}
```

### Adding methods with `impl`

Methods are defined in a separate `impl` block:

```liva
struct Point {
    var x: f64
    var y: f64
}

impl Point {
    // Static method (no self parameter) — acts as a constructor
    func new(x: f64, y: f64) -> Point {
        return Point { x: x, y: y }
    }

    // Immutable method — can read fields
    func distanceFromOrigin(ref self) -> f64 {
        return sqrt(self.x * self.x + self.y * self.y)
    }

    // Mutable method — can modify fields
    func translate(ref mut self, dx: f64, dy: f64) {
        self.x = self.x + dx
        self.y = self.y + dy
    }

    // Value method — takes ownership of self
    func sum(self) -> f64 {
        return self.x + self.y
    }
}

func main() {
    var p = Point.new(3.0, 4.0)

    println(p.distanceFromOrigin())  // 5.0

    p.translate(1.0, 2.0)
    println(p.x)  // 4.0
    println(p.y)  // 6.0
}
```

**Self parameter types:**
| Syntax | Meaning | Can read? | Can modify? |
|--------|---------|-----------|-------------|
| `self` | Takes ownership | Yes | N/A (consumed) |
| `ref self` | Immutable borrow | Yes | No |
| `ref mut self` | Mutable borrow | Yes | Yes |

### Example: Building a stack

```liva
struct Stack {
    var items: [i32]
}

impl Stack {
    func new() -> Stack {
        return Stack { items: [] }
    }

    func push(ref mut self, value: i32) {
        self.items.push(value)
    }

    func pop(ref mut self) -> i32 {
        return self.items.pop()
    }

    func peek(ref self) -> i32 {
        return self.items[self.items.length - 1]
    }

    func isEmpty(ref self) -> bool {
        return self.items.isEmpty
    }

    func size(ref self) -> i32 {
        return self.items.length
    }
}

func main() {
    var stack = Stack.new()
    stack.push(10)
    stack.push(20)
    stack.push(30)

    println(stack.size())   // 3
    println(stack.peek())   // 30
    println(stack.pop())    // 30
    println(stack.size())   // 2
}
```

---

## 10. Enums and Pattern Matching

### Simple enums

```liva
enum Color {
    case Red
    case Green
    case Blue
}

func main() {
    let c = Color.Green

    match c {
        Color.Red => println("Red")
        Color.Green => println("Green")
        Color.Blue => println("Blue")
    }
}
```

### Enums with associated values

Enum variants can carry data — this is one of Liva's most powerful features:

```liva
enum Shape {
    case Circle(f64)              // radius
    case Rectangle(f64, f64)      // width, height
    case Empty
}

func area(shape: Shape) -> f64 {
    match shape {
        Shape.Circle(r) => 3.14159 * r * r
        Shape.Rectangle(w, h) => w * h
        Shape.Empty => 0.0
    }
}

func describe(shape: Shape) {
    match shape {
        Shape.Circle(r) => println("Circle with radius \(r)")
        Shape.Rectangle(w, h) => {
            println("Rectangle \(w) x \(h)")
        }
        Shape.Empty => println("Empty shape")
    }
}

func main() {
    let circle = Shape.Circle(5.0)
    let rect = Shape.Rectangle(3.0, 4.0)

    println(area(circle))   // 78.53975
    println(area(rect))     // 12.0

    describe(circle)        // Circle with radius 5.0
}
```

### Pattern matching features

#### Wildcard patterns

```liva
func classify(x: i32) {
    match x {
        0 => println("zero")
        1 => println("one")
        _ => println("something else")
    }
}
```

#### Match guards

```liva
func classify_number(x: i32) {
    match x {
        n if n < 0 => println("negative")
        0 => println("zero")
        n if n > 100 => println("large")
        _ => println("normal")
    }
}
```

#### Nested patterns

```liva
enum Expr {
    case Num(i32)
    case Add(Expr, Expr)
}

func eval(e: Expr) -> i32 {
    match e {
        Expr.Num(n) => n
        Expr.Add(Expr.Num(a), Expr.Num(b)) => a + b
        _ => 0
    }
}
```

### while-let

Repeatedly pattern-match until the pattern fails:

```liva
func main() {
    var values: [i32?] = [1, nil, 3, nil, 5]
    var i: i32 = 0
    while let val = values[i] {
        println(val)
        i = i + 1
    }
}
```

---

## 11. Closures

Closures are anonymous functions that can capture variables from their surrounding scope.

### Basic closure syntax

```liva
func main() {
    // Named function
    func double(x: i32) -> i32 {
        return x * 2
    }

    // Equivalent closure
    let double_closure = |x: i32| -> i32 { return x * 2 }

    println(double(5))          // 10
    println(double_closure(5))  // 10
}
```

### Closures with higher-order functions

```liva
func apply(x: i32, f: (i32) -> i32) -> i32 {
    return f(x)
}

func main() {
    let result = apply(5, |x: i32| -> i32 { return x * x })
    println(result)  // 25
}
```

### Trailing closure syntax

When the last parameter is a function, you can write the closure after the parentheses:

```liva
func main() {
    var numbers: [i32] = [1, 2, 3, 4, 5]

    // Regular syntax
    numbers.forEach(|x| { println(x) })

    // Trailing closure syntax
    numbers.forEach { |x|
        println(x)
    }
}
```

### Capture by reference

Closures capture variables from their enclosing scope. By default, mutable variables are captured by reference:

```liva
func main() {
    var count: i32 = 0

    let increment = || -> void {
        count += 1
    }

    increment()
    increment()
    increment()

    println(count)  // 3
}
```

### Practical example: accumulator

```liva
func apply(f: () -> void) {
    f()
    f()
    f()
}

func main() {
    var total: i32 = 0

    apply(|| -> void { total += 10 })

    println(total)  // 30
}
```

---

## 12. Generics

Generics let you write code that works with any type.

### Generic functions

```liva
func identity<T>(x: T) -> T {
    return x
}

func main() {
    let a = identity(42)       // T = i32
    let b = identity("hello")  // T = string

    println(a)   // 42
    println(b)   // hello
}
```

The compiler infers `T` from the argument — no need to specify it explicitly.

### Generic structs

```liva
struct Pair<A, B> {
    var first: A
    var second: B
}

impl<A, B> Pair<A, B> {
    func new(first: A, second: B) -> Pair<A, B> {
        return Pair { first: first, second: second }
    }

    func getFirst(ref self) -> A {
        return self.first
    }

    func getSecond(ref self) -> B {
        return self.second
    }
}

func main() {
    let p = Pair.new(42, "hello")
    println(p.getFirst())    // 42
    println(p.getSecond())   // hello
}
```

### Constrained generics

You can require that type parameters implement specific protocols:

```liva
protocol Printable {
    func toString(ref self) -> string
}

func printItem<T: Printable>(item: ref T) {
    println(item.toString())
}
```

See the [Protocols](#13-protocols-traits) section for more details.

### Where clauses

For more complex constraints, use `where`:

```liva
func process<T, U>(a: T, b: U) where T: Printable, U: Printable {
    println(a.toString())
    println(b.toString())
}
```

---

## 13. Protocols (Traits)

Protocols define a set of methods that types must implement — similar to interfaces in other languages or traits in Rust.

### Defining a protocol

```liva
protocol Describable {
    func describe(ref self) -> string
}
```

### Implementing a protocol

```liva
struct Dog {
    var name: string
    var age: i32
}

impl Describable for Dog {
    func describe(ref self) -> string {
        return self.name
    }
}

func main() {
    let dog = Dog { name: "Rex", age: 5 }
    println(dog.describe())  // Rex
}
```

### Protocol as constraint

```liva
func printDescription<T: Describable>(item: ref T) {
    println(item.describe())
}

func main() {
    let dog = Dog { name: "Rex", age: 5 }
    printDescription(ref dog)  // Rex
}
```

### Default implementations

Protocols can provide default method implementations:

```liva
protocol Greetable {
    func name(ref self) -> string

    func greet(ref self) -> string {
        return "Hello, " + self.name() + "!"
    }
}

struct Person {
    var personName: string
}

impl Greetable for Person {
    func name(ref self) -> string {
        return self.personName
    }
    // greet() uses the default implementation
}

func main() {
    let p = Person { personName: "Alice" }
    println(p.greet())  // Hello, Alice!
}
```

### Multiple protocol conformance

A type can implement multiple protocols:

```liva
protocol Printable {
    func toString(ref self) -> string
}

protocol Comparable {
    func compareTo(ref self, other: ref Self) -> i32
}

struct Score {
    var value: i32
}

impl Printable for Score {
    func toString(ref self) -> string {
        return "Score: " + toString(self.value)
    }
}

impl Comparable for Score {
    func compareTo(ref self, other: ref Score) -> i32 {
        return self.value - other.value
    }
}
```

### Operator overloading via protocols

Liva uses protocols for operator overloading:

```liva
struct Vec2 {
    var x: f64
    var y: f64
}

impl Addable for Vec2 {
    func add(ref self, other: ref Vec2) -> Vec2 {
        return Vec2 { x: self.x + other.x, y: self.y + other.y }
    }
}

func main() {
    let a = Vec2 { x: 1.0, y: 2.0 }
    let b = Vec2 { x: 3.0, y: 4.0 }
    let c = a + b   // calls Addable.add
    println(c.x)    // 4.0
    println(c.y)    // 6.0
}
```

Available operator protocols: `Addable` (+), `Subtractable` (-), `Multipliable` (*), `Dividable` (/), `Equatable` (==, !=), `Comparable` (<, >, <=, >=).

---

## 14. Ownership and Borrowing

Liva uses ownership and borrowing to guarantee memory safety without a garbage collector. This is one of Liva's defining features.

### Ownership rules

1. Every value has exactly one **owner**
2. When the owner goes out of scope, the value is **dropped** (freed)
3. Ownership can be **transferred** (moved)

### Move semantics

When you assign a non-Copy value (like a string or struct) to another variable or pass it to a function, ownership **moves**:

```liva
struct Buffer {
    var size: i32
}

func consume(buf: Buffer) {
    println(buf.size)
    // buf is dropped here
}

func main() {
    var buf = Buffer { size: 1024 }
    consume(buf)
    // buf has been moved — using it here would be an error
    // println(buf.size)  // ERROR: use after move
}
```

### Copy types

Primitive types (`i32`, `f64`, `bool`, etc.) are **Copy** — they are duplicated instead of moved:

```liva
func main() {
    let x: i32 = 42
    let y = x       // x is copied, not moved
    println(x)      // OK — x is still valid
    println(y)      // 42
}
```

### Borrowing with references

Instead of transferring ownership, you can **borrow** a value:

```liva
// Immutable borrow — can read but not modify
func print_size(buf: ref Buffer) {
    println(buf.size)
}

// Mutable borrow — can read and modify
func double_size(buf: ref mut Buffer) {
    buf.size = buf.size * 2
}

func main() {
    var buf = Buffer { size: 100 }

    print_size(ref buf)       // borrow — buf still valid
    println(buf.size)          // 100

    double_size(ref mut buf)  // mutable borrow
    println(buf.size)          // 200
}
```

### Borrowing rules

1. You can have **multiple immutable borrows** (`ref`) at the same time
2. You can have **only one mutable borrow** (`ref mut`) at a time
3. You cannot have immutable and mutable borrows simultaneously

These rules prevent data races at compile time.

### Drop trait

You can define custom cleanup logic with the `Drop` protocol:

```liva
struct Connection {
    var id: i32
}

impl Drop for Connection {
    func drop(mut self) {
        println("Closing connection \(self.id)")
    }
}

func main() {
    let conn = Connection { id: 1 }
    // ... use connection ...
}  // conn.drop() is called automatically here
```

---

## 15. Optional Types

Optional types represent values that may or may not exist — Liva's alternative to null pointers.

### Declaring optionals

```liva
func main() {
    let x: i32? = 42      // has a value
    let y: i32? = nil      // no value

    println(x)   // 42
    println(y)   // nil
}
```

The `?` suffix makes any type optional: `i32?`, `string?`, `Point?`, etc.

### Returning optionals

```liva
func find(arr: [i32], target: i32) -> i32? {
    for item in arr {
        if item == target {
            return item
        }
    }
    return nil
}

func main() {
    let result = find([1, 2, 3, 4, 5], 3)
    let missing = find([1, 2, 3], 99)

    println(result)   // 3
    println(missing)  // nil
}
```

### Nil coalescing (`??`)

Provide a default value when the optional is `nil`:

```liva
func main() {
    let name: string? = nil
    let display = name ?? "Anonymous"
    println(display)  // Anonymous

    let score: i32? = 95
    println(score ?? 0)  // 95
}
```

### Optional chaining (`?.`)

Safely access fields or methods on optional values:

```liva
struct User {
    var name: string
    var age: i32
}

func main() {
    var user: User? = User { name: "Alice", age: 30 }

    let name = user?.name ?? "Unknown"
    println(name)  // Alice

    user = nil
    let name2 = user?.name ?? "Unknown"
    println(name2)  // Unknown
}
```

### if-let binding

Unwrap an optional and bind it to a new variable:

```liva
func main() {
    let value: i32? = 42

    if let v = value {
        println("Got value: \(v)")
    } else {
        println("No value")
    }
}
```

### Force unwrap (`!`)

When you're absolutely sure an optional has a value, use `!` to unwrap it. **This crashes if the value is `nil`:**

```liva
func main() {
    let x: i32? = 42
    let value = x!       // OK — x has a value
    println(value)        // 42

    // let y: i32? = nil
    // let bad = y!       // CRASH: force unwrap of nil
}
```

Use `!` sparingly — prefer `??`, `if let`, or pattern matching instead.

---

## 16. Error Handling

Liva uses the `Result` type for error handling — no exceptions.

### Result type

```liva
enum FileError {
    case NotFound
    case PermissionDenied
    case Corrupted
}

func readFile(path: string) -> Result<string, FileError> {
    if path == "" {
        return Err(FileError.NotFound)
    }
    return Ok("file contents here")
}
```

### Handling results with match

```liva
func main() {
    let result = readFile("data.txt")

    match result {
        Ok(content) => {
            println("Read successfully:")
            println(content)
        }
        Err(FileError.NotFound) => {
            println("File not found!")
        }
        Err(FileError.PermissionDenied) => {
            println("Permission denied!")
        }
        Err(_) => {
            println("Unknown error")
        }
    }
}
```

### Propagating errors with `try`

Use `try` to propagate errors up the call stack:

```liva
func processFile(path: string) -> Result<i32, FileError> {
    let content = try readFile(path)
    // If readFile returns Err, processFile immediately returns that Err
    // If readFile returns Ok, content gets the unwrapped value
    return Ok(len(content))
}
```

---

## 17. Collections: Maps and Sets

### Maps (hash maps)

```liva
func main() {
    // Create a map
    var ages: Map<string, i32>

    // Insert key-value pairs
    ages.insert("Alice", 30)
    ages.insert("Bob", 25)
    ages.insert("Charlie", 35)

    // Lookup
    let alice_age = ages.get("Alice")
    println(alice_age)  // 30

    // Check existence
    println(ages.contains("Bob"))      // true
    println(ages.contains("Diana"))    // false

    // Remove
    ages.remove("Bob")

    // Size
    println(ages.size)      // 2
    println(ages.isEmpty)   // false

    // Iterate
    for (key, value) in ages {
        println(key)
    }
}
```

### Sets (hash sets)

```liva
func main() {
    var numbers: Set<i32>

    // Insert elements
    numbers.insert(10)
    numbers.insert(20)
    numbers.insert(30)
    numbers.insert(20)  // duplicate — ignored

    // Check membership
    println(numbers.contains(20))  // true
    println(numbers.contains(99))  // false

    // Remove
    numbers.remove(10)

    // Size
    println(numbers.size)      // 2
    println(numbers.isEmpty)   // false

    // Iterate
    for item in numbers {
        println(item)
    }
}
```

---

## 18. Tuples

Tuples group multiple values of different types into a single compound value.

### Creating and accessing tuples

```liva
func main() {
    let pair = (42, "hello")

    // Access by index
    println(pair.0)   // 42
    println(pair.1)   // hello
}
```

### Destructuring

```liva
func main() {
    let point = (3.0, 4.0)
    let (x, y) = point

    println(x)   // 3.0
    println(y)   // 4.0
}
```

### Multiple return values

Tuples are the idiomatic way to return multiple values from a function:

```liva
func divmod(a: i32, b: i32) -> (i32, i32) {
    return (a / b, a % b)
}

func minmax(arr: [i32]) -> (i32, i32) {
    var lo = arr[0]
    var hi = arr[0]
    for val in arr {
        if val < lo { lo = val }
        if val > hi { hi = val }
    }
    return (lo, hi)
}

func main() {
    let (quotient, remainder) = divmod(17, 5)
    println(quotient)    // 3
    println(remainder)   // 2

    let (lo, hi) = minmax([3, 1, 4, 1, 5, 9, 2, 6])
    println(lo)  // 1
    println(hi)  // 9
}
```

---

## 19. Modules and Imports

### Standard library modules

Liva provides built-in standard library modules:

```liva
import std::math      // abs, sqrt, pow, sin, cos, tan, floor, ceil, round, log, log10, min, max
import std::io        // readLine, readFile, writeFile, appendFile, fileExists
import std::convert   // parseInt, parseInt64, parseFloat, toString
import std::os        // env, args, exit, exec
import std::random    // randInt, randFloat
import std::regex     // Regex.new, match, replace, split
import http::http     // HttpRequest, HttpResponse, HttpClient
import std::json      // jsonParse, jsonStringify
import std::time      // now, clock, clockMs, sleep
import std::crypto    // sha256, md5, hmacSha256
import std::async     // taskSelect, withTimeout, async I/O
import std::path      // pathJoin, pathBasename, pathDirname
import std::ui        // raylib-based UI framework
```

You can also import the umbrella module:

```liva
import std   // imports everything from std::*
```

### Using modules

```liva
import std::math
import std::convert

func main() {
    let angle: f64 = 1.5708   // ~π/2
    println(sin(angle))        // ~1.0
    println(cos(0.0))          // 1.0

    let text = "42"
    if let num = parseInt(text) {
        println(num + 8)       // 50
    }
}
```

### User-defined modules

Create a file `math_utils.liva`:

```liva
pub func add(a: i32, b: i32) -> i32 {
    return a + b
}

pub func multiply(a: i32, b: i32) -> i32 {
    return a * b
}
```

Import it in another file:

```liva
import math_utils

func main() {
    let sum = add(3, 4)
    let product = multiply(5, 6)
    println(sum)       // 7
    println(product)   // 30
}
```

Use `pub` to make functions and types visible to importers.

---

## 20. Async/Await

Liva supports asynchronous programming with `async` and `await`.

### Async functions

```liva
import http::http

async func fetchData(url: string) -> string {
    let resp = HttpRequest.get(url).send()
    return resp.text()
}

async func fetchMultiple() {
    let data1 = await fetchData("https://api.example.com/a")
    let data2 = await fetchData("https://api.example.com/b")
    println(data1)
    println(data2)
}
```

### Key concepts

- Mark functions with `async` to make them asynchronous
- Use `await` to wait for an async operation to complete
- Async functions return a coroutine that is executed by the runtime
- `await` can only be used inside `async` functions

### For await (async iteration)

Use `for await` to iterate over async streams:

```liva
async func processItems() {
    for await item in asyncStream {
        println(item)
    }
}
```

Note: `for await` can only be used inside `async` functions.

### Networking example

```liva
import http::http

func main() {
    let resp = HttpRequest.get("https://api.example.com/data").send()
    if resp.is2xx() {
        println(resp.text())
    }
}
```

---

## 21. Advanced Features

### Type aliases

Create shorter names for complex types:

```liva
type Meters = f64
type Callback = (i32) -> void
type StringList = [string]

func measure(distance: Meters) {
    println(distance)
}

func main() {
    let d: Meters = 42.5
    measure(d)

    let names: StringList = ["Alice", "Bob"]
    for name in names {
        println(name)
    }
}
```

### Custom iterators

Implement the `Iterator` protocol to make your types work with `for-in`:

```liva
protocol Iterator {
    func hasNext(ref self) -> bool
    func next(ref mut self) -> i32
}

struct Range {
    var current: i32
    var end: i32
}

impl Iterator for Range {
    func hasNext(ref self) -> bool {
        return self.current < self.end
    }

    func next(ref mut self) -> i32 {
        let val = self.current
        self.current = self.current + 1
        return val
    }
}

func main() {
    var r = Range { current: 0, end: 5 }
    for val in r {
        println(val)
    }
    // Prints: 0 1 2 3 4
}
```

### Regex

```liva
import std::regex

func main() {
    let pattern = Regex.new("[0-9]+")
    let text = "Order 42 has 3 items"

    if pattern.match(text) {
        println("Found numbers!")
    }

    let cleaned = pattern.replace(text, "N")
    println(cleaned)  // Order N has N items
}
```

### Environment and process

```liva
import std::os

func main() {
    // Environment variables
    let home = env("HOME")
    println(home)

    // Command line arguments
    let arguments = args()
    for arg in arguments {
        println(arg)
    }

    // Exit with code
    exit(0)
}
```

### Random numbers

```liva
import std::random

func main() {
    let dice = randInt(1, 6)
    println(dice)

    let probability = randFloat(0.0, 1.0)
    println(probability)
}
```

### Comptime blocks

```liva
func main() {
    // Compile-time evaluation
    let lookup = comptime {
        var table: [i32] = []
        var i = 0
        while i < 5 {
            table.push(i * i)
            i = i + 1
        }
        table
    }
    // lookup is [0, 1, 4, 9, 16] — computed at compile time
    println(lookup)
}
```

### Macros

```liva
macro max_of {
    ($a, $b) => {
        if $a > $b { $a } else { $b }
    }
}

func main() {
    let biggest = max_of!(10, 20)
    println(biggest)  // 20
}
```

### Guard clauses

```liva
func divide(a: f64, b: f64) -> f64? {
    guard b != 0.0 else {
        println("Cannot divide by zero")
        return nil
    }
    return a / b
}
```

---

## 22. Project Management

### Creating a project

```bash
livac init myproject
cd myproject
```

This creates a `liva.toml` manifest file and a basic project structure.

### liva.toml

```toml
[project]
name = "myproject"
version = "0.1.0"
entry = "src/main.liva"

[build]
optimization = "debug"      # or "release"
extra_flags = ["-lm"]       # extra linker flags

[dependencies]
json_parser = "^1.0.0"      # semantic versioning
utils = "~2.3.0"
```

### Version constraints

| Syntax | Meaning | Example |
|--------|---------|---------|
| `^1.2.3` | Compatible (same major) | 1.2.3 ≤ v < 2.0.0 |
| `~1.2.3` | Patch-level (same minor) | 1.2.3 ≤ v < 1.3.0 |
| `=1.2.3` | Exact version | Only 1.2.3 |
| `>=1.0.0` | Minimum version | 1.0.0 or newer |

### Building and running

```bash
# Build the project
livac build

# Build in release mode
livac build --release

# Run the project
livac run
```

### Lock file

After resolving dependencies, Liva creates a `liva.lock` file that pins exact versions for reproducible builds. Commit this file to version control.

---

## 23. Tooling

### LSP Server

Liva includes a Language Server Protocol (LSP) server for editor integration:

```bash
livac lsp
```

**Supported features:**
- Diagnostics (errors and warnings)
- Code completion (keywords, built-ins, symbols)
- Hover information (function signatures, variable types)
- Go to definition
- Find all references
- Rename symbol
- Signature help (parameter hints)
- Document symbols (outline)
- Code Actions (quick-fix suggestions)
- Code Lens (reference counts)
- Call Hierarchy (incoming/outgoing calls)
- Inlay Hints (inline type annotations)
- Selection Range (smart selection)
- Workspace Symbol (project-wide search)

#### VS Code setup

Add to your VS Code `settings.json`:

```json
{
    "liva.serverPath": "/path/to/livac",
    "liva.serverArgs": ["lsp"]
}
```

### Interactive REPL

```bash
livac repl
```

The REPL lets you experiment with Liva interactively:

```
Liva REPL v0.1.0
Type :help for help, :quit to exit.
>>> let x = 42
>>> x + 8
50
>>> func double(n: i32) -> i32 { return n * 2 }
>>> double(x)
84
```

**REPL commands:**
| Command | Shortcut | Description |
|---------|----------|-------------|
| `:help` | `:h` | Show help |
| `:quit` | `:q` | Exit the REPL |
| `:reset` | `:r` | Clear all declarations |
| `:declarations` | `:decls` | Show accumulated declarations |

**Multi-line input:** The REPL automatically detects incomplete input (unclosed braces, parentheses) and waits for more lines:

```
>>> func factorial(n: i32) -> i32 {
...     if n <= 1 {
...         return 1
...     }
...     return n * factorial(n - 1)
... }
>>> factorial(10)
3628800
```

### Benchmarking

```bash
livac bench
```

Run performance benchmarks defined in your code:

```liva
import std::bench

func main() {
    benchStart("fibonacci")
    for i in 0..1000 {
        benchIter("fibonacci")
        fibonacci(20)
    }
    benchDone("fibonacci")
    benchReport()
}
```

### Test Runner

```bash
livac test
```

Run all `test` blocks in your project. See [Testing Your Code](#26-testing-your-code) for details.

### Compiler diagnostic options

```bash
livac --dump-tokens file.liva    # Show all tokens
livac --dump-ast file.liva       # Show the AST
livac --check-only file.liva     # Type-check without generating code
livac --emit-ir file.liva        # Output LLVM IR
```

---

## 24. What's Next?

Congratulations — you've covered the core of the Liva programming language! Here are some suggestions for going deeper:

### Practice projects

1. **Calculator:** Parse and evaluate arithmetic expressions
2. **Todo list:** A CLI app using structs, arrays, and file I/O
3. **HTTP client:** Fetch and display data from a web API using `std::net`
4. **Data processor:** Read a CSV file, parse it, compute statistics
5. **Mini game:** A text-based number guessing game with random numbers

### Example: Complete mini-program

Here's a complete program that demonstrates many features together:

```liva
import std::math
import std::convert

struct Student {
    var name: string
    var grades: [f64]
}

impl Student {
    func new(name: string) -> Student {
        return Student { name: name, grades: [] }
    }

    func addGrade(ref mut self, grade: f64) {
        self.grades.push(grade)
    }

    func average(ref self) -> f64? {
        if self.grades.isEmpty {
            return nil
        }
        var sum: f64 = 0.0
        for g in self.grades {
            sum = sum + g
        }
        return sum / (self.grades.length as f64)
    }

    func highest(ref self) -> f64? {
        if self.grades.isEmpty {
            return nil
        }
        var best = self.grades[0]
        for g in self.grades {
            if g > best {
                best = g
            }
        }
        return best
    }
}

func letterGrade(score: f64) -> string {
    match score {
        s if s >= 90.0 => "A"
        s if s >= 80.0 => "B"
        s if s >= 70.0 => "C"
        s if s >= 60.0 => "D"
        _ => "F"
    }
}

func main() {
    var student = Student.new("Alice")
    student.addGrade(92.5)
    student.addGrade(87.0)
    student.addGrade(95.3)
    student.addGrade(78.8)

    println("Student: \(student.name)")
    println("Grades: \(student.grades.length)")

    if let avg = student.average() {
        let rounded = round(avg, 1)
        println("Average: \(rounded)")
        println("Letter: \(letterGrade(avg))")
    }

    if let best = student.highest() {
        println("Highest: \(best)")
    }
}
```

### Further reading

- [Language Reference](language-reference.md) — Complete syntax and semantics reference
- [Contributing](CONTRIBUTING.md) — How to contribute to Liva
- [Examples](examples/) — More example programs
- [API Reference](API-REFERENCE.md) — Standard library module reference
- [Cookbook](COOKBOOK.md) — Common patterns and recipes

---

## 25. Classes and OOP

Classes in Liva are reference types with support for inheritance, constructors, destructors, and virtual method dispatch.

### Defining a class

```liva
class Animal {
    var name: string
    var sound: string

    init(name: string, sound: string) {
        self.name = name
        self.sound = sound
    }

    deinit {
        println("\(self.name) freed")
    }

    func speak(ref self) {
        println("\(self.name) says \(self.sound)")
    }
}

func main() {
    let cat = Animal("Whiskers", "Meow")
    cat.speak()  // Whiskers says Meow
}
```

### Inheritance and override

```liva
class Dog : Animal {
    var breed: string

    init(name: string, breed: string) {
        super.init(name, "Woof")
        self.breed = breed
    }

    override func speak(ref self) {
        println("\(self.name) the \(self.breed) says Woof!")
    }
}

func main() {
    let dog = Dog("Rex", "Labrador")
    dog.speak()  // Rex the Labrador says Woof!
}
```

### Private fields

```liva
class BankAccount {
    private var balance: f64

    init(initial: f64) {
        self.balance = initial
    }

    func deposit(ref mut self, amount: f64) {
        self.balance = self.balance + amount
    }

    func getBalance(ref self) -> f64 {
        return self.balance
    }
}

func main() {
    var account = BankAccount(100.0)
    account.deposit(50.0)
    println(account.getBalance())  // 150.0
    // account.balance  // ERROR: 'balance' is private
}
```

### When to use class vs struct

Use **struct** for simple data containers (value semantics, no inheritance needed).
Use **class** when you need inheritance, virtual dispatch, or reference semantics.

### Beyond the basics

Classes support a rich Swift-style feature set:

- **Static members**: `static func`, `static var` — type-level, no `self`.
- **Computed properties**: `var area: f64 { get { ... } set { ... } }`.
- **Property observers**: `willSet { ... } didSet { ... }` on stored fields.
- **Final classes/methods**: `final class` and `final func` prevent further inheritance/override.
- **Type checks**: `expr is Type` returns `bool`; `expr as? Type` yields `Type?`.
- **Failable init**: `init?(...)` may `return nil` to signal failure; result type is `ClassName?`.
- **Multiple init / convenience**: classes may declare a designated `init(...)` plus
  `convenience init(...)` overloads, resolved by argument count.
- **Lazy properties**: `lazy var x: T = expr` is computed on first access and cached.
- **Subscripts**: `subscript(i: T) -> U { get { } set { } }` overloads `obj[i]`;
  generic subscripts are supported via `subscript<T>(...)`.
- **Access levels**: `open`, `public`, `internal`, `fileprivate`, `private` (Swift-style).
  Only `open` classes can be subclassed.
- **Extensions**: `extension TypeName { func ... }` adds methods to an existing type.

Quick example:

```liva
final class Circle {
    var radius: f64
    init(r: f64) { self.radius = r }

    var area: f64 {
        get { return self.radius * self.radius * 3.14159 }
    }

    static func unit() -> Circle {
        return Circle(1.0)
    }
}

class Shape { func name(ref self) -> string { return "Shape" } }
class Square : Shape {
    override func name(ref self) -> string { return "Square" }
}

func describe(s: Shape) {
    if s is Square {
        println("it is a square")
    }
}
```

See the [Language Reference](LANGUAGE-REFERENCE.md#28-classes) for the full
specification and the [Cookbook](COOKBOOK.md#19-swift-style-class-features)
for idiomatic recipes.

---

## 26. Testing Your Code

Liva has a built-in test framework that lets you write test blocks directly in your source files.

### Writing tests

```liva
func add(a: i32, b: i32) -> i32 {
    return a + b
}

test "add returns correct sum" {
    assert(add(2, 3) == 5)
    assert(add(-1, 1) == 0)
    assert(add(0, 0) == 0)
}

test "add with large numbers" {
    let result = add(1000000, 2000000)
    assert(result == 3000000)
}
```

### Running tests

```bash
livac test                          # run all tests
livac test --filter "add"           # run tests matching pattern
```

Each test runs in isolation — a failure in one test does not prevent other tests from running.

---

## 27. Compile-Time Features

### Comptime blocks

Evaluate expressions at compile time:

```liva
func main() {
    let pi_approx = comptime {
        var sum: f64 = 0.0
        var i = 0
        while i < 1000 {
            let term = 1.0 / (2.0 * (i as f64) + 1.0)
            if i % 2 == 0 {
                sum = sum + term
            } else {
                sum = sum - term
            }
            i = i + 1
        }
        sum * 4.0
    }
    println(pi_approx)
}
```

### Macros

Define reusable code patterns:

```liva
macro unless {
    ($cond, $body) => {
        if !($cond) { $body }
    }
}

func main() {
    let x = 5
    unless!(x > 10, {
        println("x is not greater than 10")
    })
}
```

---

## New Language Features (v1.0)

### Const Generics

```liva
func repeat<const N: i32>(value: i32) -> i32 {
    return N * value
}
let r = repeat<5>(3)  // 15
```

### Explicit Lifetimes

```liva
func first<'a>(x: ref 'a i32) -> ref 'a i32 {
    return x
}
// Elision: func identity(x: ref i32) -> ref i32 { return x }
```

### Generators

```liva
func fibonacci() {
    var a = 0
    var b = 1
    while true {
        yield a
        let tmp = a; a = b; b = tmp + b
    }
}
```

### Enum Discriminant Values

```liva
enum HttpStatus {
    case OK = 200
    case NotFound = 404
}
```

---

*This tutorial covers Liva as of version 0.3.0 with 2149 tests. The language is under active development — new features and improvements are added regularly.*
