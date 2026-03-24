# Liva Derleyici Plugin Rehberi

## Genel Bakis

Liva'nin plugin sistemi, derleme pipeline'ina ozel analiz adimlari eklemenize olanak tanir. Plugin'ler, parsing veya semantik analiz sonrasinda AST'yi inceleyebilir ve raporlayabilir.

## Yerlesik Plugin'ler

Liva iki yerlesik plugin ile gelir:

### naming-convention

Adlandirma kurallarini kontrol eder: tipler (struct, enum, class, protocol) icin PascalCase, fonksiyonlar icin camelCase.

```toml
[plugins]
naming-convention = true
```

### unused-function

Tanimlanmis ama hic cagrilmamis top-level fonksiyonlari tespit eder. `main`, `extern` fonksiyonlar ve metotlar haric tutulur.

```toml
[plugins]
unused-function = true
```

## Ozel Plugin Yazma

### 1. CompilerPlugin Arayuzunu Implemente Edin

```cpp
#include "liva/Plugin/PluginAPI.h"

class BenimPlugin : public liva::CompilerPlugin {
public:
    std::string getName() const override { return "benim-plugin"; }
    std::string getDescription() const override {
        return "Plugin'in ne yaptiginin aciklamasi";
    }

    // Parsing sonrasi, semantik analiz oncesi cagrilir
    bool afterParse(TranslationUnit &tu, DiagnosticsEngine &diag) override {
        // tu.getDeclarations() ile AST'yi incele
        // diag.report(loc, diagId, ...) ile sorunlari raporla
        return true; // false donerse derleme iptal olur
    }

    // Semantik analiz sonrasi, IR uretimi oncesi cagrilir
    bool afterSema(TranslationUnit &tu, DiagnosticsEngine &diag) override {
        // AST artik cozulmus tiplere sahip
        return true;
    }

    // TOML [plugins.benim-plugin] anahtar-deger ciftleri ile cagrilir
    void configure(const std::map<std::string, std::string> &options) override {
        // Plugin'e ozel ayarlari oku
    }
};
```

### 2. Plugin'i Kaydedin

```cpp
#include "liva/Plugin/PluginRegistry.h"

PluginRegistry registry = PluginRegistry::createWithBuiltins();
registry.registerPlugin(std::make_unique<BenimPlugin>());
```

### 3. liva.toml ile Yapilandirin

```toml
[plugins]
benim-plugin = true
naming-convention = false
unused-function = true
```

Plugin'i devre disi birakmak icin `false` yapin.

## Plugin API Referansi

### CompilerPlugin (Temel Sinif)

| Metot | Aciklama |
|-------|----------|
| `getName()` | Plugin'in benzersiz adini dondur |
| `getDescription()` | Insan-okunabilir aciklamayi dondur |
| `afterParse(tu, diag)` | Hook: parsing sonrasi calisir. Iptal icin `false` don |
| `afterSema(tu, diag)` | Hook: sema sonrasi calisir. Iptal icin `false` don |
| `configure(options)` | TOML anahtar-deger yapilandirmasini al |
| `isEnabled()` / `setEnabled(bool)` | Etkin durumunu kontrol et/ayarla |

### PluginRegistry

| Metot | Aciklama |
|-------|----------|
| `registerPlugin(plugin)` | Registry'ye plugin ekle |
| `getPlugin(name)` | Ada gore plugin bul |
| `runAfterParse(tu, diag)` | Tum etkin afterParse hook'larini calistir |
| `runAfterSema(tu, diag)` | Tum etkin afterSema hook'larini calistir |
| `configureFromTOML(doc)` | `[plugins]` bolumunden etkin/devre disi yukle |
| `createWithBuiltins()` | Fabrika: yerlesik plugin'lerle registry olustur |

### Hook Calisma Sirasi

```
Kaynak Kod
    |
    v
  [Parse]
    |
    v
  afterParse() <-- NamingConventionPlugin
    |
    v
  [Sema]
    |
    v
  afterSema()  <-- UnusedFunctionPlugin
    |
    v
  [IRGen]
    |
    v
  [CodeGen]
```

Plugin'ler kayit sirasina gore calisir. Herhangi biri `false` donerse pipeline iptal olur.

## Kullanilabilir AST Dugum Tipleri

Plugin'ler su bildirim tiplerini inceleyebilir:

- `FuncDecl` — fonksiyonlar (`getName()`, `getParams()`, `getBody()`, `isPublic()`, `isAsync()`, `isExtern()`)
- `StructDecl` — struct'lar (`getName()`, `getFields()`, `isPublic()`)
- `EnumDecl` — enum'lar (`getName()`, `getCases()`, `isPublic()`)
- `ClassDecl` — siniflar (`getName()`, `getMethods()`, `getFields()`, `getParentName()`)
- `ProtocolDecl` — protokoller/trait'ler (`getName()`, `getMethods()`)
- `ImplDecl` — impl bloklari (`getTypeName()`, `getMethods()`)
- `VarDecl` — degiskenler (`getName()`, `isMutable()`, `hasInit()`)
- `ImportDecl` — import'lar (`getPath()`)
