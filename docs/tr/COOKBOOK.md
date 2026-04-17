# Liva Yemek Kitabı

Liva programlama dili için yaygın kalıplar, tarifler ve deyimler. Her tarif bağımsızdır ve pratik bir tekniği gösterir.

---

## İçindekiler

1. [`?` Operatörü ile Hata Yönetimi](#1--operatörü-ile-hata-yönetimi)
2. [Builder Kalıbı](#2-builder-kalıbı)
3. [Protocol'lerle Gözlemci Kalıbı](#3-protocollerle-gözlemci-kalıbı)
4. [Özel Iterator](#4-özel-iterator)
5. [Drop Trait ile RAII](#5-drop-trait-ile-raii)
6. [Generik Konteyner](#6-generik-konteyner)
7. [Asenkron HTTP İstekleri](#7-asenkron-http-i̇stekleri)
8. [Kanal Tabanlı Üretici/Tüketici](#8-kanal-tabanlı-üreticitüketici)
9. [C Kütüphaneleri ile FFI](#9-c-kütüphaneleri-ile-ffi)
10. [WASM'a Çapraz Derleme](#10-wasma-çapraz-derleme)
11. [Sınıflar ve Kalıtım](#11-sınıflar-ve-kalıtım)
12. [For Await ve Channel'larla Async](#12-for-await-ve-channellarla-async)
13. [Crypto: Hash ve HMAC](#13-crypto-hash-ve-hmac)
14. [Ayrı Derleme](#14-ayrı-derleme)

---

## 1. `?` Operatörü ile Hata Yönetimi

`?` operatörü bir `Result` değerini açar veya hatayı çağırana yayar.

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

**Önemli noktalar:**
- Bir `Result<T, E>` ifadesinin ardından gelen `?` operatörü `Ok(T)` değerini açar veya `Err(E)` ile erken dönüş yapar
- Çağıran fonksiyon da uyumlu bir hata tipiyle `Result` döndürmelidir
- Doğrulama zincirleri için `guard` ile birleştirin

---

## 2. Builder Kalıbı

Karmaşık nesneleri adım adım oluşturmak için metod zincirleme kullanın.

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

## 3. Protocol'lerle Gözlemci Kalıbı

Bildirim arayüzü tanımlamak için protocol'leri kullanın.

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

## 4. Özel Iterator

Yinelenebilir tipler oluşturmak için `Iterator` protocol'ünü uygulayın.

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

## 5. Drop Trait ile RAII

Otomatik kaynak temizliği için `Drop` protocol'ünü kullanın.

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

**Önemli noktalar:**
- `Drop.drop(mut self)` kapsam çıkışında otomatik olarak çağrılır
- Drop, tanımlama sırasının tersinde çağrılır
- Dosya kapatma, ağ bağlantıları, kilit serbest bırakma gibi işlemler için kullanın

---

## 6. Generik Konteyner

Generik'leri kullanarak tip güvenli konteynerler oluşturun.

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

## 7. Asenkron HTTP İstekleri

async/await kullanarak API'lerden veri çekin.

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

**Önemli noktalar:**
- `await` kullanmak için fonksiyonları `async` ile işaretleyin
- `await` işlem tamamlanana kadar askıya alır
- Asenkron fonksiyonlar arka planda LLVM coroutine'lerini kullanır

---

## 8. Kanal Tabanlı Üretici/Tüketici

Görevler arasında iletişim için kanalları kullanın.

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

**Önemli noktalar:**
- Kanallar tipli ve tamponludur
- `channelClose` artık değer gönderilmeyeceğini bildirir
- Tüm görevleri beklemek için `taskGroupAwaitAll` kullanın

---

## 9. C Kütüphaneleri ile FFI

`extern "C"` kullanarak Liva'dan C fonksiyonlarını çağırın.

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

**Önemli noktalar:**
- `extern "C"` fonksiyonları C bağlama kurallarıyla tanımlar
- C değişken argümanları için `...` kullanın (yalnızca extern fonksiyonlarda)
- Harici kütüphaneler için `liva.toml` dosyasına `-l` bayrakları ekleyin:

```toml
[build]
extra_flags = ["-lm", "-lpthread"]
```

---

## 10. WASM'a Çapraz Derleme

Liva programlarını WebAssembly'ye derleyin.

### Kaynak Kodu

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

### Derleme

```bash
# Compile to WASM
livac build --target wasm32 -o hello.wasm hello_wasm.liva

# Or set target in liva.toml:
# [build]
# target = "wasm32"
```

### Diğer Çapraz Derleme Hedefleri

```bash
# Linux x86_64
livac build --target x86_64-linux-gnu

# Linux ARM64
livac build --target aarch64-linux-gnu

# macOS ARM64
livac build --target aarch64-apple-darwin
```

**Önemli noktalar:**
- Çapraz derleme için `--target <triple>` kullanın
- WASM çıktısı çalışma zamanı kütüphanesi bağlamasını atlar
- Çapraz derleme için LLVM'in AllTargets bileşeni başlatılmalıdır

---

## 11. Sınıflar ve Kalıtım

Kalıtım ve sanal dispatch ile referans tipli nesneler için sınıfları kullanın.

```liva
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

func main() {
    let c = Circle(5.0, "kırmızı")
    println("Alan: \(c.area()), Renk: \(c.color)")
}
```

**Önemli noktalar:**
- Sınıflar heap allocation ve referans semantiği kullanır
- Üst sınıf metodlarını geçersiz kılarken `override` zorunludur
- `super.init(...)` üst sınıf yapıcısını çağırır
- `deinit` nesne kapsam dışına çıktığında otomatik çalışır

---

## 12. For Await ve Channel'larla Async

Veri akışı için channel ve async iterasyonu birleştirin.

```liva
import std::channel
import std::task

func main() {
    let ch = channelCreate(100)
    let group = taskGroupCreate()

    // Üretici
    taskGroupSpawn(group, || {
        for i in 0..10 {
            channelSend(ch, i * i)
        }
        channelClose(ch)
    })

    // Tüketici
    taskGroupSpawn(group, || {
        var total = 0
        for i in 0..10 {
            let val = channelRecv(ch)
            total = total + val
        }
        println("Toplam: \(total)")
    })

    taskGroupAwaitAll(group)
}
```

---

## 13. Crypto: Hash ve HMAC

Hashleme ve mesaj doğrulama için crypto modülünü kullanın.

```liva
import std::crypto

func main() {
    let hash = sha256("merhaba dünya")
    println("SHA-256: \(hash)")

    let md5hash = md5("merhaba dünya")
    println("MD5: \(md5hash)")

    let mac = hmacSha256("gizli-anahtar", "önemli mesaj")
    println("HMAC: \(mac)")
}
```

---

## 14. Ayrı Derleme

Dosyaları tek tek obje dosyalarına derleyip sonra linkleyin.

```bash
# Her dosyayı .o'ya derle
livac --emit-obj modul_a.liva
livac --emit-obj modul_b.liva

# Tüm obje dosyalarını linkle
livac link modul_a.o modul_b.o -o uygulama
```

**Önemli noktalar:**
- `--emit-obj` linkleme yapmadan `.o` obje dosyası üretir
- `livac link` birden fazla obje dosyasını final çalıştırılabilire linkler
- `livac build` komutu bunu otomatik olarak önbellekleme ile yapar

---

## 14. Const Generics

```liva
// Derleme zamani boyut parametreleri
func repeat<const N: i32>(value: i32) -> i32 {
    return N * value
}

let result = repeat<5>(3)   // N = 5, sonuc = 15
```

## 15. Explicit Lifetime Syntax

```liva
// Referanslarda yasam suresi anotasyonu
func longest<'a>(x: ref 'a String, y: ref 'a String) -> ref 'a String {
    if x.length() > y.length() {
        return x
    }
    return y
}
```

## 16. Generator Fonksiyonlar (Yield)

```liva
func fibonacci() {
    var a = 0
    var b = 1
    while true {
        yield a        // deger uret ve askiya al
        let tmp = a
        a = b
        b = tmp + b
    }
}
```

## 17. Enum Discriminant Degerleri

```liva
enum HttpStatus {
    case OK = 200
    case NotFound = 404
    case InternalError = 500
}
```

## 18. Generic Associated Types (GATs)

```liva
protocol LendingIterator {
    type Item<'a>
    func next(mut self) -> i32
}
```

---

## 19. Swift-Tarzı Sınıf Özellikleri

Tam sınıf araç setine ait reçeteler.

### 19.1 Statik factory ve sayaç

```liva
class Counter {
    static var total: i32

    static func make() -> Counter {
        Counter.total = Counter.total + 1
        return Counter()
    }
}
```

### 19.2 Hesaplanmış özellikler

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

### 19.3 Loglama için özellik gözlemcileri

```liva
class Volume {
    var level: i32 {
        willSet {
            println("değişiyor:")
            println(self.level)
            println("yeni değer:")
            println(newValue)
        }
        didSet { println("değişti") }
    }
    init() { self.level = 0 }
}
```

### 19.4 Failable init — girdi doğrulama

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

### 19.5 Designated + convenience init aşırı yükleme

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

### 19.6 Pahalı hesaplamalar için lazy

```liva
class Report {
    var rows: i32
    lazy var summary: i32 = self.rows * self.rows * 1000

    init(rows: i32) { self.rows = rows }
}

let r = Report(500)
println(r.summary)   // ilk erişimde hesaplanır
println(r.summary)   // ikinci seferde cache'ten
```

### 19.7 Subscript tabanlı tablo

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

### 19.8 `is` / `as?` ile çalışma zamanı tip kontrolü

```liva
class Shape {
    func name(ref self) -> string { return "Shape" }
}
class Circle : Shape { override func name(ref self) -> string { return "Circle" } }
class Square : Shape { override func name(ref self) -> string { return "Square" } }

func describe(s: Shape) {
    if s is Circle {
        println("daire bu")
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

### 19.10 Erişim seviyeleri

```liva
open class Widget { }          // kalıtım alınabilir
public class Sealed { }        // kalıtım alınamaz
class Button : Widget { }      // OK

class Account {
    private var pin: i32        // sadece Account içinden
    fileprivate var balance: i32 // diğer çağrılardan gizli
    init() {
        self.pin = 0
        self.balance = 0
    }
}
```

### 19.11 Extension metodu

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

*Bu yemek kitabi, 1.0.0 surumu itibariyla Liva'daki yaygin kaliplari kapsar.*
