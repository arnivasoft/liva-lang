# Faz 6 — Koleksiyon Bağlama Implementasyon Planı

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Faz 5 gözlemlenebilir `Model`'i bir liste değeriyle genişletip bir string listesini ListBox/Dropdown/ComboBox öğe listelerine tek-yönlü bağlamak.

**Architecture:** Faz 5 `LivaModel`'e `listVals` (anahtar→`vector<string>`) eklenir; `LivaBinding.kind=2` liste bağlaması. Üç liste-widget'ı `wxControlWithItems` ortak tabanından `Append`/`Clear` ile doldurulur (`widgetAppendItem`/`widgetClearItems`). `bindList`/`listAdd`/`listClear`/`listCount` FFI'leri + Model metotları. Tek yön (model→widget); seçim ayrı Faz 5 `bindInt`. 4 FFI mevcut extern tiplerini yeniden kullanır.

**Tech Stack:** C++20, LLVM 21 IRGen, wxWidgets 3.3 (vcpkg), CMake/Ninja (`build-clang/`), GoogleTest.

---

## Dokunulacak Dosyalar

- **`stdlib/ui/wx_runtime.h`** — Phase 6 FFI prototipleri (4).
- **`stdlib/ui/wx_runtime.cpp`** — `LivaModel`'e `listVals`; `widgetAppendItem`/`widgetClearItems`; 4 FFI.
- **`src/IR/IRGen.cpp`** — 4 extern (Phase 5 bloğundan sonra; yeni tip yok).
- **`src/IR/IRGenCall.cpp`** — 4 intrinsic (Phase 5 `modelBindInt`'ten sonra).
- **`src/Sema/ModuleLoader.cpp`** — 4 builtin adı.
- **`stdlib/ui/widgets.liva`** — `Model` sınıfına 4 metot.
- **`tests/unit/UICodegenExecTest.cpp`** — emit-IR testleri.
- **`examples/ui_collection_binding.liva`** — (Task 2) örnek.

### Doğrulama stratejisi
- **Derleyici zinciri:** `livac --emit-ir` (link yok); `emitsClean` + `hasRuntimeCall` (void FFI'lerin lower olduğunu assert).
- **Runtime C++:** bu makinede wx KURULU → `cmake --build build-clang` `wx_runtime.cpp`'yi derler (`widgetAppendItem`/`wxControlWithItems` cast hataları yakalanır).
- **Senkron davranışı:** wx-kurulu makinede `.exe` ile teyit (Task 2).

### Ortak komutlar
- Tam derleme: `cmake --build build-clang`
- Tek test: `ctest --test-dir build-clang -R "<TestAdı>" --output-on-failure`
- Tam regresyon: `ctest --test-dir build-clang --output-on-failure`

---

## Task 1: Koleksiyon bağlama çekirdeği

`LivaModel.listVals` + `wxControlWithItems` doldurma yardımcıları + 4 FFI + derleyici bağlama + `Model` metotları.

**Files:**
- Test: `tests/unit/UICodegenExecTest.cpp`
- Modify: `stdlib/ui/wx_runtime.h`, `stdlib/ui/wx_runtime.cpp`, `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/ModuleLoader.cpp`, `stdlib/ui/widgets.liva`

- [ ] **Step 1: Write the failing test**

`tests/unit/UICodegenExecTest.cpp` içinde, son `TEST`'ten sonra (`#endif // LIVA_HAS_LLVM`'den önce) ekle:

```cpp
// ── Phase 6: collection binding ────────────────────────────────────────
TEST(UICodegenExec, ModelListBindCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let root = Panel(win)\n"
        "  let model = Model()\n"
        "  let lb = ListBox(root)\n"
        "  model.bindList(\"items\", lb)\n"
        "  let dd = Dropdown(root, \"\")\n"
        "  model.bindList(\"items\", dd)\n"
        "  let cmb = ComboBox(root, \"\")\n"
        "  model.bindList(\"items\", cmb)\n"
        "  model.listAdd(\"items\", \"a\")\n"
        "  model.listAdd(\"items\", \"b\")\n"
        "  println(model.listCount(\"items\"))\n"
        "  model.listClear(\"items\")\n"
        "}\n",
        "model_list_bind");
    ASSERT_TRUE(emitsClean(ir));
    EXPECT_TRUE(hasRuntimeCall(ir, "liva_ui_model_bind_list"))
        << "bindList must lower to liva_ui_model_bind_list";
    EXPECT_TRUE(hasRuntimeCall(ir, "liva_ui_model_list_add"))
        << "listAdd must lower to liva_ui_model_list_add";
    EXPECT_TRUE(hasRuntimeCall(ir, "liva_ui_model_list_clear"))
        << "listClear must lower to liva_ui_model_list_clear";
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.ModelListBindCompiles" --output-on-failure`
Expected: FAIL — `no IR emitted` (`bindList`/`listAdd` builtin'leri yok).

- [ ] **Step 3a: Add runtime FFI prototypes**

`stdlib/ui/wx_runtime.h` içinde, Phase 5 bölümünden sonra (son `liva_ui_model_bind_int` satırından sonra, `/* ── List / Tab operations ── */` yorumundan önce) ekle:

```c
/* ── Phase 6: collection binding ────────────────────────────────────── */
void     liva_ui_model_bind_list(int32_t model, const char *key, int32_t widget);
void     liva_ui_model_list_add(int32_t model, const char *key, const char *item);
void     liva_ui_model_list_clear(int32_t model, const char *key);
int32_t  liva_ui_model_list_count(int32_t model, const char *key);
```

- [ ] **Step 3b: Extend LivaModel struct**

`stdlib/ui/wx_runtime.cpp` içinde, Phase 5 `LivaModel` struct tanımında, `intVals` satırından sonra `listVals` alanını ekle:

```cpp
struct LivaModel {
    std::unordered_map<std::string, std::string>              textVals;
    std::unordered_map<std::string, int32_t>                  intVals;
    std::unordered_map<std::string, std::vector<std::string>> listVals;   // Phase 6
    std::unordered_map<std::string, std::vector<LivaBinding>> bindings;
};
```

(Mevcut `LivaModel`'i bul ve YALNIZ `listVals` satırını ekle; diğer alanlar/sıra korunur. `LivaBinding.kind` yorumuna `2=list` eklenebilir ama struct değişmez.)

- [ ] **Step 3c: Add helpers + 4 FFIs**

`stdlib/ui/wx_runtime.cpp` içinde, dosyanın SONUNA (Phase 5 `liva_ui_model_bind_int` fonksiyonundan sonra) ekle:

```cpp
/* ═══════════════════════════════════════════════════════════════════
   Phase 6: collection binding (model list -> ListBox/Dropdown/ComboBox)
   ═══════════════════════════════════════════════════════════════════ */

// ListBox/wxChoice/wxComboBox hepsi wxControlWithItems'tan turer.
static void widgetAppendItem(wxWindow *w, const wxString &s) {
    if (auto *ci = dynamic_cast<wxControlWithItems *>(w)) ci->Append(s);
}
static void widgetClearItems(wxWindow *w) {
    if (auto *ci = dynamic_cast<wxControlWithItems *>(w)) ci->Clear();
}

void liva_ui_model_bind_list(int32_t model, const char *key, int32_t widget) {
    auto it = g_models.find(model);
    if (it == g_models.end()) return;
    auto *w = getHandle<wxWindow>(widget);
    if (!w) return;
    std::string k = key ? key : "";
    LivaModel &M = it->second;
    M.bindings[k].push_back({widget, 2});
    widgetClearItems(w);
    auto vit = M.listVals.find(k);
    if (vit != M.listVals.end())
        for (auto &s : vit->second)
            widgetAppendItem(w, wxString::FromUTF8(s));
}

void liva_ui_model_list_add(int32_t model, const char *key, const char *item) {
    auto it = g_models.find(model);
    if (it == g_models.end()) return;
    std::string k = key ? key : "";
    std::string v = item ? item : "";
    LivaModel &M = it->second;
    M.listVals[k].push_back(v);
    auto bit = M.bindings.find(k);
    if (bit == M.bindings.end()) return;
    for (auto &b : bit->second)
        if (b.kind == 2)
            if (auto *w = getHandle<wxWindow>(b.widget))
                widgetAppendItem(w, wxString::FromUTF8(v));
}

void liva_ui_model_list_clear(int32_t model, const char *key) {
    auto it = g_models.find(model);
    if (it == g_models.end()) return;
    std::string k = key ? key : "";
    LivaModel &M = it->second;
    M.listVals[k].clear();
    auto bit = M.bindings.find(k);
    if (bit == M.bindings.end()) return;
    for (auto &b : bit->second)
        if (b.kind == 2)
            if (auto *w = getHandle<wxWindow>(b.widget))
                widgetClearItems(w);
}

int32_t liva_ui_model_list_count(int32_t model, const char *key) {
    auto it = g_models.find(model);
    if (it == g_models.end()) return 0;
    auto vit = it->second.listVals.find(key ? key : "");
    return (vit != it->second.listVals.end())
               ? static_cast<int32_t>(vit->second.size()) : 0;
}
```

> Not: `wxControlWithItems` `<wx/ctrlsub.h>`'dedir; `<wx/listbox.h>`/`<wx/choice.h>`/`<wx/combobox.h>` (zaten dahil) onu çeker. Derlemede "incomplete type / not declared" hatası çıkarsa dosya başındaki include bloğuna `#include <wx/ctrlsub.h>` ekle ve raporla.

- [ ] **Step 3d: Add IRGen externs**

`src/IR/IRGen.cpp` içinde, Phase 5 bloğunun sonunda (`liva_ui_model_bind_int` extern'inden sonra), `declareCoroutineIntrinsics();`'den ÖNCE ekle:

```cpp
    // ── Phase 6: collection binding ──────────────────────────────────
    module_->getOrInsertFunction("liva_ui_model_bind_list", uiI32StrI32VoidTy);
    module_->getOrInsertFunction("liva_ui_model_list_add", uiI32StrStrVoidTy);
    module_->getOrInsertFunction("liva_ui_model_list_clear", uiI32StrVoidTy);
    module_->getOrInsertFunction("liva_ui_model_list_count", uiI32StrRetI32Ty);
```

> Hepsi kapsamda: `uiI32StrI32VoidTy` ((i32,ptr,i32)→void, Phase 2), `uiI32StrStrVoidTy` ((i32,ptr,ptr)→void, Phase 5), `uiI32StrVoidTy` ((i32,ptr)→void, Phase 2), `uiI32StrRetI32Ty` ((i32,ptr)→i32, Phase 2). Biri kapsamda değilse doğru eşdeğeri bul veya yeni yerel tanımla ve raporla.

- [ ] **Step 3e: Add IRGenCall intrinsics**

`src/IR/IRGenCall.cpp` içinde, Phase 5 bloğunun sonunda (`modelBindInt` intrinsic'inden sonra), `// Closure-taking free-function forms` yorumundan ÖNCE ekle:

```cpp
    // ── Phase 6: collection binding ──────────────────────────────────
    // modelBindList(model, key, widget) -> void
    if (funcName == "modelBindList" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *w = visit(node->getArgs()[2].get());
        if (!m || !k || !w) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_bind_list"), {m, k, w});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelListAdd(model, key, item) -> void
    if (funcName == "modelListAdd" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *v = visit(node->getArgs()[2].get());
        if (!m || !k || !v) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_list_add"), {m, k, v});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelListClear(model, key) -> void
    if (funcName == "modelListClear" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        if (!m || !k) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_list_clear"), {m, k});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelListCount(model, key) -> i32
    if (funcName == "modelListCount" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        if (!m || !k) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_model_list_count"), {m, k}, "ui.mlcount");
    }
```

- [ ] **Step 3f: Register builtin names**

`src/Sema/ModuleLoader.cpp` içinde, Phase 5 satırını (`"modelSetInt", "modelGetInt", "modelBindInt"});`) değiştir:

```cpp
         "modelSetInt", "modelGetInt", "modelBindInt",
         // Phase 6: collection binding
         "modelBindList", "modelListAdd", "modelListClear", "modelListCount"});
```

- [ ] **Step 3g: Add Model collection methods**

`stdlib/ui/widgets.liva` içinde, `Model` sınıfındaki `bindInt` metodundan sonra ekle:

```liva
    pub func bindList(key: string, w: Control) { modelBindList(self.handle, key, w.handle) }
    pub func listAdd(key: string, item: string) { modelListAdd(self.handle, key, item) }
    pub func listClear(key: string) { modelListClear(self.handle, key) }
    pub func listCount(key: string) -> i32 { return modelListCount(self.handle, key) }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.ModelListBindCompiles" --output-on-failure`
Expected: PASS. Bu makinede wx kurulu → tam `cmake --build build-clang` `wx_runtime.cpp`'yi de derler; `wxControlWithItems`/`Append`/`Clear` çözülmeli. Bir wx sembolü eksikse doğrusunu bul (örn. `<wx/ctrlsub.h>` include) ve sapmayı raporla.

- [ ] **Step 5: Commit**

```bash
git add stdlib/ui/wx_runtime.h stdlib/ui/wx_runtime.cpp src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/ModuleLoader.cpp stdlib/ui/widgets.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase6: collection binding (Model list -> ListBox/Dropdown/ComboBox)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Örnek + regresyon + GUI

Koleksiyon bağlamayı tek pencerede gösteren örnek; tam regresyon; manuel GUI teyidi. (Birleşik test Task 1'in testi tüm yüzeyi kapsadığından ayrı birleşik test gerekmez; bunun yerine örneğin emit-IR'ı doğrulanır.)

**Files:**
- Create: `examples/ui_collection_binding.liva`

- [ ] **Step 1: Create the example file**

`examples/ui_collection_binding.liva` oluştur (ASCII-folded Türkçe; ş/ç/ğ/ü/ö/ı KULLANMA; em-dash — serbest):

```liva
// Faz 6 — Koleksiyon baglama (Model liste -> ListBox/Dropdown).
// Build: livac examples/ui_collection_binding.liva -o collection_binding
//
// Model bir string listesi tutar; bindList ayni listeyi birden cok liste-
// widget'ina baglar (tek yon: model -> widget). listAdd/listClear ile liste
// degisince bagli widget'lar aninda guncellenir. Secili indeks ayri bir
// skalerdir (bindInt, Faz 5).
import ui::widgets

func main() {
    appInit()
    let win = Window(480, 360, "Liva — Koleksiyon Baglama")
    let root = Panel(win)

    let model = Model()

    let lbLbl = Label(root, "Dosyalar:")
    lbLbl.setBounds(20, 16, 120, 24)
    let lb = ListBox(root)
    lb.setBounds(20, 44, 200, 220)
    model.bindList("dosyalar", lb)            // tek yon: liste -> ListBox

    let ddLbl = Label(root, "Ayni liste:")
    ddLbl.setBounds(240, 16, 120, 24)
    let dd = Dropdown(root, "")
    dd.setBounds(240, 44, 200, 28)
    model.bindList("dosyalar", dd)            // ayni liste -> Dropdown

    // Baslangic ogeleri (her iki widget'a yansir)
    model.listAdd("dosyalar", "main.liva")
    model.listAdd("dosyalar", "ui.liva")
    model.listAdd("dosyalar", "README.md")

    // Secim (iki yon, Faz 5)
    model.bindInt("secili", lb)
    lb.onSelect(|_h: i32| { println(model.getInt("secili")) })

    let add = Button(root, "Ekle")
    add.setBounds(240, 90, 95, 30)
    add.onClick(|_h: i32| { model.listAdd("dosyalar", "yeni.liva") })

    let clear = Button(root, "Temizle")
    clear.setBounds(345, 90, 95, 30)
    clear.onClick(|_h: i32| { model.listClear("dosyalar") })

    let countLbl = Label(root, "")
    countLbl.setBounds(240, 130, 200, 24)

    win.show()
    appRun()
}
```

Oluşturduktan sonra örneğin temiz IR ürettiğini doğrula:

Run: `build-clang/livac.exe --emit-ir -o build-clang/_ex_coll.ll examples/ui_collection_binding.liva`
`build-clang/_ex_coll.ll` içinde `define` olduğunu Grep ile teyit et, sonra `_ex_coll.ll`'i sil. livac hata verirse örneği gerçek API'ye göre düzelt (`stdlib/ui/widgets.liva`: Model.bindList/listAdd/listClear/listCount/bindInt/getInt; ListBox(parent), Dropdown(parent, choices), Control.setBounds/onClick/onSelect; Label/Button). ASCII-folded tut (ş/ç/ğ/ü/ö/ı yok); em-dash — serbest.

- [ ] **Step 2: Run the full regression suite**

Run: `cmake --build build-clang && ctest --test-dir build-clang --output-on-failure`
Expected: TÜM testler PASS (mevcut 2313 + Task 1'de eklenen ModelListBindCompiles = 2314). Tam pass/fail sayısını raporla.

- [ ] **Step 3: GUI doğrulama (wx-kurulu makinede — manuel)**

> Kullanıcının wx-kurulu ortamında çalıştırılır. Otomatik test edilemez (headless senkron).

```bash
build-clang/livac.exe examples/ui_collection_binding.liva -o examples/ui_collection_binding.exe
examples/ui_collection_binding.exe
```

Doğrula:
- Açılışta hem ListBox hem Dropdown 3 öğe gösterir (main.liva/ui.liva/README.md) — aynı liste iki widget'a.
- "Ekle" → her iki widget'a "yeni.liva" eklenir (canlı).
- "Temizle" → her iki widget boşalır.
- ListBox'tan öğe seç → seçili indeks konsola yazılır (bindInt + onSelect).
- Crash/donma yok.

- [ ] **Step 4: Commit**

```bash
git add examples/ui_collection_binding.liva
git commit -m "ui-phase6: collection-binding example

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Tamamlama

Task 1 + örnek tamamlandığında `superpowers:finishing-a-development-branch` ile entegrasyon (master'a merge / PR) seçeneklerini sun. Branch: `feat/ui-vcl-phase6-collection-binding`.
