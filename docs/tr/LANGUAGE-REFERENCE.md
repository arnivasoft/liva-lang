# Liva Dil Referansı

Liva programlama dili için kapsamlı bir referans — Swift benzeri sözdizimi ve Rust tarzı ownership semantiğine sahip, statik tipli, derlenebilir bir dil.

---

## İçindekiler

1. [Sözdizimsel Yapı](#1-sözdizimsel-yapı)
2. [Tipler](#2-tipler)
3. [Değişkenler ve Sabitler](#3-değişkenler-ve-sabitler)
4. [Operatörler](#4-operatörler)
5. [Kontrol Akışı](#5-kontrol-akışı)
6. [Fonksiyonlar](#6-fonksiyonlar)
7. [Closure'lar](#7-closurelar)
8. [Struct'lar](#8-structlar)
9. [Enum'lar ve Pattern Matching](#9-enumlar-ve-pattern-matching)
10. [Protocol'ler (Trait'ler)](#10-protocoller-traitler)
11. [Generics](#11-generics)
12. [Ownership ve Borrowing](#12-ownership-ve-borrowing)
13. [Optional Tipler](#13-optional-tipler)
14. [Hata Yönetimi](#14-hata-yönetimi)
15. [Koleksiyonlar](#15-koleksiyonlar)
16. [Tuple'lar](#16-tuplelar)
17. [Type Alias'lar](#17-type-aliaslar)
18. [String İşlemleri](#18-string-işlemleri)
19. [Modüller ve Import'lar](#19-modüller-ve-importlar)
20. [Async/Await](#20-asyncawait)
21. [Operator Overloading](#21-operator-overloading)
22. [Özel Iterator'lar](#22-özel-iteratorlar)
23. [Drop Trait (Destructor'lar)](#23-drop-trait-destructorlar)
24. [Standart Kütüphane](#24-standart-kütüphane)
25. [Yerleşik Fonksiyonlar](#25-yerleşik-fonksiyonlar)
26. [Proje Konfigürasyonu](#26-proje-konfigürasyonu)
27. [Derleyici Seçenekleri](#27-derleyici-seçenekleri)
28. [Sınıflar (Classes)](#28-sınıflar-classes)
29. [FFI (Yabancı Fonksiyon Arayüzü)](#29-ffi-yabancı-fonksiyon-arayüzü)
30. [Derleme Zamanı Değerlendirme](#30-derleme-zamanı-değerlendirme)
31. [Macro'lar](#31-macrolar)
32. [Test Framework](#32-test-framework)
33. [Eşzamanlılık](#33-eşzamanlılık)
34. [dyn Protocol (Trait Nesneleri)](#34-dyn-protocol-trait-nesneleri)
35. [Guard Koşulları](#35-guard-koşulları)

---

## 1. Sözdizimsel Yapı

### Yorumlar

```liva
// Tek satırlık yorum

/* Blok yorumu
   birden fazla satıra yayılabilir */
```

### Tanımlayıcılar

Tanımlayıcılar bir harf veya alt çizgi ile başlar, ardından harfler, rakamlar veya alt çizgiler gelebilir. Anahtar kelimeler tanımlayıcı olarak kullanılamaz.

### Anahtar Kelimeler

**Kontrol Akışı:**
`if`, `else`, `while`, `for`, `in`, `match`, `case`, `break`, `continue`, `return`, `guard`

**Bildirimler:**
`func`, `let`, `var`, `const`, `struct`, `enum`, `impl`, `protocol`, `type`, `import`, `pub`

**Bellek:**
`ref`, `mut`, `self`

**Değerler:**
`true`, `false`, `nil`

**Niteleyiciler:**
`as`, `where`, `async`, `await`, `try`

### Literal'ler

#### Integer Literal'ler

```liva
let decimal = 42
let hex = 0xFF
let binary = 0b1010
let octal = 0o77
let negative = -10
```

#### Float Literal'ler

```liva
let pi = 3.14159
let scientific = 1.5e10
let small = 2.5e-3
```

#### String Literal'ler

```liva
let simple = "Hello, World!"
let escaped = "satır1\nsatır2\ttab"
let unicode = "emoji: \u{1F600}"
let interpolated = "değer: \(x + 1)"
```

**Kaçış dizileri:** `\\`, `\"`, `\n`, `\r`, `\t`, `\0`, `\u{XXXX}`

**String interpolation:** `\(ifade)` herhangi bir ifadeyi string içine gömer.

#### Çok Satırlı String'ler

```liva
let text = "birinci satır
ikinci satır
üçüncü satır"
```

#### Karakter Literal'leri

```liva
let ch = 'A'
```

#### Boolean Literal'ler

```liva
let yes = true
let no = false
```

---

## 2. Tipler

### Primitif Tipler

| Tip | Açıklama | Boyut |
|-----|----------|-------|
| `i8` | İşaretli 8-bit tamsayı | 1 byte |
| `i16` | İşaretli 16-bit tamsayı | 2 byte |
| `i32` | İşaretli 32-bit tamsayı | 4 byte |
| `i64` | İşaretli 64-bit tamsayı | 8 byte |
| `u8` | İşaretsiz 8-bit tamsayı | 1 byte |
| `u16` | İşaretsiz 16-bit tamsayı | 2 byte |
| `u32` | İşaretsiz 32-bit tamsayı | 4 byte |
| `u64` | İşaretsiz 64-bit tamsayı | 8 byte |
| `f32` | 32-bit kayan nokta | 4 byte |
| `f64` | 64-bit kayan nokta | 8 byte |
| `bool` | Boolean | 1 byte |
| `string` | UTF-8 string | pointer |
| `void` | Değer yok | 0 byte |

### Bileşik Tipler

| Sözdizimi | Açıklama |
|-----------|----------|
| `[T]` | T'nin dinamik dizisi |
| `(T, U)` | T ve U'nun tuple'ı |
| `T?` | Optional T (nil olabilir) |
| `Result<T, E>` | Başarı T veya hata E |
| `(T) -> U` | T'den U'ya fonksiyon |
| `ref T` | T'ye değişmez referans |
| `ref mut T` | T'ye değişebilir referans |
| `Map<K, V>` | K'dan V'ye hash map |
| `Set<T>` | T'nin hash set'i |

### Tip Dönüştürme

```liva
let x: i32 = 42
let y = x as i64      // integer genişletme
let z = x as f64      // integer'dan float'a
```

`as` anahtar kelimesi sayısal tipler arasında açık tip dönüşümü yapar.

---

## 3. Değişkenler ve Sabitler

### Değişmez Bağlamalar (`let`)

```liva
let x: i32 = 42        // açık tip
let name = "Liva"      // çıkarımlı tip
```

`let` bağlamaları başlatıldıktan sonra yeniden atanamaz.

### Değişebilir Bağlamalar (`var`)

```liva
var count: i32 = 0
count = count + 1       // OK: var değişebilir

var name = "hello"
name = "world"          // OK
```

### Derleme Zamanı Sabitleri (`const`)

```liva
const MAX_SIZE: i32 = 100
const PI: f64 = 3.14159
const GREETING: string = "Hello"
```

`const` değerleri derleme zamanında hesaplanabilir olmalıdır. Başlatıcı derleyici tarafından değerlendirilir ve değer her kullanım noktasına inline edilir.

**Geçerli `const` başlatıcılar:**
- Integer, float, bool ve string literal'ler
- Sabitler üzerinde tekli işlemler (`-42`)
- Sabitler üzerinde ikili işlemler (`10 + 20`, `3.14 * 2.0`)
- Sabitler üzerinde cast ifadeleri (`42 as i64`)

### Tuple Destructuring

```liva
let (x, y) = (10, 20)
let (quotient, remainder) = divmod(17, 5)
```

---

## 4. Operatörler

### Aritmetik Operatörler

| Operatör | Açıklama | Örnek |
|----------|----------|-------|
| `+` | Toplama | `a + b` |
| `-` | Çıkarma | `a - b` |
| `*` | Çarpma | `a * b` |
| `/` | Bölme | `a / b` |
| `%` | Modül | `a % b` |
| `-` (tekli) | Olumsuzlama | `-a` |

### Karşılaştırma Operatörleri

| Operatör | Açıklama |
|----------|----------|
| `==` | Eşit |
| `!=` | Eşit değil |
| `<` | Küçüktür |
| `<=` | Küçük eşit |
| `>` | Büyüktür |
| `>=` | Büyük eşit |

### Mantıksal Operatörler

| Operatör | Açıklama |
|----------|----------|
| `&&` | Mantıksal VE (kısa devre) |
| `\|\|` | Mantıksal VEYA (kısa devre) |
| `!` | Mantıksal DEĞİL |

### Bit Düzeyinde Operatörler

| Operatör | Açıklama |
|----------|----------|
| `&` | Bit düzeyinde VE |
| `\|` | Bit düzeyinde VEYA |
| `^` | Bit düzeyinde XOR |
| `~` | Bit düzeyinde DEĞİL |
| `<<` | Sola kaydırma |
| `>>` | Sağa kaydırma |

### Atama Operatörleri

| Operatör | Eşdeğer |
|----------|---------|
| `=` | Basit atama |
| `+=` | `a = a + b` |
| `-=` | `a = a - b` |
| `*=` | `a = a * b` |
| `/=` | `a = a / b` |
| `%=` | `a = a % b` |

### Özel Operatörler

| Operatör | Açıklama | Örnek |
|----------|----------|-------|
| `??` | Nil coalescing | `x ?? varsayılan` |
| `?.` | Optional chaining | `obj?.alan` |
| `!` (sonek) | Force unwrap | `optional!` |
| `as` | Tip dönüştürme | `x as i64` |
| `..` | Aralık | `0..10` |
| `...` | Dahil aralık / variadic | `0...10` |
| `? :` | Ternary koşul | `koşul ? a : b` |

### Operatör Önceliği (yüksekten düşüğe)

1. Sonek: `()`, `[]`, `.`, `?.`, `!`
2. Önek: `-`, `!`, `~`, `ref`, `ref mut`
3. Cast: `as`
4. Çarpımsal: `*`, `/`, `%`
5. Toplamsal: `+`, `-`
6. Kaydırma: `<<`, `>>`
7. Bit düzeyinde VE: `&`
8. Bit düzeyinde XOR: `^`
9. Bit düzeyinde VEYA: `|`
10. Karşılaştırma: `<`, `<=`, `>`, `>=`
11. Eşitlik: `==`, `!=`
12. Mantıksal VE: `&&`
13. Mantıksal VEYA: `||`
14. Nil coalescing: `??`
15. Ternary: `? :`
16. Atama: `=`, `+=`, `-=`, `*=`, `/=`, `%=`

---

## 5. Kontrol Akışı

### If / Else

```liva
if koşul {
    // then dalı
}

if x > 0 {
    println("pozitif")
} else if x == 0 {
    println("sıfır")
} else {
    println("negatif")
}
```

Koşullar `bool` tipinde olmalıdır. Koşul etrafında parantezler isteğe bağlıdır (ama süslü parantezler zorunludur).

### While Döngüsü

```liva
var i = 0
while i < 10 {
    println(i)
    i = i + 1
}
```

### For-in Döngüsü

```liva
// Aralık üzerinde iterasyon
for i in 0..10 {
    println(i)
}

// Dizi üzerinde iterasyon
let items = [10, 20, 30]
for item in items {
    println(item)
}

// Tuple destructuring ile map üzerinde iterasyon
var m: Map<string, i32>
m.insert("a", 1)
for (key, value) in m {
    println(key)
}
```

### Break ve Continue

```liva
var i = 0
while true {
    if i >= 10 { break }
    i = i + 1
    if i % 2 == 0 { continue }
    println(i)
}
```

### Guard

```liva
func process(x: i32) {
    guard x > 0 else {
        println("pozitif olmalı")
        return
    }
    println(x)
}
```

`guard` ifadesi, kapsamdan çıkması gereken (return`, `break` veya `continue` ile) bir `else` bloğu gerektirir.

### Match İfadesi

```liva
match value {
    0 => println("sıfır")
    1 => println("bir")
    _ => println("diğer")
}
```

Tam ayrıntılar için [Enum'lar ve Pattern Matching](#9-enumlar-ve-pattern-matching) bölümüne bakın.

### Ternary İfade

```liva
let result = koşul ? doğruysa_değer : yanlışsa_değer
let abs_val = x < 0 ? -x : x
```

### If-let (Optional Bağlama)

```liva
let opt: i32? = 42

if let value = opt {
    println(value)       // value i32'dir (unwrap edilmiş)
} else {
    println("nil idi")
}
```

### While-let

```liva
while let item = queue.pop() {
    process(item)
}
```

---

## 6. Fonksiyonlar

### Temel Sözdizimi

```liva
func fonksiyonAdı(param1: Tip1, param2: Tip2) -> DönüşTipi {
    // gövde
    return değer
}
```

### Örnekler

```liva
// Basit fonksiyon
func add(a: i32, b: i32) -> i32 {
    return a + b
}

// Void dönüş (dönüş tipi belirtilmez)
func greet(name: string) {
    println("Hello, \(name)!")
}

// Parametresiz
func getVersion() -> string {
    return "1.0.0"
}
```

### Varsayılan Parametreler

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

### Variadic Fonksiyonlar

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

Variadic parametre son parametre olmalıdır. Fonksiyon içinde bir dizi gibi davranır.

### Referans Parametreler

```liva
// Değişmez borrow — okuyabilir ama değiştiremez
func read(x: ref i32) -> i32 {
    return x
}

// Değişebilir borrow — değiştirebilir
func increment(x: ref mut i32) {
    x = x + 1
}

var n = 10
increment(ref mut n)   // n artık 11
let v = read(ref n)    // v = 11
```

### Public Fonksiyonlar

```liva
pub func publicAPI(x: i32) -> i32 {
    return x * 2
}
```

`pub` anahtar kelimesi bir fonksiyonu diğer modüllere görünür kılar.

### Async Fonksiyonlar

```liva
async func fetchData(url: string) -> string {
    let response = await httpGet(url)
    return response
}
```

Ayrıntılar için [Async/Await](#20-asyncawait) bölümüne bakın.

---

## 7. Closure'lar

### Closure Sözdizimi

```liva
// Tam sözdizimi
let double = |x: i32| -> i32 { return x * 2 }

// Çıkarımlı dönüş tipi
let triple = |x: i32| { return x * 3 }

// Çok satırlı closure
let process = |x: i32, y: i32| -> i32 {
    let sum = x + y
    return sum * 2
}
```

### Fonksiyon Tipi Belirtimleri

```liva
func apply(x: i32, f: (i32) -> i32) -> i32 {
    return f(x)
}

let result = apply(5, |x: i32| -> i32 { return x * 2 })
```

### Higher-Order Fonksiyonlar

```liva
let numbers = [1, 2, 3, 4, 5]

// forEach
numbers.forEach(|x: i32| {
    println(x)
})

// map — her elemanı dönüştür
let doubled = numbers.map(|x: i32| -> i32 { return x * 2 })

// filter — eşleşen elemanları tut
let evens = numbers.filter(|x: i32| -> bool { return x % 2 == 0 })

// reduce — bir değer biriktir
let sum = numbers.reduce(0, |acc: i32, x: i32| -> i32 { return acc + x })
```

### Trailing Closure Sözdizimi

Bir fonksiyonun son parametresi closure olduğunda, parantezlerden sonra yazabilirsiniz:

```liva
numbers.forEach { |x|
    println(x)
}

numbers.map { |x: i32| -> i32
    return x * 2
}
```

### Capture Semantiği

Closure'lar çevreleyen kapsamdaki değişkenleri yakalar:

```liva
var counter = 0
let increment = || {
    counter = counter + 1   // 'counter'ı referans ile yakalar
}
```

---

## 8. Struct'lar

### Bildirim

```liva
struct Point {
    var x: f64
    var y: f64
}
```

`var` ile bildirilen alanlar değişebilir; `let` ile bildirilen alanlar değişmezdir.

### Örnek Oluşturma

```liva
let p = Point { x: 3.0, y: 4.0 }
```

Oluşturma sırasında tüm alanlar sağlanmalıdır.

### Impl Blokları (Metodlar)

```liva
impl Point {
    // Statik metod (self yok)
    func new(x: f64, y: f64) -> Point {
        return Point { x: x, y: y }
    }

    // Değişmez metod (ref self)
    func magnitude(ref self) -> f64 {
        return sqrt(self.x * self.x + self.y * self.y)
    }

    // Değişebilir metod (ref mut self)
    func translate(ref mut self, dx: f64, dy: f64) {
        self.x = self.x + dx
        self.y = self.y + dy
    }

    // Tüketici metod (self — ownership alır)
    func consume(self) {
        println(self.x)
    }

    // Değişebilir self (mut self)
    func drop(mut self) {
        // temizlik
    }
}
```

### Self Parametreleri

| Parametre | Erişim | Ownership |
|-----------|--------|-----------|
| `self` | Salt okunur | Ownership alır (move) |
| `ref self` | Salt okunur | Değişmez borrow |
| `ref mut self` | Okuma/yazma | Değişebilir borrow |
| `mut self` | Okuma/yazma | Ownership alır (değiştirebilir) |

### Metod Çağrısı

```liva
var p = Point.new(3.0, 4.0)
let dist = p.magnitude()     // otomatik ref self borrow
p.translate(1.0, 2.0)        // otomatik ref mut self borrow
```

### Generic Struct'lar

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

### Public Struct'lar

```liva
pub struct Config {
    var debug: bool
    var port: i32
}
```

---

## 9. Enum'lar ve Pattern Matching

### Basit Enum'lar

```liva
enum Direction {
    case North
    case South
    case East
    case West
}

let d = Direction.North
```

### Associated Value'lu Enum'lar

```liva
enum Shape {
    case Circle(f64)             // yarıçap
    case Rectangle(f64, f64)     // genişlik, yükseklik
    case Empty
}

let s = Shape.Circle(3.14)
let r = Shape.Rectangle(10.0, 20.0)
```

### Match İfadeleri

```liva
match shape {
    Shape.Circle(r) => {
        println("Yarıçapı \(r) olan daire")
    }
    Shape.Rectangle(w, h) => {
        let area = w * h
        println("Alan: \(area)")
    }
    Shape.Empty => println("Boş şekil")
}
```

### Wildcard ile Match

```liva
match value {
    0 => println("sıfır")
    1 => println("bir")
    _ => println("başka bir şey")    // wildcard her şeyle eşleşir
}
```

### Guard'lı Match

```liva
match x {
    n if n > 0 => println("pozitif")
    n if n < 0 => println("negatif")
    _ => println("sıfır")
}
```

### İç İçe Pattern Matching

```liva
enum Wrapper {
    case Some(Shape)
    case None
}

match wrapper {
    Wrapper.Some(Shape.Circle(r)) => println(r)
    Wrapper.Some(Shape.Rectangle(w, h)) => println(w * h)
    Wrapper.None => println("hiçbir şey")
    _ => println("diğer")
}
```

### Kapsamlılık

Derleyici tüm enum case'lerinin kapsandığını kontrol eder. Tüm case'ler listelenmemişse bir wildcard `_` kolu gereklidir.

---

## 10. Protocol'ler (Trait'ler)

### Bildirim

```liva
protocol Printable {
    func toString(ref self) -> string
}

protocol Shape {
    func area(ref self) -> f64
    func perimeter(ref self) -> f64
}
```

### Uygulama

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

### Varsayılan Uygulamalar

```liva
protocol Describable {
    func name(ref self) -> string

    // Varsayılan uygulama
    func describe(ref self) -> string {
        return "Nesne: " + self.name()
    }
}
```

`Describable` uygulayan tipler sadece `name()` sağlamalıdır; `describe()` geçersiz kılınmadıkça miras alınır.

### Tip Kısıtlaması Olarak Protocol

```liva
func printShape<T: Shape>(s: ref T) {
    println(s.area())
}
```

### Çoklu Trait Bound

```liva
func display<T: Printable + Shape>(item: ref T) {
    println(item.toString())
    println(item.area())
}
```

### Where Clause'lar

```liva
func process<T>(item: ref T) where T: Printable, T: Shape {
    println(item.toString())
}
```

### Associated Type'lar

```liva
protocol Iterator {
    type Item
    func next(ref mut self) -> Item?
}
```

---

## 11. Generics

### Generic Fonksiyonlar

```liva
func identity<T>(x: T) -> T {
    return x
}

let a = identity(42)       // T, i32 olarak çıkarılır
let b = identity("hello")  // T, string olarak çıkarılır
```

### Generic Struct'lar

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

### Kısıtlanmış Generics

```liva
func largest<T: Comparable>(a: T, b: T) -> T {
    if a > b {
        return a
    }
    return b
}
```

### Where Clause'lar

```liva
func combine<T, U>(a: T, b: U) -> string where T: Printable, U: Printable {
    return a.toString() + " " + b.toString()
}
```

---

## 12. Ownership ve Borrowing

Liva, garbage collector olmadan bellek güvenliğini sağlamak için Rust'tan esinlenen bir ownership sistemi kullanır.

### Ownership Kuralları

1. Her değerin tam olarak bir sahibi vardır
2. Sahip kapsam dışına çıktığında, değer drop edilir
3. Ownership başka bir değişkene veya fonksiyona transfer edilebilir (move)

### Move Semantics

```liva
struct Buffer {
    var size: i32
}

func consume(buf: Buffer) {
    println(buf.size)
    // buf bu fonksiyonun sonunda drop edilir
}

var buf = Buffer { size: 1024 }
consume(buf)          // ownership consume() fonksiyonuna geçer
// println(buf.size)  // HATA: taşınmış 'buf' değerinin kullanımı
```

Bir struct'ı değer olarak fonksiyona geçirmek ownership'i **taşır**. Taşımadan sonra orijinal değişken artık geçerli değildir.

### Copy Tipler

Primitif tipler (`i32`, `f64`, `bool`, vb.) taşınmak yerine örtük olarak kopyalanır:

```liva
let x = 42
let y = x      // x kopyalanır, hem x hem y geçerli
println(x)     // OK
println(y)     // OK
```

### Borrowing

Borrowing, ownership almadan bir değeri kullanmanızı sağlar:

```liva
// Değişmez borrow (salt okunur)
func read(data: ref Buffer) {
    println(data.size)     // okuyabilir
    // data.size = 10      // HATA: değişmez referans üzerinden değiştirilemez
}

// Değişebilir borrow (okuma + yazma)
func modify(data: ref mut Buffer) {
    data.size = data.size + 1   // OK
}

var buf = Buffer { size: 100 }
read(ref buf)           // değişmez borrow
modify(ref mut buf)     // değişebilir borrow
println(buf.size)       // buf hala geçerli (101)
```

### Borrowing Kuralları

1. Aynı anda **birden fazla değişmez borrow** (`ref T`) olabilir
2. Aynı anda **tam olarak bir değişebilir borrow** (`ref mut T`) olabilir
3. Değişmez borrow'lar varken değişebilir borrow olamaz

### Lifetime Analizi

Derleyici, referansların işaret ettikleri değerlerden daha uzun yaşamadığını doğrular:

```liva
// HATA: borrow değerden daha uzun yaşıyor
func bad() -> ref i32 {
    let x = 42
    return ref x    // x fonksiyon sonunda drop edilir
}
```

---

## 13. Optional Tipler

### Bildirim

```liva
let x: i32? = 42       // 42 içeren optional
let y: i32? = nil       // hiçbir şey içermeyen optional
```

### Nil Coalescing

```liva
let value = x ?? 0     // 42 (nil olmadığı için x'in değerini kullanır)
let other = y ?? -1    // -1 (y nil olduğu için varsayılanı kullanır)
```

### Optional Chaining

```liva
struct User {
    var name: string
    var age: i32
}

let user: User? = getUser()
let name = user?.name       // string? — user nil ise nil
let age = user?.age ?? 0   // chain + coalesce
```

### Force Unwrap

```liva
let value = x!     // x nil ise runtime'da panik
```

Force unwrap'ı yalnızca değerin nil olmadığından emin olduğunuzda kullanın.

### If-let Bağlama

```liva
if let value = optionalValue {
    // value unwrap edilmiş nil olmayan değerdir
    println(value)
} else {
    println("nil idi")
}
```

### While-let Bağlama

```liva
while let item = iterator.next() {
    process(item)
}
```

---

## 14. Hata Yönetimi

### Result Tipi

```liva
enum FileError {
    case NotFound
    case PermissionDenied
}

func readFile(path: string) -> Result<string, FileError> {
    // Ok(...) veya Err(...) döndür
}
```

### Result Üzerinde Pattern Matching

```liva
let result = readFile("data.txt")
match result {
    Ok(content) => println(content)
    Err(FileError.NotFound) => println("Dosya bulunamadı")
    Err(FileError.PermissionDenied) => println("Erişim reddedildi")
}
```

### Try İfadesi

```liva
func processFile() -> Result<string, FileError> {
    let content = try readFile("input.txt")   // Err ise hatayı yayar
    return Ok(content)
}
```

### Postfix `?` Operatörü

`?` operatörü, `Result` tipleri üzerinde hata yayılımının kısaltmasıdır:

```liva
func processFile() -> Result<string, FileError> {
    let content = readFile("input.txt")?   // başarısız olursa erken Err döner
    return Ok(content)
}
```

`?` operatörü `Ok(T)` değerini açar veya `Err(E)` değerini çevreleyen fonksiyondan anında döndürür.

---

## 15. Koleksiyonlar

### Diziler

```liva
// Dizi literal'i
let numbers: [i32] = [1, 2, 3, 4, 5]
let empty: [i32] = []

// Tip çıkarımı
let names = ["Alice", "Bob", "Charlie"]

// İndeksle erişim (0 tabanlı)
let first = numbers[0]      // 1
let last = numbers[4]       // 5

// Uzunluk
let count = len(numbers)     // 5

// Dilimleme
let slice = numbers[1..4]    // [2, 3, 4]
```

#### Dizi Metodları

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

#### Higher-Order Dizi Metodları

```liva
let nums = [1, 2, 3, 4, 5]

nums.forEach(|x| { println(x) })
let doubled = nums.map(|x: i32| -> i32 { return x * 2 })
let evens = nums.filter(|x: i32| -> bool { return x % 2 == 0 })
let sum = nums.reduce(0, |acc: i32, x: i32| -> i32 { return acc + x })
```

### Map'ler (Hash Map'ler)

```liva
var m: Map<string, i32>

m.insert("alice", 30)
m.insert("bob", 25)

let age = m.get("alice")           // i32? — optional
let exists = m.contains("charlie") // false
m.remove("bob")

let count = m.size                 // 1
let empty = m.isEmpty              // false

// İterasyon
for (key, value) in m {
    println("\(key): \(value)")
}
```

### Set'ler (Hash Set'ler)

```liva
var s: Set<i32>

s.insert(10)
s.insert(20)
s.insert(30)

let has = s.contains(20)   // true
s.remove(10)

let count = s.size          // 2
let empty = s.isEmpty       // false

// İterasyon
for item in s {
    println(item)
}
```

---

## 16. Tuple'lar

### Oluşturma

```liva
let pair = (42, "hello")
let triple = (1, 2.0, true)
```

### Erişim

```liva
let x = pair.0        // 42
let y = pair.1        // "hello"
```

### Destructuring

```liva
let (a, b) = pair      // a = 42, b = "hello"
```

### Çoklu Dönüş Değerli Fonksiyonlar

```liva
func divmod(a: i32, b: i32) -> (i32, i32) {
    return (a / b, a % b)
}

let (quotient, remainder) = divmod(17, 5)
// quotient = 3, remainder = 2
```

### Tuple Destructuring ile For-in

```liva
var m: Map<string, i32>
for (key, value) in m {
    println(key)
}
```

---

## 17. Type Alias'lar

```liva
type Int = i32
type Str = string
type Point2D = Point
type IntArray = [i32]

let x: Int = 42
let s: Str = "hello"
```

Type alias'lar mevcut bir tip için yeni bir isim oluşturur. Alias, orijinal tiple birbirinin yerine kullanılabilir.

---

## 18. String İşlemleri

### Yerleşik Metodlar

```liva
let s = "Hello World"

s.length                    // i64: string uzunluğu
s.contains("World")         // bool: alt string kontrolü
s.startsWith("Hello")       // bool: ön ek kontrolü
s.endsWith("World")         // bool: son ek kontrolü
s.indexOf("World")          // i64: ilk bulunma indeksi (bulunamazsa -1)
s.substring(0, 5)           // string: "Hello"
s.trim()                    // string: baştaki/sondaki boşlukları kaldır
s.toUpper()                 // string: "HELLO WORLD"
s.toLower()                 // string: "hello world"
s.replace("World", "Liva")  // string: "Hello Liva"
s.split(" ")                // [string]: ["Hello", "World"]
```

### String İndeksleme

```liva
let ch = s[0]              // ilk karakter
let sub = s[0..5]          // aralıkla alt string: "Hello"
```

### String Interpolation

```liva
let name = "Liva"
let version = 1
let msg = "Welcome to \(name) v\(version)!"
// "Welcome to Liva v1!"
```

`\(...)` içine herhangi bir ifade yerleştirilebilir.

### Çok Argümanlı Print

```liva
println("x =", x, "y =", y)
```

### Format Fonksiyonu

```liva
let msg = format("x = {}, y = {}", 10, 20)
// "x = 10, y = 20"
```

---

## 19. Modüller ve Import'lar

### Import Sözdizimi

```liva
import std::math          // math modülünü import et
import std::io            // I/O fonksiyonlarını import et
import std::convert       // dönüşüm fonksiyonlarını import et
```

### Mevcut Standart Modüller

| Modül | Sağladıkları |
|-------|-------------|
| `std::math` | `abs`, `min`, `max`, `sqrt`, `pow`, `floor`, `ceil`, `round`, `log`, `log10`, `sin`, `cos`, `tan` |
| `std::io` | `readLine`, `readFile`, `writeFile`, `appendFile`, `fileExists` |
| `std::convert` | `parseInt`, `parseInt64`, `parseFloat`, `toString` |
| `std::os` | `env`, `exit`, `args`, `exec`, `cwd`, `sleep` |
| `std::random` | `randInt`, `randFloat`, `random`, `randomChoice` |
| `std::regex` | `regexMatch`, `regexFind`, `regexFindAll`, `regexReplace` |
| `std::net` | `httpGet`, `httpPost`, `httpPut`, `httpDelete` |
| `std::json` | `jsonParse`, `jsonStringify` |
| `std::time` | `now`, `clock`, `clockMs`, `sleep` |
| `std::channel` | `channelCreate`, `channelSend`, `channelRecv`, `channelClose` |
| `std::task` | `taskGroupCreate`, `taskGroupSpawn`, `taskGroupAwaitAll`, `taskGroupCancelAll` |
| `std::crypto` | `sha256`, `md5`, `hmacSha256` |
| `std::async` | `taskSelect`, `withTimeout`, `schedulerInit`, `asyncFileRead`, `asyncFileWrite` |
| `std::path` | `pathJoin`, `pathExtension`, `pathBasename`, `pathDirname` |
| `std::testing` | `assertEqual`, `assertNotEqual`, `assertTrue`, `assertFalse` |
| `std::collections` | List, Map, Set yardımcı fonksiyonları |
| `std::strings` | String manipülasyon yardımcıları |
| `std::ui` | raylib tabanlı UI framework (widget'lar, layout, tema, animasyon) |
| `std` | Yukarıdakilerin tümü |

### Kullanıcı Modülleri

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

### Public Görünürlük

Varsayılan olarak bildirimler modül-özeldir. Dışa aktarmak için `pub` kullanın:

```liva
pub func publicFunction() {}
pub struct PublicStruct { var x: i32 }
```

---

## 20. Async/Await

### Async Fonksiyonlar

```liva
async func fetchUser(id: i32) -> string {
    let response = await httpGet(format("/users/{}", id))
    return response
}
```

### Await İfadesi

```liva
async func main() {
    let user = await fetchUser(42)
    println(user)
}
```

`await` anahtar kelimesi yalnızca `async` fonksiyonlar içinde kullanılabilir. Asenkron işlem tamamlanana kadar yürütmeyi askıya alır.

### For Await

Akışlar üzerinde asenkron iterasyon:

```liva
async func processStream() {
    for await item in asyncStream {
        println(item)
    }
}
```

`for await` sadece `async` fonksiyonların içinde kullanılabilir.

### Async Runtime Özellikleri

```liva
import std::async

// İlk tamamlanan görevi seç
let result = taskSelect([task1, task2, task3])

// Zaman aşımıyla çalıştır
let result = withTimeout(myTask, 5000)  // 5 saniye zaman aşımı

// İş parçacığı havuzu zamanlayıcısı
schedulerInit(4)    // 4 işçi iş parçacığı
// ... görevleri başlat ...
schedulerShutdown()
```

### Kısıtlamalar

- `main()` `async` olarak bildirilemez (zamanlayıcı senkron çalışır)
- Async fonksiyonlar LLVM coroutine'ler kullanılarak uygulanır

---

## 21. Operator Overloading

Operator overloading, belirli protocol'ler uygulanarak gerçekleştirilir:

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

// Artık Vec2 ile + kullanabilirsiniz:
let a = Vec2 { x: 1.0, y: 2.0 }
let b = Vec2 { x: 3.0, y: 4.0 }
let c = a + b   // Vec2 { x: 4.0, y: 6.0 }
```

### Desteklenen Operator Protocol'leri

| Protocol | Operatör | Metod |
|----------|----------|-------|
| `Addable` | `+` | `add(ref self, other: ref Self) -> Self` |
| `Subtractable` | `-` | `sub(ref self, other: ref Self) -> Self` |
| `Multipliable` | `*` | `mul(ref self, other: ref Self) -> Self` |
| `Dividable` | `/` | `div(ref self, other: ref Self) -> Self` |
| `Equatable` | `==`, `!=` | `eq(ref self, other: ref Self) -> bool` |
| `Comparable` | `<`, `<=`, `>`, `>=` | `lt(ref self, other: ref Self) -> bool` |

---

## 22. Özel Iterator'lar

Bir tipi `for-in` döngülerinde yinelenebilir yapmak için `Iterator` protocol'ünü uygulayın:

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

// Kullanım:
var counter = Counter { current: 0, max: 5 }
for value in counter {
    println(value)    // 0, 1, 2, 3, 4 yazdırır
}
```

---

## 23. Drop Trait (Destructor'lar)

`Drop` protocol'ü, bir değer kapsam dışına çıktığında çalışan bir destructor sağlar:

```liva
protocol Drop {
    func drop(mut self)
}

struct FileHandle {
    var fd: i32
}

impl FileHandle: Drop {
    func drop(mut self) {
        // temizlik: dosya tanımlayıcısını kapat
        println("Dosya kapatılıyor")
    }
}

func main() {
    let f = FileHandle { fd: 3 }
    // ... f kullan ...
}   // f.drop() burada otomatik çağrılır
```

### Kurallar

- `drop` `mut self` alır (ownership'i tüketir)
- Derleyici kapsam çıkışında `drop()` çağrılarını yerleştirir
- Drop, ters bildirim sırasında çağrılır

---

## 24. Standart Kütüphane

### Math Fonksiyonları

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

### I/O Fonksiyonları

```liva
import std::io

print("satır sonu yok")
println("satır sonu ile")
let line = readLine()           // stdin'den oku
let msg = format("x={}", 42)   // string biçimlendirme

// Dosya I/O
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

### Tip Dönüşümü

```liva
import std::convert

let s = toString(42)              // "42"
let n = parseInt("123")           // 123 (i32)
let big = parseInt64("999999")    // 999999 (i64)
let f = parseFloat("3.14")       // 3.14 (f64)
```

### OS Fonksiyonları

```liva
import std::os

let home = env("HOME")            // ortam değişkeni
let arguments = args()             // komut satırı argümanları [string]
let now = clock()                  // epoch'tan bu yana saniye (f64)
let ms = clockMs()                 // epoch'tan bu yana milisaniye (i64)
sleep(1000)                        // 1000ms uyu
exit(0)                            // işlemden çık
```

### Random

```liva
import std::random

let n = randInt(1, 100)     // [1, 100] arasında rastgele tamsayı
let f = randFloat()          // [0.0, 1.0) arasında rastgele float
```

### Düzenli İfadeler

```liva
import std::regex

let matches = regexMatch("hello123", "[a-z]+[0-9]+")   // true
let found = regexFind("age: 25", "[0-9]+")              // "25"
let all = regexFindAll("a1 b2 c3", "[a-z][0-9]")       // ["a1", "b2", "c3"]
let replaced = regexReplace("foo bar", "bar", "baz")    // "foo baz"
```

### Ağ

```liva
import std::net

let body = httpGet("https://example.com")
let response = httpPost("https://api.example.com/data", "{\"key\":\"value\"}")
```

Windows'ta ağ işlemleri WinHTTP kullanır. Linux/macOS'ta libcurl kullanır (mevcutsa).

---

## 25. Yerleşik Fonksiyonlar

Bu fonksiyonlar herhangi bir `import` olmadan global olarak kullanılabilir:

| Fonksiyon | İmza | Açıklama |
|-----------|------|----------|
| `println` | `(any...)` | Değerleri satır sonu ile yazdır |
| `print` | `(any...)` | Değerleri satır sonu olmadan yazdır |
| `len` | `([T]) -> i64` | Dizi veya string uzunluğu |
| `toString` | `(any) -> string` | String'e dönüştür |
| `abs` | `(numeric) -> numeric` | Mutlak değer |
| `min` | `(T, T) -> T` | İki değerin minimumu |
| `max` | `(T, T) -> T` | İki değerin maksimumu |
| `sqrt` | `(f64) -> f64` | Karekök |
| `pow` | `(f64, f64) -> f64` | Üs alma |
| `readLine` | `() -> string` | stdin'den satır oku |
| `format` | `(string, any...) -> string` | String biçimlendirme |
| `parseInt` | `(string) -> i32` | Tamsayı ayrıştır |
| `parseFloat` | `(string) -> f64` | Float ayrıştır |
| `randInt` | `(i32, i32) -> i32` | Rastgele tamsayı |
| `randFloat` | `() -> f64` | Rastgele float [0, 1) |
| `benchStart` | `(string) -> void` | Benchmark zamanlayıcısını başlat |
| `benchIter` | `(string) -> void` | Benchmark iterasyonu kaydet |
| `benchDone` | `(string) -> void` | Benchmark'ı sonlandır |
| `benchReport` | `() -> void` | Benchmark sonuçlarını yazdır |
| `benchReset` | `() -> void` | Tüm benchmark'ları sıfırla |
| `channelCreate` | `(i32) -> Channel` | Tamponlu kanal oluştur |
| `channelSend` | `(Channel, any) -> void` | Kanala değer gönder |
| `channelRecv` | `(Channel) -> any` | Kanaldan değer al |
| `channelClose` | `(Channel) -> void` | Kanalı kapat |
| `taskGroupCreate` | `() -> TaskGroup` | Görev grubu oluştur |
| `taskGroupSpawn` | `(TaskGroup, () -> void) -> void` | Grupta görev başlat |
| `taskGroupAwaitAll` | `(TaskGroup) -> void` | Tüm görevleri bekle |
| `taskGroupCancelAll` | `(TaskGroup) -> void` | Tüm görevleri iptal et |
| `assert` | `(bool, string?) -> void` | Koşulu doğrula veya panik |

---

## 26. Proje Konfigürasyonu

### liva.toml

Proje kök dizininde bir `liva.toml` dosyası oluşturun:

```toml
[project]
name = "myapp"
version = "1.0.0"
entry = "src/main.liva"

[build]
optimization = "release"    # "debug" veya "release"

[dependencies]
json_parser = "^1.0.0"     # SemVer kısıtlaması
utils = "~2.3"             # 2.3.x ile uyumlu
```

### Versiyon Kısıtlamaları

| Sözdizimi | Anlam |
|-----------|-------|
| `"1.2.3"` | Tam versiyon |
| `"^1.2.3"` | Uyumlu (>=1.2.3, <2.0.0) |
| `"~1.2.3"` | Yama seviyesi (>=1.2.3, <1.3.0) |
| `">=1.0"` | Minimum versiyon |

### Lock Dosyası

`liva.lock` otomatik oluşturulur ve tam bağımlılık versiyonlarını sabitler. Tekrarlanabilir build'ler için bu dosyayı versiyon kontrolüne kaydedin.

### Proje Komutları

```bash
livac init myproject        # yeni proje oluştur
livac build                 # build (debug)
livac build --release       # build (optimize)
livac run                   # build + çalıştır
```

---

## 27. Derleyici Seçenekleri

```
Kullanım: livac [seçenekler] <dosya>

Derleme:
  -o <çıktı>            Çıktı dosya yolu
  -O0, -O1, -O2, -O3    Optimizasyon seviyesi
  -g                     Debug bilgisi üret
  --debug                Debug build (O0 + debug bilgisi)
  --release              Release build (O2, debug bilgisi yok)

Tanılama:
  --dump-tokens <dosya>    Token akışını yazdır
  --dump-ast <dosya>       AST ağacını yazdır
  --check-only <dosya>     Kod üretimi olmadan tip kontrolü
  --emit-ir <dosya>        LLVM IR çıktısı
  --emit-obj <dosya>       Obje dosyası çıktısı (.o)
  --emit-asm <dosya>       Assembly çıktısı (.s)
  --dump-timings           Faz bazlı derleme zamanlamasını göster
  --trace-macros           Macro genişletmelerini izle

Proje:
  init [isim]              Yeni Liva projesi oluştur
  build [--release]        liva.toml'dan build et
  run [--release]          Build et ve çalıştır
  remove [paket]           Bağımlılık kaldır

Test & Benchmark:
  test                     Test bloklarını çalıştır (test "ad" { ... })
  bench                    Benchmark bloklarını çalıştır

Çapraz Derleme:
  --target <triple>        Hedef platform için derle (ör. x86_64-linux-gnu, wasm32)

Ayrı Derleme:
  --emit-obj               Linkleme yapmadan obje dosyasına derle
  link <dosyalar> -o <çıktı>  Obje dosyalarını çalıştırılabilire linkle

Araçlar:
  lsp                      Language Server Protocol sunucusunu başlat
  dap                      Debug Adapter Protocol sunucusunu başlat
  repl                     İnteraktif REPL başlat
  format [dosya]           Kaynak kodu biçimlendir
  lint [dosya]             Linter kontrolleri çalıştır
```

### LSP Sunucusu

`livac lsp` komutu stdio üzerinden bir Language Server Protocol sunucusu başlatır ve şunları sağlar:

- **Tanılama** — Gerçek zamanlı hata ve uyarı raporlama
- **Completion** — Anahtar kelimeler, yerleşik fonksiyonlar ve doküman sembolleri
- **Hover** — Fonksiyonlar, değişkenler, struct'lar, enum'lar için tip bilgisi
- **Go to Definition** — Bildirime atla
- **Document Symbols** — Fonksiyonlar, tipler, değişkenlerin anahat görünümü
- **References** — Bir sembolün tüm oluşumlarını bul
- **Rename** — Bir sembolü doküman genelinde yeniden adlandır
- **Signature Help** — Fonksiyon çağrısında parametre ipuçları
- **Semantik Token'lar** — Derleyici doğruluğunda sözdizimi vurgulama
- **Biçimlendirme** — Otomatik kod biçimlendirme
- **Katlama Aralıkları** — Kod bloklarını katla/aç
- **Belge Vurgulama** — Sembol oluşumlarını vurgula
- **Kod Aksiyonları** — Hızlı düzeltme önerileri (ör. kullanılmayan değişken ön eki)
- **Kod Merceği** — Satır içi referans sayıları
- **Çağrı Hiyerarşisi** — Gelen ve giden çağrı navigasyonu
- **Satır İçi İpuçları** — Satır içi tip açıklamaları
- **Seçim Aralığı** — AST tabanlı akıllı seçim genişletme
- **Çalışma Alanı Sembolü** — Proje genelinde sembol arama

### REPL

`livac repl` komutu interaktif bir oturum başlatır:

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
  :help, :h           Bu yardımı göster
  :quit, :q           REPL'den çık
  :reset, :r          Tüm bildirimleri temizle
  :declarations, :decls  Birikmiş bildirimleri göster
>>> :quit
Goodbye!
```

Özellikler:
- İfade değerlendirme (otomatik `println` sarmalama)
- Bildirim birikimi (fonksiyonlar, struct'lar, enum'lar girişler arasında kalıcı)
- Statement çalıştırma (`if`, `while`, `for`)
- Çok satırlı giriş (kapatılmamış süslü/normal parantezler sonraki satırda devam eder)
- Import desteği

---

## 28. Sınıflar (Classes)

Sınıflar, kalıtım, sanal dispatch ve otomatik bellek yönetimi desteği sunan referans tipleridir.

### Bildirim

```liva
class Animal {
    var name: string
    var age: i32

    init(name: string, age: i32) {
        self.name = name
        self.age = age
    }

    deinit {
        println("Animal serbest bırakıldı")
    }

    func speak(ref self) -> string {
        return self.name + " ses çıkarır"
    }
}
```

### Kalıtım

```liva
class Dog : Animal {
    var breed: string

    init(name: string, age: i32, breed: string) {
        super.init(name, age)
        self.breed = breed
    }

    override func speak(ref self) -> string {
        return self.name + " havlar"
    }
}
```

### Erişim Kontrolü

```liva
class Account {
    private var balance: f64

    init(initial: f64) {
        self.balance = initial
    }

    func getBalance(ref self) -> f64 {
        return self.balance
    }

    func deposit(ref mut self, amount: f64) {
        self.balance = self.balance + amount
    }
}
```

### Protocol Uyumluluğu

Sınıflar doğrudan protocol'lere uyum sağlayabilir:

```liva
protocol Printable {
    func toString(ref self) -> string
}

class User : Printable {
    var name: string

    init(name: string) {
        self.name = name
    }

    func toString(ref self) -> string {
        return "User: " + self.name
    }
}
```

### Temel Farklar: Class vs Struct

| Özellik | `struct` | `class` |
|---------|----------|---------|
| Semantik | Değer tipi (kopyalama) | Referans tipi (paylaşımlı) |
| Kalıtım | Hayır | Evet |
| `init`/`deinit` | Hayır | Evet |
| `override` | Hayır | Evet |
| `private` alanlar | Hayır | Evet |
| `super` | Hayır | Evet |
| Sanal dispatch | Hayır | Evet (vtable) |

### Örtük Self

Sınıf metodları içinde alanlara `self.` ön eki olmadan doğrudan erişilebilir:

```liva
class Point {
    var x: f64
    var y: f64

    init(x: f64, y: f64) {
        self.x = x
        self.y = y
    }

    func magnitude(ref self) -> f64 {
        return sqrt(x * x + y * y)   // örtük self
    }
}
```

---

## 29. FFI (Yabancı Fonksiyon Arayüzü)

Liva, `extern "C"` bildirimleri aracılığıyla C fonksiyonlarını çağırmayı destekler.

### Tekli Bildirim

```liva
extern "C" func puts(s: string) -> i32
```

### Blok Bildirim

```liva
extern "C" {
    func malloc(size: u64) -> u64
    func free(ptr: u64)
    func printf(fmt: string, ...) -> i32
}
```

### C Varargs

Extern fonksiyonlar `...` ile C tarzı değişken argümanlar kullanabilir:

```liva
extern "C" func printf(fmt: string, ...) -> i32

func main() {
    printf("Merhaba %s, yaşınız %d\n", "Dünya", 42)
}
```

### Kurallar

- Extern bildirimlerde yalnızca C uyumlu tipler kullanılabilir
- `...` (C varargs) yalnızca `extern "C"` fonksiyonlarında geçerlidir
- Extern fonksiyonların gövdesi yoktur — derleme zamanında bağlanır
- Harici kütüphaneler için `liva.toml`'da `-l` bağlayıcı bayrakları kullanın

---

## 30. Derleme Zamanı Değerlendirme

`comptime` anahtar kelimesi, derleme zamanında değerlendirilen blokları işaretler.

### Comptime Blokları

```liva
let size = comptime {
    let base = 16
    let multiplier = 4
    base * multiplier
}
// size 64'tür, derleme zamanında hesaplanır
```

### Fonksiyonlarda Comptime

```liva
func main() {
    let table = comptime {
        var result: [i32] = []
        var i = 0
        while i < 10 {
            result.push(i * i)
            i = i + 1
        }
        result
    }
    // table [0, 1, 4, 9, 16, 25, 36, 49, 64, 81] olur
    println(table)
}
```

### Sınırlamalar

- Yalnızca saf ifadeler ve deyimler (G/Ç yok, FFI yok)
- Desteklenen: aritmetik, değişkenler, döngüler, koşullar, dizi/string işlemleri
- Sonuç derleme zamanı sabit tipi olmalıdır

---

## 31. Macro'lar

Macro'lar derleme zamanında kalıp tabanlı kod üretimi sağlar.

### Macro Tanımlama

```liva
macro swap {
    ($a, $b) => {
        let temp = $a
        $a = $b
        $b = temp
    }
}
```

### Macro Çağrısı

```liva
func main() {
    var x = 1
    var y = 2
    swap!(x, y)
    println(x)  // 2
    println(y)  // 1
}
```

### Çoklu Kollar

```liva
macro vec {
    () => { [] }
    ($($x),*) => {
        let arr = []
        $( arr.push($x) )*
        arr
    }
}
```

### Kurallar

- Macro'lar tip kontrolünden önce genişletilir
- Macro isimleri çağrı noktasında ayrıştırma için `!` kullanır
- Kalıp değişkenleri `$` ile başlar
- Tekrarlama `$(...)*` sözdizimi kullanır

---

## 32. Test Framework

Liva'nın `test` blokları ile dahili test desteği vardır.

### Test Blokları

```liva
test "toplama çalışıyor" {
    let result = 2 + 2
    assert(result == 4)
}

test "string birleştirme" {
    let s = "hello" + " " + "world"
    assert(s == "hello world")
}
```

### Testleri Çalıştırma

```bash
livac test                    # tüm testleri çalıştır
livac test --filter "toplama"   # eşleşen testleri çalıştır
```

### Test İzolasyonu

Her test bloğu `setjmp`/`longjmp` kullanarak izole çalışır. Bir testteki başarısız doğrulama diğer testleri etkilemez.

### Assert

```liva
test "doğrulamalar" {
    assert(true)                          // geçer
    assert(1 + 1 == 2)                   // geçer
    assert(false, "özel hata mesajı")     // özel mesajla başarısız
}
```

---

## 33. Eşzamanlılık

Liva, yapısal eşzamanlılık için kanallar ve görev grupları sağlar.

### Kanallar

Kanallar eşzamanlı görevler arasında iletişim sağlar:

```liva
import std::channel

func main() {
    let ch = channelCreate(10)  // tamponlu kanal, kapasite 10

    channelSend(ch, 42)
    channelSend(ch, 100)

    let val1 = channelRecv(ch)  // 42
    let val2 = channelRecv(ch)  // 100

    channelClose(ch)
}
```

### Görev Grupları

Görev grupları spawn/await ile yapısal eşzamanlılık sağlar:

```liva
import std::task

func main() {
    let group = taskGroupCreate()

    taskGroupSpawn(group, || {
        println("Görev 1")
    })

    taskGroupSpawn(group, || {
        println("Görev 2")
    })

    taskGroupAwaitAll(group)  // tüm görevleri bekle
    println("Hepsi tamamlandı")
}
```

### İptal

```liva
taskGroupCancelAll(group)  // çalışan tüm görevleri iptal et
```

### Kanal Özellikleri

- Yapılandırılabilir kapasiteli tamponlu (dahili olarak ring buffer)
- Spin-wait senkronizasyonu ile iş parçacığı güvenli
- Kanalı kapatmak daha fazla gönderimi engeller

---

## 34. dyn Protocol (Trait Nesneleri)

`dyn` anahtar kelimesi dinamik dispatch için trait nesneleri oluşturur.

### Bildirim

```liva
protocol Drawable {
    func draw(ref self)
}

func render(shape: dyn Drawable) {
    shape.draw()  // sanal dispatch
}
```

### Kullanım

```liva
struct Circle {
    var radius: f64
}

impl Circle: Drawable {
    func draw(ref self) {
        println("Yarıçapı \(self.radius) olan daire çiziliyor")
    }
}

struct Square {
    var side: f64
}

impl Square: Drawable {
    func draw(ref self) {
        println("Kenarı \(self.side) olan kare çiziliyor")
    }
}

func main() {
    let shapes: [dyn Drawable] = [Circle { radius: 5.0 }, Square { side: 3.0 }]
    for shape in shapes {
        render(shape)
    }
}
```

### Nesne Güvenliği

Bir protocol `dyn` ile kullanılabilmesi (nesne güvenli) için:
- Hiçbir metod dönüş tipi olarak `Self` kullanmamalı
- Hiçbir metodun generik tip parametresi olmamalı
- Tüm metodlar `ref self` veya `ref mut self` almalı

---

## 35. Guard Koşulları

`guard` deyimi, koşullar sağlanmadığında erken çıkış sağlar.

### Temel Guard

```liva
func process(value: i32) {
    guard value > 0 else {
        println("pozitif olmalı")
        return
    }
    // value burada > 0 garanti edilir
    println(value)
}
```

### Optional Bağlama ile Guard

```liva
func greet(name: string?) {
    guard let n = name else {
        println("İsim sağlanmadı")
        return
    }
    println("Merhaba, \(n)!")
}
```

### Kurallar

- `else` bloğu zorunludur
- `else` bloğu kapsayıcı kapsamdan çıkmalıdır (`return`, `break`, `continue`)
- `guard let` ile bağlanan değişkenler guard deyiminden sonra kullanılabilir
- Ön koşul kontrolü için iç içe `if` yerine guard tercih edilir

---

## Ek: Gramer Özeti

```
program       = declaration*
declaration   = funcDecl | varDecl | structDecl | enumDecl
              | implDecl | protocolDecl | importDecl | typeAlias
              | classDecl | externDecl | testDecl | macroDecl

funcDecl      = ["pub"] ["async"] "func" IDENT ["<" typeParams ">"]
                "(" params ")" ["->" type] block
varDecl       = ("let" | "var" | "const") IDENT [":" type] ["=" expr]
structDecl    = ["pub"] "struct" IDENT ["<" typeParams ">"] "{" fieldDecl* "}"
enumDecl      = ["pub"] "enum" IDENT "{" caseDecl* "}"
implDecl      = "impl" ["<" typeParams ">"] IDENT [":" IDENT] "{" funcDecl* "}"
protocolDecl  = ["pub"] "protocol" IDENT "{" funcDecl* "}"
importDecl    = "import" IDENT ("::" IDENT)*
typeAlias     = ["pub"] "type" IDENT "=" type

classDecl     = "class" IDENT [":" IDENT ("," IDENT)*] "{" classBody "}"
classBody     = (initDecl | deinitDecl | funcDecl | varDecl)*
initDecl      = "init" "(" params ")" block
deinitDecl    = "deinit" block
externDecl    = "extern" STRING_LIT ("{" externFunc* "}" | externFunc)
externFunc    = "func" IDENT "(" params ["," "..."] ")" ["->" type]
testDecl      = "test" STRING_LIT block
macroDecl     = "macro" IDENT "{" macroArm* "}"
macroArm      = "(" pattern ")" "=>" block
comptimeExpr  = "comptime" block
dynType       = "dyn" IDENT
guardStmt     = "guard" expr "else" block
              | "guard" "let" IDENT "=" expr "else" block

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
              | "comptime" block

type          = "i8" | "i16" | "i32" | "i64" | "u8" | "u16" | "u32" | "u64"
              | "f32" | "f64" | "bool" | "string" | "void"
              | IDENT ["<" type ("," type)* ">"]
              | "[" type "]"
              | "(" type ("," type)* ")" "->" type
              | "(" type ("," type)* ")"
              | "ref" ["mut"] type
              | "ref" LIFETIME ["mut"] type
              | type "?"
              | "dyn" IDENT
```

## 23. Const Generics

Derleme zamani sabit parametreler:

```liva
func repeat<const N: i32>(value: i32) -> i32 {
    return N * value
}

func withDefault<T, const SIZE: i32 = 10>(value: T) {
    println(SIZE)
}

// Sabit boyutlu dizi: [T; N]
func fill<T, const N: i32>(value: T, data: [T; N]) {}
```

- Sozdizimi: `const AD: TIP` veya `const AD: TIP = VARSAYILAN`
- Desteklenen tipler: i32, i64, bool
- Monomorphize edilir: `repeat<5>` icin benzersiz kod uretilir

## 24. Explicit Lifetime Sozdizimi

Rust tarzi yasam suresi anotasyonlari:

```liva
func first<'a>(items: ref 'a [i32]) -> ref 'a i32 {
    return ref items[0]
}

func merge<'a, 'b>(x: ref 'a i32, y: ref 'b i32) {}

// 'static yasam suresi
let global: ref 'static i32 = ref CONSTANT
```

### Lifetime Elision Kurallari

Belirsizlik olmayan durumlarda derleyici yasam surelerini otomatik cikarir:

1. Her girdi ref kendi yasam suresini alir
2. Tek girdi ref → cikis ayni yasam suresini alir
3. &self metotlarda → cikis self'in yasam suresini alir

## 25. Generator ve Yield

Generator fonksiyonlar `yield` ile deger uretir:

```liva
func fibonacci() {
    var a = 0
    var b = 1
    while true {
        yield a
        let tmp = a
        a = b
        b = tmp + b
    }
}
```

- `yield` iceren fonksiyonlar otomatik generator olarak algilanir
- LLVM coroutine altyapisi kullanilir (async/await ile ayni)

## 26. Generic Associated Types (GATs)

Protokollerdeki associated type'lar kendi generic parametrelerine sahip olabilir:

```liva
protocol LendingIterator {
    type Item<'a>
    func next(mut self) -> i32
}

protocol Container {
    type Element<T>
}
```

## 27. Enum Discriminant Degerleri

Enum case'leri acik tamsayi degerlerine sahip olabilir:

```liva
enum HttpStatus {
    case OK = 200
    case NotFound = 404
    case InternalError = 500
}

enum Signal {
    case None
    case SIGINT = 2
    case SIGKILL = -9    // negatif degerler desteklenir
}
```
