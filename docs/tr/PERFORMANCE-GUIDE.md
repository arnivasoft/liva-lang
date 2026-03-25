# Liva Performans Ayarlama Rehberi

## Optimizasyon Seviyeleri

```bash
livac -O0 main.liva          # Optimizasyon yok (en hizli derleme, debug icin)
livac -O1 main.liva          # Temel optimizasyonlar
livac -O2 main.liva          # Standart optimizasyonlar (release icin onerilen)
livac -O3 main.liva          # Agresif optimizasyonlar (binary boyutu artabilir)
livac --release main.liva    # -O2 + debug bilgisi kapali
livac --debug main.liva      # -O0 + debug bilgisi acik
```

| Seviye | Derleme Hizi | Calisma Hizi | Binary Boyutu | Debug Kalitesi |
|--------|-------------|-------------|---------------|----------------|
| `-O0` | En hizli | En yavas | En buyuk | En iyi |
| `-O1` | Hizli | Orta | Orta | Iyi |
| `-O2` | Orta | Hizli | Orta | Sinirli |
| `-O3` | En yavas | En hizli | Buyuyebilir | Sinirli |

## Link-Time Optimization (LTO)

LTO, link zamaninda moduller arasi optimizasyon saglar:

```toml
# liva.toml
[build]
lto = "thin"    # Thin LTO (onerilen: iyi hiz/kalite dengesi)
# lto = "full"  # Full LTO (en iyi optimizasyon, yavas link)
```

## Profile-Guided Optimization (PGO)

PGO, calisma zamani profil verisini kullanarak sicak yollari optimize eder:

```bash
# Adim 1: Profilleme enstrumantasyonu ile derle
livac --pgo=generate -O2 main.liva -o main_instrumented

# Adim 2: Temsili is yuku ile calistir
./main_instrumented

# Adim 3: Profil verisiyle derle
livac --pgo=use --pgo-profile=default.profraw -O2 main.liva -o main_optimized
```

## Derleme Zamani Profilleme

`--dump-timings` ile derleme darbogazlarini tespit edin:

```bash
livac --dump-timings main.liva
```

Cikti:
```
=== Compilation Timings ===
  Parse:    2.1 ms
  Sema:    15.3 ms
  IRGen:   28.7 ms
  Optimize: 12.4 ms
  Emit:     5.1 ms
  Link:    45.2 ms
  Total:  108.8 ms
```

| Faz | Ne Yapar | Yavas ise... |
|-----|---------|-------------|
| **Parse** | Tokenize + AST olustur | Cok buyuk dosya; modullere bol |
| **Sema** | Tip kontrolu + ownership | Karmasik generics veya derin trait hiyerarsisi |
| **IRGen** | LLVM IR uret | Agir monomorphization; generic kullanimini azalt |
| **Optimize** | LLVM optimizasyon gecisleri | Opt seviyesini dusur (-O1 vs -O2) |
| **Link** | Object dosyalari bagla | Thin LTO kullan; bagimliliklari azalt |

## Incremental Build

Liva'nin build cache'i degismemis dosyalari yeniden derlemez:

```bash
livac build                  # Ilk derleme: her seyi derler
livac build                  # Ikinci derleme: sadece degisen dosyalar
livac build --rebuild        # Tam yeniden derleme
livac clean                  # Build cache'i temizle
```

## Monomorphization Ipuclari

Generics her tip icin ozellestirilir (monomorphize). Asiri generic kullanimi derleme suresini ve binary boyutunu artirir.

```liva
// Cok fazla generic ornekleme yerine:
func process<T>(items: [T]) { ... }

// Trait object ile runtime dispatch dusunun:
func process(items: [dyn Processable]) { ... }
// Tek fonksiyon, monomorphization yok
```

## Build Yapilandirmasi

```toml
# liva.toml
[project]
name = "myapp"
version = "1.0.0"
entry = "src/main.liva"

[build]
opt-level = 2       # 0, 1, 2 veya 3
debug = false        # Debug bilgisi ekle
lto = "thin"         # "none", "thin" veya "full"
jobs = 0             # Paralel derleme (0 = otomatik CPU sayisi)
```
