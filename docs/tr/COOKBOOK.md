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

*Bu yemek kitabı Liva'daki yaygın kalıpları kapsar. Eksiksiz dil referansı için [LANGUAGE-REFERENCE.md](LANGUAGE-REFERENCE.md) belgesine bakın. Standart kütüphane API'si için [API-REFERENCE.md](API-REFERENCE.md) belgesine bakın.*
