# Liva'ya Katkıda Bulunma

Liva programlama diline katkıda bulunmaya gösterdiğiniz ilgi için teşekkür ederiz!

## Başlarken

### Ön Gereksinimler

- **CMake** 3.20 veya üstü
- **Ninja** build sistemi (Windows'ta `winget install Ninja-build.Ninja`)
- **C++20 derleyici**: Clang 16+, GCC 13+ veya MSVC 2022
- **LLVM 21** (opsiyonel — sadece kod üretimi için gerekli; testler onsuz çalışır)

### Derleme

**Windows (Clang + MSVC ABI — önerilen):**

```batch
build_clang.bat
```

**Windows (MinGW GCC — sadece testler):**

```batch
cmake -G "MinGW Makefiles" -B build
cmake --build build
```

**Linux / macOS:**

```bash
./build.sh
```

### Testleri Çalıştırma

```bash
# MinGW build
ctest --test-dir build --output-on-failure

# Clang build (önerilen — codegen, JIT ve self-host testlerini içerir)
ctest --test-dir build-clang --output-on-failure
```

Pull request göndermeden önce tüm 1600+ testin geçmesi gerekir.

## Proje Yapısı

| Dizin | Açıklama |
|-------|----------|
| `include/liva/` | Tüm bileşenler için genel header dosyaları |
| `src/` | Bileşenlere göre organize edilmiş uygulama dosyaları |
| `tests/unit/` | GoogleTest birim testleri |
| `tests/integration/` | Uçtan uca `.liva` test programları |
| `tests/error/` | Beklenen hata test senaryoları |
| `examples/` | Örnek Liva programları |
| `stdlib/runtime/` | C++ runtime kütüphanesi |
| `cmake/` | CMake modülleri ve toolchain dosyaları |

## Kod Stili

- **C++20** standardı
- 4 boşluk girinti
- Fonksiyon ve değişkenler için `camelCase`, tipler için `PascalCase`
- Otomatik biçimlendirme için `.clang-format` kullanın
- MinGW build'lerinde exception yok (`-fno-exceptions`) — `stoi`/`try-catch` yerine `strtol` kullanın
- Dosya akışı kontrolü için `good()` yerine `is_open()` tercih edin

## Yeni Özellik Ekleme

1. **Parser**: `include/liva/AST/` dizinine yeni AST düğümleri ve `src/Parser/` dizinine ayrıştırma kodu ekleyin
2. **Sema**: `src/Sema/TypeChecker.cpp` dosyasına tip kontrolü kuralları ekleyin
3. **IRGen**: Uygun `src/IR/IRGen*.cpp` dosyasına kod üretimi ekleyin
4. **Testler**: `tests/unit/` altındaki ilgili test dosyasına birim testleri ekleyin
5. **Örnekler**: Özelliği gösteren bir `.liva` örneği `examples/` dizinine ekleyin

## Test Ekleme

Testler GoogleTest kullanır. Her bileşenin kendi test dosyası vardır:

| Bileşen | Test Dosyası |
|---------|-------------|
| Lexer | `tests/unit/LexerTest.cpp` |
| Parser | `tests/unit/ParserTest.cpp` |
| Sema | `tests/unit/SemaTest.cpp` |
| Types | `tests/unit/TypeTest.cpp` |
| Ownership | `tests/unit/OwnershipTest.cpp` |
| Project Config | `tests/unit/ProjectConfigTest.cpp` |
| LSP | `tests/unit/LSPTest.cpp` |
| REPL | `tests/unit/REPLTest.cpp` |
| CodeGen | `tests/unit/CodeGenTest.cpp` |
| Integration | `tests/unit/IntegrationTest.cpp` |
| Macro | `tests/unit/MacroTest.cpp` |
| Plugin | `tests/unit/PluginTest.cpp` |
| Benchmark | `tests/unit/BenchmarkTest.cpp` |
| SelfHost | `tests/unit/SelfHostTest.cpp` |
| JIT | `tests/unit/JITTest.cpp` |
| DAP | `tests/unit/DAPTest.cpp` |

Yeni test eklemek için:

```cpp
TEST(SemaTest, MyNewFeature) {
    std::string code = R"(
func main() {
    // test kodu buraya
}
)";
    SourceManager sm("test", code);
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    Parser parser(lexer, diag);
    auto tu = parser.parseTranslationUnit();
    ASSERT_FALSE(diag.hasErrors());

    Sema sema(diag, nullptr);
    sema.analyze(*tu);
    EXPECT_FALSE(diag.hasErrors());
}
```

## Bilinen Derleme Notları

- MinGW build'leri exception'ları devre dışı bırakır (`-fno-exceptions`) — `stoi`/`try-catch` yerine `strtol` kullanın
- MinGW `livac.exe` linkleme işlemi LLVM kütüphaneleriyle başarısız olur (beklenen davranış); birim testleri yine de çalışır
- `\(` içeren raw string literal'ler özel delimiter gerektirir: `R"--(...)--"`
- Çok alanlı inline struct'lar (`struct Pt { var x: i32; var y: i32 }`) — DÜZELTİLDİ, noktalı virgüller artık doğru şekilde tüketiliyor
- Windows'ta, tırnaklı yollar içeren `std::system()` komutları `cmd.exe` için ek sarmalama gerektirir
- MinGW CTest, Git'in `libstdc++-6.dll` dosyasıyla çakışma nedeniyle SEGFAULT verebilir — MinGW'yi PATH'e ekleyin: `PATH="/c/Program Files/mingw64/bin:$PATH"`

## Pull Request Süreci

1. `main` dalından bir özellik dalı oluşturun
2. Değişikliklerinizi net, odaklanmış commit'lerle yapın
3. Tüm 1600+ testin geçtiğinden emin olun
4. Yeni işlevsellik için yeni testler ekleyin
5. Bir milestone ekliyorsanız `plan.md` dosyasını güncelleyin
6. Net bir açıklama ile pull request gönderin

## Sorun Bildirme

Lütfen sorunları projenin issue tracker'ında şu bilgilerle bildirin:
- Yeniden üretme adımları
- Beklenen ve gerçek davranış
- Derleyici ve platform bilgisi
- Sorunu yeniden üreten minimal `.liva` kodu (varsa)
