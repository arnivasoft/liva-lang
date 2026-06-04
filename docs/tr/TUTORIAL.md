# Liva Dili Eğitimi

Liva programlama dilini öğrenmek için uygulamalı, adım adım bir rehber — ilk programınızdan generics, ownership ve async/await gibi ileri düzey özelliklere kadar.

> **Ön koşullar:** Herhangi bir dilde temel programlama deneyimi. Derleme ve kurulum talimatları için [README.md](README.md) dosyasına bakın.

---

## İçindekiler

1. [Merhaba, Dünya!](#1-merhaba-dünya)
2. [Değişkenler ve Sabitler](#2-değişkenler-ve-sabitler)
3. [Primitif Tipler](#3-primitif-tipler)
4. [Operatörler](#4-operatörler)
5. [Kontrol Akışı](#5-kontrol-akışı)
6. [Fonksiyonlar](#6-fonksiyonlar)
7. [String'ler](#7-stringler)
8. [Diziler](#8-diziler)
9. [Struct'lar ve Metodlar](#9-structlar-ve-metodlar)
10. [Enum'lar ve Pattern Matching](#10-enumlar-ve-pattern-matching)
11. [Closure'lar](#11-closurelar)
12. [Generics](#12-generics)
13. [Protocol'ler (Trait'ler)](#13-protocoller-traitler)
14. [Ownership ve Borrowing](#14-ownership-ve-borrowing)
15. [Optional Tipler](#15-optional-tipler)
16. [Hata Yönetimi](#16-hata-yönetimi)
17. [Koleksiyonlar: Map'ler ve Set'ler](#17-koleksiyonlar-mapler-ve-setler)
18. [Tuple'lar](#18-tuplelar)
19. [Modüller ve Import'lar](#19-modüller-ve-importlar)
20. [Async/Await](#20-asyncawait)
21. [İleri Düzey Özellikler](#21-ileri-düzey-özellikler)
22. [Proje Yönetimi](#22-proje-yönetimi)
23. [Araçlar](#23-araçlar)
24. [Sırada Ne Var?](#24-sırada-ne-var)
25. [Sınıflar ve OOP](#25-sınıflar-ve-oop)
26. [Kodunuzu Test Etme](#26-kodunuzu-test-etme)
27. [Derleme Zamanı Özellikleri](#27-derleme-zamanı-özellikleri)

---

## 1. Merhaba, Dünya!

Her Liva programı bir `main` fonksiyonuyla başlar. Mümkün olan en basit programı yazalım:

```liva
func main() {
    println("Hello, World!")
}
```

Bunu `hello.liva` olarak kaydedin ve derleyin:

```bash
livac -o hello hello.liva
./hello
# Çıktı: Hello, World!
```

**Burada ne oluyor:**
- `func` bir fonksiyon bildirir
- `main()` giriş noktasıdır — her çalıştırılabilir dosyanın birine ihtiyacı var
- `println()` bir değeri ardından satır sonu ile yazdırır
- Noktalı virgül gerekmez — Liva satır sonlarını ifade ayırıcı olarak kullanır
- Süslü parantezler `{ }` blokları sınırlar

Satır sonu eklemeyen `print()` fonksiyonunu da kullanabilirsiniz:

```liva
func main() {
    print("Hello, ")
    println("World!")
}
```

### REPL'de Deneyin

Dosya oluşturmadan interaktif REPL kullanarak deneyebilirsiniz:

```bash
livac repl
>>> println("Hello from REPL!")
Hello from REPL!
>>> 2 + 3
5
```

---

## 2. Değişkenler ve Sabitler

Liva'da değerlere isim bağlamanın üç yolu vardır:

### `let` ile değişmez bağlamalar

```liva
func main() {
    let name: string = "Liva"
    let year: i32 = 2025
    println(name)
    println(year)

    // name = "Other"   // HATA: değişmez değişkene atama yapılamaz
}
```

`let` bağlamaları başlatıldıktan sonra yeniden atanamaz. Bu varsayılan davranıştır — mümkün olduğunca `let` tercih edin.

### `var` ile değişebilir bağlamalar

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

Oluşturulduktan sonra bir değeri değiştirmeniz gerektiğinde `var` kullanın.

### `const` ile derleme zamanı sabitleri

```liva
const MAX_SIZE: i32 = 100
const PI: f64 = 3.14159265

func main() {
    println(MAX_SIZE)
    println(PI)
}
```

`const` değerleri derleme zamanında değerlendirilir. Basit literal ifadeler veya sabit işlemler (diğer sabitler üzerinde aritmetik) olmalıdır. Gerçekten sabit ve derleme zamanında bilinen değerler için `const` kullanın.

### Tip çıkarımı

Tipi her zaman yazmak zorunda değilsiniz — Liva genellikle çıkarabilir:

```liva
func main() {
    let x = 42           // i32 olarak çıkarılır
    let pi = 3.14        // f64 olarak çıkarılır
    let greeting = "Hi"  // string olarak çıkarılır
    let flag = true       // bool olarak çıkarılır

    println(x)
    println(pi)
    println(greeting)
    println(flag)
}
```

**Pratik kural:** Sağ taraftan tip açıkça anlaşıldığında tip belirtimini atlayın. Niyetinizi netleştirdiğinde veya belirli bir tipe ihtiyaç duyduğunuzda ekleyin (örn. `let x: i64 = 42`).

---

## 3. Primitif Tipler

Liva zengin bir yerleşik tip seti sunar:

### Tamsayı tipleri

| Tip | Boyut | Aralık |
|-----|-------|--------|
| `i8` | 8-bit | -128 ile 127 |
| `i16` | 16-bit | -32.768 ile 32.767 |
| `i32` | 32-bit | -2^31 ile 2^31-1 |
| `i64` | 64-bit | -2^63 ile 2^63-1 |
| `u8` | 8-bit | 0 ile 255 |
| `u16` | 16-bit | 0 ile 65.535 |
| `u32` | 32-bit | 0 ile 2^32-1 |
| `u64` | 64-bit | 0 ile 2^64-1 |

`i32` varsayılan tamsayı tipidir.

### Kayan nokta tipleri

| Tip | Boyut | Hassasiyet |
|-----|-------|------------|
| `f32` | 32-bit | ~7 ondalık basamak |
| `f64` | 64-bit | ~15 ondalık basamak |

`f64` varsayılan kayan nokta tipidir.

### Diğer tipler

| Tip | Açıklama | Örnek |
|-----|----------|-------|
| `bool` | Boolean | `true`, `false` |
| `string` | Metin | `"Hello"` |
| `void` | Değer yok | Hiçbir şey döndürmeyen fonksiyonlar için |

### `as` ile tip dönüştürme

`as` anahtar kelimesini kullanarak sayısal tipler arasında dönüşüm yapabilirsiniz:

```liva
func main() {
    let x: i32 = 42
    let y: i64 = x as i64        // i32 → i64 genişletme
    let z: f64 = x as f64        // tamsayı → float

    let pi: f64 = 3.14
    let rounded: i32 = pi as i32  // float → tamsayı (keser)

    println(y)        // 42
    println(z)        // 42.0
    println(rounded)  // 3
}
```

---

## 4. Operatörler

### Aritmetik

```liva
func main() {
    let a: i32 = 17
    let b: i32 = 5

    println(a + b)    // 22  — toplama
    println(a - b)    // 12  — çıkarma
    println(a * b)    // 85  — çarpma
    println(a / b)    // 3   — tamsayı bölme
    println(a % b)    // 2   — modül (kalan)
}
```

### Karşılaştırma

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

### Mantıksal

```liva
func main() {
    let a = true
    let b = false

    println(a && b)    // false — mantıksal VE
    println(a || b)    // true  — mantıksal VEYA
    println(!a)        // false — mantıksal DEĞİL
}
```

### Bileşik atama

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

### Bit düzeyinde

```liva
func main() {
    let a: i32 = 0b1100   // 12
    let b: i32 = 0b1010   // 10

    println(a & b)    // 8   (0b1000) — VE
    println(a | b)    // 14  (0b1110) — VEYA
    println(a ^ b)    // 6   (0b0110) — XOR
    println(~a)       // bit düzeyinde DEĞİL
    println(a << 2)   // 48  — sola kaydırma
    println(a >> 1)   // 6   — sağa kaydırma
}
```

---

## 5. Kontrol Akışı

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

**Not:** Koşullar parantez gerektirmez (C/Java'dan farklı olarak), ama süslü parantezler her zaman gereklidir.

### while döngüleri

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

### for-in döngüleri

Liva iterasyon için `for-in` kullanır — C tarzı `for(;;)` döngüsü yoktur.

```liva
func main() {
    // Aralık iterasyonu (0'dan 4'e)
    for i in 0..5 {
        println(i)
    }

    // Dizi iterasyonu
    let fruits = ["elma", "muz", "kiraz"]
    for fruit in fruits {
        println(fruit)
    }
}
```

`..` operatörü yarı-açık bir aralık oluşturur: `0..5` yani 0, 1, 2, 3, 4.

### break ve continue

```liva
func main() {
    // break döngüden çıkar
    var i: i32 = 0
    while true {
        if i >= 5 {
            break
        }
        println(i)
        i = i + 1
    }
    // Yazdırır: 0 1 2 3 4

    // continue sonraki iterasyona atlar
    for j in 0..10 {
        if j % 2 == 0 {
            continue
        }
        println(j)
    }
    // Yazdırır: 1 3 5 7 9
}
```

### guard

`guard` erken çıkış yapısıdır — koşulun doğru olmasını gerektirir, aksi halde `else` bloğu return veya break yapmalıdır:

```liva
func process(value: i32) {
    guard value > 0 else {
        println("Geçersiz: pozitif olmalı")
        return
    }

    // Buraya ulaşırsak, value > 0 garantili
    println(value)
}

func main() {
    process(42)   // 42 yazdırır
    process(-1)   // "Geçersiz: pozitif olmalı" yazdırır
}
```

### Ternary ifadeler

```liva
func main() {
    let x: i32 = 10
    let label = x > 0 ? "pozitif" : "pozitif değil"
    println(label)  // "pozitif"

    let abs_val = x < 0 ? 0 - x : x
    println(abs_val)  // 10
}
```

---

## 6. Fonksiyonlar

### Temel fonksiyonlar

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

- Parametreler `isim: Tip` olarak bildirilir
- Dönüş tipi `->` ile belirtilir (`void` fonksiyonlar için atlanır)
- Pozisyonel argümanlarla çağrılır: `add(3, 4)`

### Varsayılan parametreler

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

Varsayılan parametreler, varsayılan olmayan parametrelerden sonra gelmelidir.

### Rekürsif fonksiyonlar

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

### Variadic fonksiyonlar

Fonksiyonlar `...` kullanarak değişken sayıda argüman kabul edebilir:

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

### Fonksiyonları argüman olarak geçirme

Fonksiyonlar birinci sınıf değerlerdir — diğer fonksiyonlara geçirebilirsiniz:

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

`(i32) -> i32` tipi, bir `i32` alan ve `i32` döndüren bir fonksiyonu tanımlar.

---

## 7. String'ler

Liva'daki string'ler UTF-8 kodludur ve zengin bir işlem seti destekler.

### String temelleri

```liva
func main() {
    let greeting = "Hello, World!"
    println(greeting)

    // String uzunluğu
    println(len(greeting))    // 13

    // Birleştirme
    let first = "Hello"
    let second = "World"
    let combined = first + ", " + second + "!"
    println(combined)
}
```

### String interpolation

Değerleri string'lerin içine gömmek için `\(ifade)` kullanın:

```liva
func main() {
    let name = "Alice"
    let age: i32 = 30

    println("Adım \(name) ve \(age) yaşındayım.")
    println("Gelecek yıl \(age + 1) olacağım.")
}
```

### String metodları

```liva
func main() {
    let s = "Hello World"

    // Arama
    println(s.contains("World"))     // true
    println(s.startsWith("Hello"))   // true
    println(s.endsWith("World"))     // true
    println(s.indexOf("World"))      // 6

    // Dönüştürme
    println(s.toUpper())             // HELLO WORLD
    println(s.toLower())             // hello world
    println(s.replace("World", "Liva"))  // Hello Liva

    // Çıkarma
    println(s.substring(0, 5))       // Hello
    println(s.trim())                // Hello World (boşlukları kaldırır)

    // Bölme
    let csv = "elma,muz,kiraz"
    let parts = csv.split(",")
    for part in parts {
        println(part)
    }
}
```

### String indeksleme ve dilimleme

```liva
func main() {
    let s = "Hello"

    // Tek karakter erişimi
    let ch = s[0]
    println(ch)    // H

    // Aralıklarla dilimleme
    let sub = s[0..3]
    println(sub)   // Hel
}
```

### Çok argümanlı print

`println` boşluklarla ayrılmış birden fazla argüman alabilir:

```liva
func main() {
    let x: i32 = 10
    let y: i32 = 20
    println("x =", x, "y =", y)  // x = 10 y = 20
}
```

### Format fonksiyonu

```liva
func main() {
    let msg = format("Ad: {}, Yaş: {}", "Alice", 30)
    println(msg)  // Ad: Alice, Yaş: 30
}
```

---

## 8. Diziler

Diziler aynı tipte elemanlardan oluşan sıralı, dinamik boyutlu koleksiyonlardır.

### Dizi oluşturma

```liva
func main() {
    // Dizi literal'i
    let numbers = [1, 2, 3, 4, 5]

    // Açık tipli
    let names: [string] = ["Alice", "Bob", "Charlie"]

    // Değişebilir dizi
    var scores: [i32] = [90, 85, 92]

    println(len(numbers))  // 5
    println(numbers[0])    // 1
    println(numbers[4])    // 5
}
```

### Dizileri değiştirme

```liva
func main() {
    var arr: [i32] = [10, 20, 30]

    // İndeksle atama
    arr[0] = 100
    println(arr[0])    // 100

    // Sona ekleme
    arr.push(40)
    println(arr.length)  // 4

    // Sondan çıkarma
    let last = arr.pop()
    println(arr.length)  // 3
}
```

### Dizi metodları

```liva
func main() {
    var arr: [i32] = [10, 20, 30, 40, 50]

    // Özellikler
    println(arr.length)     // 5
    println(arr.isEmpty)    // false

    // Arama
    println(arr.contains(30))  // true
    println(arr.indexOf(40))   // 3

    // Ters çevirme
    arr.reverse()
    // arr artık [50, 40, 30, 20, 10]

    // Dilimleme
    let slice = arr[1..4]
    // slice [40, 30, 20]
}
```

### Diziler üzerinde iterasyon

```liva
func main() {
    let fruits = ["elma", "muz", "kiraz"]

    // for-in döngüsü
    for fruit in fruits {
        println(fruit)
    }

    // Aralık kullanarak indeksli
    for i in 0..len(fruits) {
        println(fruits[i])
    }
}
```

### Higher-order dizi metodları

```liva
func main() {
    var numbers: [i32] = [1, 2, 3, 4, 5]

    // forEach: her eleman için bir fonksiyon çalıştır
    numbers.forEach(|x| {
        println(x)
    })

    // map: her elemanı dönüştür
    let doubled = numbers.map(|x: i32| -> i32 { return x * 2 })
    // doubled [2, 4, 6, 8, 10]

    // filter: koşula uyan elemanları tut
    let evens = numbers.filter(|x: i32| -> bool { return x % 2 == 0 })
    // evens [2, 4]

    // reduce: tüm elemanları tek bir değere birleştir
    let sum = numbers.reduce(0) { |acc, x| acc + x }
    // sum 15
}
```

---

## 9. Struct'lar ve Metodlar

Struct'lar Liva'da özel veri tipleri oluşturmanın birincil yoludur.

### Struct tanımlama

```liva
struct Point {
    var x: f64
    var y: f64
}

func main() {
    // Örnek oluştur
    let p = Point { x: 3.0, y: 4.0 }

    // Alanlara eriş
    println(p.x)   // 3.0
    println(p.y)   // 4.0

    // Değişebilir struct — alanlar değiştirilebilir
    var q = Point { x: 0.0, y: 0.0 }
    q.x = 10.0
    q.y = 20.0
    println(q.x)   // 10.0
}
```

### `impl` ile metod ekleme

Metodlar ayrı bir `impl` bloğunda tanımlanır:

```liva
struct Point {
    var x: f64
    var y: f64
}

impl Point {
    // Statik metod (self parametresi yok) — constructor görevi görür
    func new(x: f64, y: f64) -> Point {
        return Point { x: x, y: y }
    }

    // Değişmez metod — alanları okuyabilir
    func distanceFromOrigin(ref self) -> f64 {
        return sqrt(self.x * self.x + self.y * self.y)
    }

    // Değişebilir metod — alanları değiştirebilir
    func translate(ref mut self, dx: f64, dy: f64) {
        self.x = self.x + dx
        self.y = self.y + dy
    }

    // Değer metodu — self'in ownership'ini alır
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

**Self parametre tipleri:**
| Sözdizimi | Anlam | Okuyabilir? | Değiştirebilir? |
|-----------|-------|-------------|-----------------|
| `self` | Ownership alır | Evet | Geçersiz (tüketildi) |
| `ref self` | Değişmez borrow | Evet | Hayır |
| `ref mut self` | Değişebilir borrow | Evet | Evet |

### Örnek: Stack oluşturma

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

## 10. Enum'lar ve Pattern Matching

### Basit enum'lar

```liva
enum Color {
    case Red
    case Green
    case Blue
}

func main() {
    let c = Color.Green

    match c {
        Color.Red => println("Kırmızı")
        Color.Green => println("Yeşil")
        Color.Blue => println("Mavi")
    }
}
```

### Associated value'lu enum'lar

Enum variant'ları veri taşıyabilir — bu Liva'nın en güçlü özelliklerinden biridir:

```liva
enum Shape {
    case Circle(f64)              // yarıçap
    case Rectangle(f64, f64)      // genişlik, yükseklik
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
        Shape.Circle(r) => println("Yarıçapı \(r) olan daire")
        Shape.Rectangle(w, h) => {
            println("Dikdörtgen \(w) x \(h)")
        }
        Shape.Empty => println("Boş şekil")
    }
}

func main() {
    let circle = Shape.Circle(5.0)
    let rect = Shape.Rectangle(3.0, 4.0)

    println(area(circle))   // 78.53975
    println(area(rect))     // 12.0

    describe(circle)        // Yarıçapı 5.0 olan daire
}
```

### Pattern matching özellikleri

#### Wildcard pattern'ler

```liva
func classify(x: i32) {
    match x {
        0 => println("sıfır")
        1 => println("bir")
        _ => println("başka bir şey")
    }
}
```

#### Match guard'ları

```liva
func classify_number(x: i32) {
    match x {
        n if n < 0 => println("negatif")
        0 => println("sıfır")
        n if n > 100 => println("büyük")
        _ => println("normal")
    }
}
```

#### İç içe pattern'ler

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

Pattern eşleşmesi başarısız olana kadar tekrarla:

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

## 11. Closure'lar

Closure'lar çevreleyen kapsamdaki değişkenleri yakalayabilen anonim fonksiyonlardır.

### Temel closure sözdizimi

```liva
func main() {
    // İsimli fonksiyon
    func double(x: i32) -> i32 {
        return x * 2
    }

    // Eşdeğer closure
    let double_closure = |x: i32| -> i32 { return x * 2 }

    println(double(5))          // 10
    println(double_closure(5))  // 10
}
```

### Higher-order fonksiyonlarla closure'lar

```liva
func apply(x: i32, f: (i32) -> i32) -> i32 {
    return f(x)
}

func main() {
    let result = apply(5, |x: i32| -> i32 { return x * x })
    println(result)  // 25
}
```

### Trailing closure sözdizimi

Son parametre bir fonksiyon olduğunda, closure'ı parantezlerden sonra yazabilirsiniz:

```liva
func main() {
    var numbers: [i32] = [1, 2, 3, 4, 5]

    // Normal sözdizimi
    numbers.forEach(|x| { println(x) })

    // Trailing closure sözdizimi
    numbers.forEach { |x|
        println(x)
    }
}
```

### Referans ile yakalama

Closure'lar çevreleyen kapsamdaki değişkenleri yakalar. Varsayılan olarak, değişebilir değişkenler referans ile yakalanır:

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

### Pratik örnek: toplayıcı

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

Generics, herhangi bir tipte çalışan kod yazmanızı sağlar.

### Generic fonksiyonlar

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

Derleyici `T`'yi argümandan çıkarır — açıkça belirtmeye gerek yoktur.

### Generic struct'lar

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

### Kısıtlanmış generics

Tip parametrelerinin belirli protocol'leri uygulamasını zorunlu kılabilirsiniz:

```liva
protocol Printable {
    func toString(ref self) -> string
}

func printItem<T: Printable>(item: ref T) {
    println(item.toString())
}
```

Daha fazla ayrıntı için [Protocol'ler](#13-protocoller-traitler) bölümüne bakın.

### Where clause'lar

Daha karmaşık kısıtlamalar için `where` kullanın:

```liva
func process<T, U>(a: T, b: U) where T: Printable, U: Printable {
    println(a.toString())
    println(b.toString())
}
```

---

## 13. Protocol'ler (Trait'ler)

Protocol'ler, tiplerin uygulaması gereken bir metod seti tanımlar — diğer dillerdeki interface'lere veya Rust'taki trait'lere benzer.

### Protocol tanımlama

```liva
protocol Describable {
    func describe(ref self) -> string
}
```

### Protocol uygulama

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

### Kısıtlama olarak protocol

```liva
func printDescription<T: Describable>(item: ref T) {
    println(item.describe())
}

func main() {
    let dog = Dog { name: "Rex", age: 5 }
    printDescription(ref dog)  // Rex
}
```

### Varsayılan uygulamalar

Protocol'ler varsayılan metod uygulamaları sağlayabilir:

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
    // greet() varsayılan uygulamayı kullanır
}

func main() {
    let p = Person { personName: "Alice" }
    println(p.greet())  // Hello, Alice!
}
```

### Çoklu protocol uyumluluğu

Bir tip birden fazla protocol uygulayabilir:

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
        return "Skor: " + toString(self.value)
    }
}

impl Comparable for Score {
    func compareTo(ref self, other: ref Score) -> i32 {
        return self.value - other.value
    }
}
```

### Protocol'ler aracılığıyla operator overloading

Liva, operator overloading için protocol'ler kullanır:

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
    let c = a + b   // Addable.add çağrılır
    println(c.x)    // 4.0
    println(c.y)    // 6.0
}
```

Mevcut operator protocol'leri: `Addable` (+), `Subtractable` (-), `Multipliable` (*), `Dividable` (/), `Equatable` (==, !=), `Comparable` (<, >, <=, >=).

---

## 14. Ownership ve Borrowing

Liva, garbage collector olmadan bellek güvenliğini garanti etmek için ownership ve borrowing kullanır. Bu, Liva'nın belirleyici özelliklerinden biridir.

### Ownership kuralları

1. Her değerin tam olarak bir **sahibi** vardır
2. Sahip kapsam dışına çıktığında, değer **drop** edilir (serbest bırakılır)
3. Ownership **transfer** edilebilir (move)

### Move semantics

Copy olmayan bir değeri (string veya struct gibi) başka bir değişkene atadığınızda veya bir fonksiyona geçirdiğinizde, ownership **taşınır**:

```liva
struct Buffer {
    var size: i32
}

func consume(buf: Buffer) {
    println(buf.size)
    // buf burada drop edilir
}

func main() {
    var buf = Buffer { size: 1024 }
    consume(buf)
    // buf taşındı — burada kullanmak hata olur
    // println(buf.size)  // HATA: move sonrası kullanım
}
```

### Copy tipler

Primitif tipler (`i32`, `f64`, `bool`, vb.) **Copy**'dir — taşınmak yerine çoğaltılır:

```liva
func main() {
    let x: i32 = 42
    let y = x       // x kopyalanır, taşınmaz
    println(x)      // OK — x hala geçerli
    println(y)      // 42
}
```

### Referanslarla borrowing

Ownership transfer etmek yerine bir değeri **ödünç** alabilirsiniz:

```liva
// Değişmez borrow — okuyabilir ama değiştiremez
func print_size(buf: ref Buffer) {
    println(buf.size)
}

// Değişebilir borrow — okuyabilir ve değiştirebilir
func double_size(buf: ref mut Buffer) {
    buf.size = buf.size * 2
}

func main() {
    var buf = Buffer { size: 100 }

    print_size(ref buf)       // borrow — buf hala geçerli
    println(buf.size)          // 100

    double_size(ref mut buf)  // değişebilir borrow
    println(buf.size)          // 200
}
```

### Borrowing kuralları

1. Aynı anda **birden fazla değişmez borrow** (`ref`) olabilir
2. Aynı anda **yalnızca bir değişebilir borrow** (`ref mut`) olabilir
3. Değişmez ve değişebilir borrow'lar aynı anda olamaz

Bu kurallar derleme zamanında veri yarışlarını önler.

### Drop trait

`Drop` protocol'ü ile özel temizleme mantığı tanımlayabilirsiniz:

```liva
struct Connection {
    var id: i32
}

impl Drop for Connection {
    func drop(mut self) {
        println("Bağlantı kapatılıyor \(self.id)")
    }
}

func main() {
    let conn = Connection { id: 1 }
    // ... bağlantıyı kullan ...
}  // conn.drop() burada otomatik çağrılır
```

---

## 15. Optional Tipler

Optional tipler var olabilecek veya olmayabilecek değerleri temsil eder — Liva'nın null pointer'lara alternatifi.

### Optional bildirimi

```liva
func main() {
    let x: i32? = 42      // bir değeri var
    let y: i32? = nil      // değeri yok

    println(x)   // 42
    println(y)   // nil
}
```

`?` soneki herhangi bir tipi optional yapar: `i32?`, `string?`, `Point?`, vb.

### Optional döndürme

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

Optional `nil` olduğunda varsayılan değer sağlayın:

```liva
func main() {
    let name: string? = nil
    let display = name ?? "Anonim"
    println(display)  // Anonim

    let score: i32? = 95
    println(score ?? 0)  // 95
}
```

### Optional chaining (`?.`)

Optional değerler üzerinde alanlara veya metodlara güvenli erişim:

```liva
struct User {
    var name: string
    var age: i32
}

func main() {
    var user: User? = User { name: "Alice", age: 30 }

    let name = user?.name ?? "Bilinmiyor"
    println(name)  // Alice

    user = nil
    let name2 = user?.name ?? "Bilinmiyor"
    println(name2)  // Bilinmiyor
}
```

### if-let bağlama

Optional'ı unwrap edip yeni bir değişkene bağlayın:

```liva
func main() {
    let value: i32? = 42

    if let v = value {
        println("Değer var: \(v)")
    } else {
        println("Değer yok")
    }
}
```

### Force unwrap (`!`)

Bir optional'ın kesinlikle değeri olduğundan eminseniz, unwrap etmek için `!` kullanın. **Değer `nil` ise çöker:**

```liva
func main() {
    let x: i32? = 42
    let value = x!       // OK — x'in değeri var
    println(value)        // 42

    // let y: i32? = nil
    // let bad = y!       // ÇÖKME: nil'in force unwrap'ı
}
```

`!` kullanımını azaltın — bunun yerine `??`, `if let` veya pattern matching tercih edin.

---

## 16. Hata Yönetimi

Liva hata yönetimi için `Result` tipini kullanır — exception yoktur.

### Result tipi

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
    return Ok("dosya içeriği burada")
}
```

### match ile result yönetimi

```liva
func main() {
    let result = readFile("data.txt")

    match result {
        Ok(content) => {
            println("Başarıyla okundu:")
            println(content)
        }
        Err(FileError.NotFound) => {
            println("Dosya bulunamadı!")
        }
        Err(FileError.PermissionDenied) => {
            println("İzin reddedildi!")
        }
        Err(_) => {
            println("Bilinmeyen hata")
        }
    }
}
```

### `try` ile hata yayma

Hataları çağrı yığınında yukarı yaymak için `try` kullanın:

```liva
func processFile(path: string) -> Result<i32, FileError> {
    let content = try readFile(path)
    // readFile Err döndürürse, processFile hemen o Err'i döndürür
    // readFile Ok döndürürse, content unwrap edilmiş değeri alır
    return Ok(len(content))
}
```

---

## 17. Koleksiyonlar: Map'ler ve Set'ler

### Map'ler (hash map'ler)

```liva
func main() {
    // Map oluştur
    var ages: Map<string, i32>

    // Anahtar-değer çiftleri ekle
    ages.insert("Alice", 30)
    ages.insert("Bob", 25)
    ages.insert("Charlie", 35)

    // Arama
    let alice_age = ages.get("Alice")
    println(alice_age)  // 30

    // Varlık kontrolü
    println(ages.contains("Bob"))      // true
    println(ages.contains("Diana"))    // false

    // Silme
    ages.remove("Bob")

    // Boyut
    println(ages.size)      // 2
    println(ages.isEmpty)   // false

    // İterasyon
    for (key, value) in ages {
        println(key)
    }
}
```

### Set'ler (hash set'ler)

```liva
func main() {
    var numbers: Set<i32>

    // Elemanlar ekle
    numbers.insert(10)
    numbers.insert(20)
    numbers.insert(30)
    numbers.insert(20)  // tekrar — yoksayılır

    // Üyelik kontrolü
    println(numbers.contains(20))  // true
    println(numbers.contains(99))  // false

    // Silme
    numbers.remove(10)

    // Boyut
    println(numbers.size)      // 2
    println(numbers.isEmpty)   // false

    // İterasyon
    for item in numbers {
        println(item)
    }
}
```

---

## 18. Tuple'lar

Tuple'lar farklı tiplerdeki birden fazla değeri tek bir bileşik değerde gruplar.

### Tuple oluşturma ve erişim

```liva
func main() {
    let pair = (42, "hello")

    // İndeksle erişim
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

### Çoklu dönüş değerleri

Tuple'lar bir fonksiyondan birden fazla değer döndürmenin deyimsel yoludur:

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

## 19. Modüller ve Import'lar

### Standart kütüphane modülleri

Liva yerleşik standart kütüphane modülleri sağlar:

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
import std::ui        // raylib tabanlı UI framework
```

Şemsiye modülü de import edebilirsiniz:

```liva
import std   // std::*'daki her şeyi import eder
```

### Modül kullanımı

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

### Kullanıcı tanımlı modüller

`math_utils.liva` dosyası oluşturun:

```liva
pub func add(a: i32, b: i32) -> i32 {
    return a + b
}

pub func multiply(a: i32, b: i32) -> i32 {
    return a * b
}
```

Başka bir dosyada import edin:

```liva
import math_utils

func main() {
    let sum = add(3, 4)
    let product = multiply(5, 6)
    println(sum)       // 7
    println(product)   // 30
}
```

Fonksiyonları ve tipleri import edenlere görünür kılmak için `pub` kullanın.

---

## 20. Async/Await

Liva, `async` ve `await` ile asenkron programlamayı destekler.

### Async fonksiyonlar

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

### Temel kavramlar

- Fonksiyonları asenkron yapmak için `async` ile işaretleyin
- Asenkron bir işlemin tamamlanmasını beklemek için `await` kullanın
- Async fonksiyonlar runtime tarafından çalıştırılan bir coroutine döndürür
- `await` yalnızca `async` fonksiyonlar içinde kullanılabilir

### For await (asenkron iterasyon)

Asenkron akışlar üzerinde iterasyon için `for await` kullanın:

```liva
async func processItems() {
    for await item in asyncStream {
        println(item)
    }
}
```

Not: `for await` sadece `async` fonksiyonların içinde kullanılabilir.

### Ağ örneği

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

## 21. İleri Düzey Özellikler

### Type alias'lar

Karmaşık tipler için daha kısa isimler oluşturun:

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

### Özel iterator'lar

Tiplerinizi `for-in` ile çalışır hale getirmek için `Iterator` protocol'ünü uygulayın:

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
    // Yazdırır: 0 1 2 3 4
}
```

### Regex

```liva
import std::regex

func main() {
    let pattern = Regex.new("[0-9]+")
    let text = "Sipariş 42'de 3 ürün var"

    if pattern.match(text) {
        println("Sayılar bulundu!")
    }

    let cleaned = pattern.replace(text, "N")
    println(cleaned)  // Sipariş N'de N ürün var
}
```

### Ortam ve süreç

```liva
import std::os

func main() {
    // Ortam değişkenleri
    let home = env("HOME")
    println(home)

    // Komut satırı argümanları
    let arguments = args()
    for arg in arguments {
        println(arg)
    }

    // Çıkış kodu ile çık
    exit(0)
}
```

### Rastgele sayılar

```liva
import std::random

func main() {
    let dice = randInt(1, 6)
    println(dice)

    let probability = randFloat(0.0, 1.0)
    println(probability)
}
```

### Comptime blokları

```liva
func main() {
    // Derleme zamanı değerlendirme
    let lookup = comptime {
        var table: [i32] = []
        var i = 0
        while i < 5 {
            table.push(i * i)
            i = i + 1
        }
        table
    }
    // lookup [0, 1, 4, 9, 16] — derleme zamanında hesaplandı
    println(lookup)
}
```

### Macro'lar

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

### Guard koşulları

```liva
func divide(a: f64, b: f64) -> f64? {
    guard b != 0.0 else {
        println("Sıfıra bölünemez")
        return nil
    }
    return a / b
}
```

---

## 22. Proje Yönetimi

### Proje oluşturma

```bash
livac init myproject
cd myproject
```

Bu, bir `liva.toml` manifest dosyası ve temel proje yapısı oluşturur.

### liva.toml

```toml
[project]
name = "myproject"
version = "0.1.0"
entry = "src/main.liva"

[build]
optimization = "debug"      # veya "release"
extra_flags = ["-lm"]       # ek linker flag'leri

[dependencies]
json_parser = "^1.0.0"      # semantic versioning
utils = "~2.3.0"
```

### Versiyon kısıtlamaları

| Sözdizimi | Anlam | Örnek |
|-----------|-------|-------|
| `^1.2.3` | Uyumlu (aynı major) | 1.2.3 ≤ v < 2.0.0 |
| `~1.2.3` | Yama seviyesi (aynı minor) | 1.2.3 ≤ v < 1.3.0 |
| `=1.2.3` | Tam versiyon | Sadece 1.2.3 |
| `>=1.0.0` | Minimum versiyon | 1.0.0 veya daha yeni |

### Build ve çalıştırma

```bash
# Projeyi build et
livac build

# Release modunda build et
livac build --release

# Projeyi çalıştır
livac run
```

### Lock dosyası

Bağımlılıklar çözümlendikten sonra Liva, tekrarlanabilir build'ler için tam versiyonları sabitleyen bir `liva.lock` dosyası oluşturur. Bu dosyayı versiyon kontrolüne kaydedin.

---

## 23. Araçlar

### LSP Sunucusu

Liva, editör entegrasyonu için bir Language Server Protocol (LSP) sunucusu içerir:

```bash
livac lsp
```

**Desteklenen özellikler:**
- Tanılama (hatalar ve uyarılar)
- Kod tamamlama (anahtar kelimeler, yerleşik fonksiyonlar, semboller)
- Hover bilgisi (fonksiyon imzaları, değişken tipleri)
- Tanıma git
- Tüm referansları bul
- Sembol yeniden adlandırma
- İmza yardımı (parametre ipuçları)
- Doküman sembolleri (anahat)
- Kod Aksiyonları (hızlı düzeltme önerileri)
- Kod Merceği (referans sayıları)
- Çağrı Hiyerarşisi (gelen/giden çağrılar)
- Satır İçi İpuçları (satır içi tip açıklamaları)
- Seçim Aralığı (akıllı seçim)
- Çalışma Alanı Sembolü (proje genelinde arama)

#### VS Code kurulumu

VS Code `settings.json` dosyanıza ekleyin:

```json
{
    "liva.serverPath": "/path/to/livac",
    "liva.serverArgs": ["lsp"]
}
```

### İnteraktif REPL

```bash
livac repl
```

REPL, Liva ile interaktif olarak deney yapmanızı sağlar:

```
Liva REPL v0.1.0
Yardım için :help, çıkmak için :quit yazın.
>>> let x = 42
>>> x + 8
50
>>> func double(n: i32) -> i32 { return n * 2 }
>>> double(x)
84
```

**REPL komutları:**
| Komut | Kısayol | Açıklama |
|-------|---------|----------|
| `:help` | `:h` | Yardım göster |
| `:quit` | `:q` | REPL'den çık |
| `:reset` | `:r` | Tüm bildirimleri temizle |
| `:declarations` | `:decls` | Birikmiş bildirimleri göster |

**Çok satırlı giriş:** REPL eksik girişi (kapatılmamış süslü parantezler, normal parantezler) otomatik algılar ve daha fazla satır bekler:

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

Kodunuzda tanımlanan performans benchmark'larını çalıştırın:

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

### Test Çalıştırıcı

```bash
livac test
```

Projenizdeki tüm `test` bloklarını çalıştırın. Detaylar için [Kodunuzu Test Etme](#26-kodunuzu-test-etme) bölümüne bakın.

### Derleyici tanılama seçenekleri

```bash
livac --dump-tokens file.liva    # Tüm token'ları göster
livac --dump-ast file.liva       # AST'yi göster
livac --check-only file.liva     # Kod üretimi olmadan tip kontrolü
livac --emit-ir file.liva        # LLVM IR çıktısı
```

---

## 24. Sırada Ne Var?

Tebrikler — Liva programlama dilinin çekirdeğini tamamladınız! Daha derine inmek için bazı öneriler:

### Pratik projeler

1. **Hesap makinesi:** Aritmetik ifadeleri ayrıştır ve değerlendir
2. **Yapılacaklar listesi:** struct'lar, diziler ve dosya I/O kullanan bir CLI uygulaması
3. **HTTP istemcisi:** `std::net` kullanarak bir web API'den veri çek ve göster
4. **Veri işleyici:** Bir CSV dosyası oku, ayrıştır, istatistik hesapla
5. **Mini oyun:** Rastgele sayılarla metin tabanlı bir sayı tahmin oyunu

### Örnek: Tam mini program

Birçok özelliği bir arada gösteren eksiksiz bir program:

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

    println("Öğrenci: \(student.name)")
    println("Notlar: \(student.grades.length)")

    if let avg = student.average() {
        let rounded = round(avg, 1)
        println("Ortalama: \(rounded)")
        println("Harf notu: \(letterGrade(avg))")
    }

    if let best = student.highest() {
        println("En yüksek: \(best)")
    }
}
```

### İleri okuma

- [Dil Referansı](LANGUAGE-REFERENCE.md) — Tam sözdizimi ve semantik referans
- [Katkıda Bulunma](CONTRIBUTING.md) — Liva'ya nasıl katkıda bulunulur
- [Örnekler](../../examples/) — Daha fazla örnek program
- [API Referansı](API-REFERENCE.md) — Standart kütüphane modül referansı
- [Yemek Kitabı](COOKBOOK.md) — Yaygın kalıplar ve tarifler

---

## 25. Sınıflar ve OOP

Liva'da sınıflar; kalıtım, yapıcılar, yıkıcılar ve sanal metod dispatch desteğine sahip referans tipleridir.

### Sınıf tanımlama

```liva
class Animal {
    var name: string
    var sound: string

    init(name: string, sound: string) {
        self.name = name
        self.sound = sound
    }

    deinit {
        println("\(self.name) serbest bırakıldı")
    }

    func speak(ref self) {
        println("\(self.name) \(self.sound) der")
    }
}

func main() {
    let cat = Animal("Boncuk", "Miyav")
    cat.speak()  // Boncuk Miyav der
}
```

### Kalıtım ve override

```liva
class Dog : Animal {
    var breed: string

    init(name: string, breed: string) {
        super.init(name, "Hav")
        self.breed = breed
    }

    override func speak(ref self) {
        println("\(self.name) \(self.breed) cinsi Hav der!")
    }
}

func main() {
    let dog = Dog("Karabaş", "Kangal")
    dog.speak()  // Karabaş Kangal cinsi Hav der!
}
```

### Private alanlar

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
    // account.balance  // HATA: 'balance' private
}
```

### Class vs struct ne zaman kullanılır

Basit veri taşıyıcıları için **struct** kullanın (değer semantiği, kalıtım gerekmez).
Kalıtım, sanal dispatch veya referans semantiği gerektiğinde **class** kullanın.

### Temel özelliklerin ötesi

Sınıflar zengin bir Swift tarzı özellik setine sahiptir:

- **Statik üyeler**: `static func`, `static var` — tip-düzeyinde, `self` yok.
- **Hesaplanmış özellikler**: `var area: f64 { get { ... } set { ... } }`.
- **Özellik gözlemcileri**: Stored alanlarda `willSet { ... } didSet { ... }`.
- **Final sınıflar/metotlar**: `final class` ve `final func` kalıtımı/override'ı engeller.
- **Tip kontrolleri**: `expr is Type` → `bool`; `expr as? Type` → `Type?`.
- **Failable init**: `init?(...)` başarısızlığı `return nil` ile bildirebilir;
  sonuç tipi `ClassName?` olur.
- **Çoklu init / convenience**: Sınıflar bir designated `init(...)` ve
  argüman sayısına göre çözümlenen `convenience init(...)` aşırı yükleri
  bildirebilir.
- **Lazy özellikler**: `lazy var x: T = expr` ilk erişimde hesaplanır ve cache'lenir.
- **Subscript**: `subscript(i: T) -> U { get { } set { } }` `obj[i]`'yi aşırı
  yükler; generic subscript `subscript<T>(...)` ile desteklenir.
- **Erişim seviyeleri**: `open`, `public`, `internal`, `fileprivate`, `private`
  (Swift tarzı). Yalnızca `open` sınıflardan kalıtım alınabilir.
- **Extension**: `extension TypeName { func ... }` mevcut bir tipe metot ekler.

Kısa örnek:

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
        println("kare")
    }
}
```

Tam spesifikasyon için [Dil Referansı](LANGUAGE-REFERENCE.md#28-sınıflar-classes)
ve deyimsel tarifler için [Yemek Kitabı](COOKBOOK.md#19-swift-tarzı-sınıf-özellikleri)
bölümlerine bakın.

---

## 26. Kodunuzu Test Etme

Liva, kaynak dosyalarınızda doğrudan test blokları yazmanıza olanak tanıyan dahili bir test framework'üne sahiptir.

### Test yazma

```liva
func add(a: i32, b: i32) -> i32 {
    return a + b
}

test "add doğru toplamı döndürür" {
    assert(add(2, 3) == 5)
    assert(add(-1, 1) == 0)
    assert(add(0, 0) == 0)
}

test "büyük sayılarla add" {
    let result = add(1000000, 2000000)
    assert(result == 3000000)
}
```

### Testleri çalıştırma

```bash
livac test                          # tüm testleri çalıştır
livac test --filter "add"           # kalıba uyan testleri çalıştır
```

Her test izole çalışır — bir testteki başarısızlık diğer testlerin çalışmasını engellemez.

---

## 27. Derleme Zamanı Özellikleri

### Comptime blokları

İfadeleri derleme zamanında değerlendirin:

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

### Macro'lar

Yeniden kullanılabilir kod kalıpları tanımlayın:

```liva
macro unless {
    ($cond, $body) => {
        if !($cond) { $body }
    }
}

func main() {
    let x = 5
    unless!(x > 10, {
        println("x 10'dan büyük değil")
    })
}
```

---

## Yeni Dil Ozellikleri (v1.0)

### Const Generics

Derleme zamani sabit parametreler:

```liva
func repeat<const N: i32>(value: i32) -> i32 {
    return N * value
}

let r = repeat<5>(3)  // N=5, sonuc=15
```

### Explicit Lifetime Sozdizimi

Referanslarda yasam suresi anotasyonlari:

```liva
func first<'a>(x: ref 'a i32) -> ref 'a i32 {
    return x
}

// Elision: derleyici otomatik cikarir
func identity(x: ref i32) -> ref i32 { return x }
```

### Generator Fonksiyonlar

`yield` ile tembel deger uretimi:

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

### Enum Discriminant Degerleri

```liva
enum HttpStatus {
    case OK = 200
    case NotFound = 404
}
```

### Generic Associated Types (GATs)

```liva
protocol LendingIterator {
    type Item<'a>
    func next(mut self) -> i32
}
```

---

*Bu egitim, 2149 teste sahip Liva'nin 1.0.0 surumunu kapsar.*
