# Faz 5 — Data Binding Implementasyon Planı

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `ui::widgets`'e hafif iki-yönlü veri bağlama eklemek — runtime'da yaşayan gözlemlenebilir `Model` (anahtar→değer, tipli text+int) ↔ widget'lar.

**Architecture:** Faz 1-4'ün 3 katmanı (Liva sınıfı → IRGenCall intrinsic → wx_runtime FFI). Yeni: runtime'da `g_models` (model handle → {textVals, intVals, bindings}); `bind*` widget'ın değişim olayını hookler (mevcut `wxEVT_TEXT`/`wxEVT_SLIDER`/... dispatch'i) ve değişimde model+diğer widget'ları günceller; `set*` model+bağlı widget'ları günceller. `g_modelUpdating` bayrağı geri-besleme döngüsünü önler. Liva `Model` ince handle-sarmalayıcı (Menu deseni).

**Tech Stack:** C++20, LLVM 21 IRGen, wxWidgets 3.3 (vcpkg), CMake/Ninja (`build-clang/`), GoogleTest.

---

## Dokunulacak Dosyalar

- **`stdlib/ui/wx_runtime.h`** — Phase 5 FFI prototipleri (7 adet).
- **`stdlib/ui/wx_runtime.cpp`** — `LivaBinding`/`LivaModel`/`g_models`/`g_modelUpdating`; `propagateText`/`propagateInt`; 7 FFI.
- **`src/IR/IRGen.cpp`** — 7 FFI için LLVM extern (Phase 4 `set_anchors`'dan sonra; 2 yeni yerel tip).
- **`src/IR/IRGenCall.cpp`** — 7 intrinsic eşleme (Phase 4 `setAnchors`'dan sonra).
- **`src/Sema/ModuleLoader.cpp`** — 7 builtin adı.
- **`stdlib/ui/widgets.liva`** — `pub class Model` (Phase 5 bölümü, dosya sonu).
- **`tests/unit/UICodegenExecTest.cpp`** — emit-IR testleri (+ `hasRuntimeCall` assert'leri).
- **`examples/ui_data_binding.liva`** — (Task 3) örnek.

### Doğrulama stratejisi
- **Derleyici zinciri:** `livac --emit-ir` (link yok). `Model`/`modelCreate` eksikse Sema hata → IR yok → test düşer; eklenince geçer. emit-IR testleri ayrıca `hasRuntimeCall` ile FFI'lerin gerçekten lower olduğunu assert eder (boş-geçişi önler).
- **Runtime C++:** Bu makinede wxWidgets KURULU → `cmake --build build-clang` `wx_runtime.cpp`'yi derler → model/bağlama/olay-hook C++ hataları yakalanır.
- **Senkron davranışı:** Yalnız wx-kurulu makinede `.exe` ile teyit (headless senkronu test edemez — Task 3).

### Ortak komutlar
- Tam derleme: `cmake --build build-clang`
- Tek test: `ctest --test-dir build-clang -R "<TestAdı>" --output-on-failure`
- Tam regresyon: `ctest --test-dir build-clang --output-on-failure`

---

## Task 1: Model + text bağlama

Runtime gözlemlenebilir model (struct + g_models + feedback guard) + text değerleri (setText/getText) + iki-yönlü text bağlama (bindText: olay-hook + propagasyon). `Model` Liva sınıfı (yalnız text metotları). int Task 2'de eklenir.

**Files:**
- Test: `tests/unit/UICodegenExecTest.cpp`
- Modify: `stdlib/ui/wx_runtime.h`, `stdlib/ui/wx_runtime.cpp`, `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/ModuleLoader.cpp`, `stdlib/ui/widgets.liva`

- [ ] **Step 1: Write the failing test**

`tests/unit/UICodegenExecTest.cpp` içinde, son `TEST`'ten sonra (`#endif // LIVA_HAS_LLVM`'den önce) ekle:

```cpp
// ── Phase 5: data binding ──────────────────────────────────────────────
TEST(UICodegenExec, ModelTextBindCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let root = Panel(win)\n"
        "  let model = Model()\n"
        "  model.setText(\"ad\", \"Ali\")\n"
        "  println(model.getText(\"ad\"))\n"
        "  let input = TextInput(root, \"\")\n"
        "  model.bindText(\"ad\", input)\n"
        "  let echo = Label(root, \"\")\n"
        "  model.bindText(\"ad\", echo)\n"
        "}\n",
        "model_text_bind");
    ASSERT_TRUE(emitsClean(ir));
    // hasRuntimeCall (Faz 4 helper) matches `call void @<fn>(`, so it only
    // works for VOID-returning FFIs. model_create returns i32 (so it can't be
    // asserted this way) — assert the two void-returning calls instead;
    // model_create is covered indirectly (the program would not emit clean IR
    // without it).
    EXPECT_TRUE(hasRuntimeCall(ir, "liva_ui_model_set_text"))
        << "setText must lower to liva_ui_model_set_text";
    EXPECT_TRUE(hasRuntimeCall(ir, "liva_ui_model_bind_text"))
        << "bindText must lower to liva_ui_model_bind_text";
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.ModelTextBindCompiles" --output-on-failure`
Expected: FAIL — `no IR emitted` (`Model` sınıfı / `modelCreate` builtin'i yok).

- [ ] **Step 3a: Add runtime FFI prototypes**

`stdlib/ui/wx_runtime.h` içinde, Phase 4 bölümünden sonra (son `liva_ui_set_anchors` satırından sonra, `/* ── List / Tab operations ── */` yorumundan önce) ekle:

```c
/* ── Phase 5: data binding ──────────────────────────────────────────── */
int32_t      liva_ui_model_create(void);
void         liva_ui_model_set_text(int32_t model, const char *key, const char *val);
const char  *liva_ui_model_get_text(int32_t model, const char *key);
void         liva_ui_model_set_int(int32_t model, const char *key, int32_t val);
int32_t      liva_ui_model_get_int(int32_t model, const char *key);
void         liva_ui_model_bind_text(int32_t model, const char *key, int32_t widget);
void         liva_ui_model_bind_int(int32_t model, const char *key, int32_t widget);
```

(Hepsi şimdi bildirilir; `set_int`/`get_int`/`bind_int` Task 2'de implemente edilir — bildirilmiş-ama-tanımsız prototip zararsızdır.)

- [ ] **Step 3b: Add runtime model + text FFIs**

`stdlib/ui/wx_runtime.cpp` içinde, dosyanın SONUNA (Phase 4 layout fonksiyonlarından sonra) ekle:

```cpp
/* ═══════════════════════════════════════════════════════════════════
   Phase 5: data binding (observable Model)
   ═══════════════════════════════════════════════════════════════════ */

struct LivaBinding { int32_t widget; int kind; };   // kind: 0=text, 1=int
struct LivaModel {
    std::unordered_map<std::string, std::string> textVals;
    std::unordered_map<std::string, int32_t>     intVals;
    std::unordered_map<std::string, std::vector<LivaBinding>> bindings;
};
static std::unordered_map<int32_t, LivaModel> g_models;
static bool g_modelUpdating = false;   // geri-besleme (feedback-loop) koruması

// `key`'e bağlı text widget'lara `val` yaz (kaynak handle atlanır).
static void propagateText(LivaModel &M, const std::string &key,
                          const char *val, int32_t source) {
    auto it = M.bindings.find(key);
    if (it == M.bindings.end()) return;
    g_modelUpdating = true;
    for (auto &b : it->second)
        if (b.kind == 0 && b.widget != source)
            liva_ui_set_text(b.widget, val);
    g_modelUpdating = false;
}

int32_t liva_ui_model_create(void) {
    int32_t h = g_nextHandle++;   // benzersiz; g_handles'a EKLENMEZ (g_treeNodes deseni)
    g_models[h];                   // default-construct
    return h;
}

void liva_ui_model_set_text(int32_t model, const char *key, const char *val) {
    auto it = g_models.find(model);
    if (it == g_models.end()) return;
    std::string k = key ? key : "";
    std::string v = val ? val : "";
    it->second.textVals[k] = v;
    propagateText(it->second, k, v.c_str(), -1);   // kaynak yok → hepsine
}

const char *liva_ui_model_get_text(int32_t model, const char *key) {
    auto it = g_models.find(model);
    if (it == g_models.end()) return "";
    auto vit = it->second.textVals.find(key ? key : "");
    if (vit == it->second.textVals.end()) return "";
    return returnTempStr(wxString::FromUTF8(vit->second));
}

void liva_ui_model_bind_text(int32_t model, const char *key, int32_t widget) {
    auto it = g_models.find(model);
    if (it == g_models.end()) return;
    auto *w = getHandle<wxWindow>(widget);
    if (!w) return;
    std::string k = key ? key : "";
    LivaModel &M = it->second;
    M.bindings[k].push_back({widget, 0});
    // mevcut model değerini widget'a it
    auto vit = M.textVals.find(k);
    if (vit != M.textVals.end())
        liva_ui_set_text(widget, vit->second.c_str());
    // widget değişim olayını hookle → model + diğerlerini güncelle
    if (dynamic_cast<wxTextCtrl *>(w) || dynamic_cast<wxComboBox *>(w)) {
        int32_t mh = model;
        w->Bind(wxEVT_TEXT, [mh, k, widget](wxCommandEvent &) {
            if (g_modelUpdating) return;
            auto mit = g_models.find(mh);
            if (mit == g_models.end()) return;
            const char *v = liva_ui_get_text(widget);
            mit->second.textVals[k] = v ? v : "";
            propagateText(mit->second, k, mit->second.textVals[k].c_str(), widget);
        });
    }
    // Label/StaticText wxEVT_TEXT yaymaz → tek yön (yalnız yukarıdaki it).
}
```

> Not: `liva_ui_set_text`/`get_text` ve `returnTempStr` bu dosyada DAHA ÖNCE tanımlı (satır ~448-470), Phase 5 sonda olduğundan erişilebilir. `<unordered_map>`/`<string>`/`<vector>` zaten dahil (Faz 4 + başlangıç).

- [ ] **Step 3c: Add IRGen externs**

`src/IR/IRGen.cpp` içinde, Phase 4 bloğunun sonunda (`liva_ui_set_anchors` extern'inden sonra), `declareCoroutineIntrinsics();`'den ÖNCE ekle:

```cpp
    // ── Phase 5: data binding ────────────────────────────────────────
    auto *uiI32StrStrVoidTy =
        llvm::FunctionType::get(voidTy, {i32Ty, i8PtrTy, i8PtrTy}, false);
    auto *uiI32StrRetStrTy =
        llvm::FunctionType::get(i8PtrTy, {i32Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_ui_model_create", uiRetI32Ty);
    module_->getOrInsertFunction("liva_ui_model_set_text", uiI32StrStrVoidTy);
    module_->getOrInsertFunction("liva_ui_model_get_text", uiI32StrRetStrTy);
    module_->getOrInsertFunction("liva_ui_model_bind_text", uiI32StrI32VoidTy);
```

> `uiRetI32Ty` (()→i32), `uiI32StrI32VoidTy` ((i32,ptr,i32)→void), `i32Ty`, `i8PtrTy`, `voidTy` Phase 2/erken bloklardan kapsamda. `uiI32StrStrVoidTy`/`uiI32StrRetStrTy` yeni yereller (Task 2 `set_int`/`get_int`/`bind_int` için olanları Task 2 ekler).

- [ ] **Step 3d: Add IRGenCall intrinsics**

`src/IR/IRGenCall.cpp` içinde, Phase 4 bloğunun sonunda (`setAnchors` intrinsic'inden sonra), `// Closure-taking free-function forms` yorumundan ÖNCE ekle:

```cpp
    // ── Phase 5: data binding ────────────────────────────────────────
    // modelCreate() -> i32
    if (funcName == "modelCreate" && node->getArgs().empty()) {
        return builder_->CreateCall(getOrPanic("liva_ui_model_create"), {}, "ui.model");
    }
    // modelSetText(model, key, val) -> void
    if (funcName == "modelSetText" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *v = visit(node->getArgs()[2].get());
        if (!m || !k || !v) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_set_text"), {m, k, v});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelGetText(model, key) -> string
    if (funcName == "modelGetText" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        if (!m || !k) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_model_get_text"), {m, k}, "ui.mget");
    }
    // modelBindText(model, key, widget) -> void
    if (funcName == "modelBindText" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *w = visit(node->getArgs()[2].get());
        if (!m || !k || !w) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_bind_text"), {m, k, w});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
```

- [ ] **Step 3e: Register builtin names**

`src/Sema/ModuleLoader.cpp` içinde, Phase 4 satırını (`"setAlign", "setAnchors"});`) değiştir:

```cpp
         "setAlign", "setAnchors",
         // Phase 5: data binding
         "modelCreate", "modelSetText", "modelGetText", "modelBindText"});
```

- [ ] **Step 3f: Add Model class (text methods)**

`stdlib/ui/widgets.liva` dosyasının SONUNA ekle:

```liva
// ═══ Phase 5: data binding ══════════════════════════════════════

// ── Model (gözlemlenebilir anahtar-değer; Control değil, handle-sarmalayıcı) ──
pub class Model {
    var handle: i32
    init() { self.handle = modelCreate() }
    pub func setText(key: string, val: string) { modelSetText(self.handle, key, val) }
    pub func getText(key: string) -> string { return modelGetText(self.handle, key) }
    pub func bindText(key: string, w: Control) { modelBindText(self.handle, key, w.handle) }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.ModelTextBindCompiles" --output-on-failure`
Expected: PASS. Bu makinede wx kurulu → tam `cmake --build build-clang` `wx_runtime.cpp`'yi de derler; `g_models`/`propagateText`/`wxEVT_TEXT` bağlama çözülmeli. Bir wx sembolü eksikse doğrusunu bul ve sapmayı raporla.

- [ ] **Step 5: Commit**

```bash
git add stdlib/ui/wx_runtime.h stdlib/ui/wx_runtime.cpp src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/ModuleLoader.cpp stdlib/ui/widgets.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase5: observable Model + two-way text binding (bindText)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: int bağlama

`Model`'e int değerleri (setInt/getInt) + sayısal widget bağlama (bindInt: tip-bazlı olay-hook + propagasyon).

**Files:**
- Test: `tests/unit/UICodegenExecTest.cpp`
- Modify: `stdlib/ui/wx_runtime.cpp`, `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/ModuleLoader.cpp`, `stdlib/ui/widgets.liva`

- [ ] **Step 1: Write the failing test**

`tests/unit/UICodegenExecTest.cpp` içinde, `ModelTextBindCompiles`'dan sonra ekle:

```cpp
TEST(UICodegenExec, ModelIntBindCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let root = Panel(win)\n"
        "  let model = Model()\n"
        "  model.setInt(\"yas\", 30)\n"
        "  println(model.getInt(\"yas\"))\n"
        "  let spin = SpinCtrl(root, 0, 100, 0)\n"
        "  model.bindInt(\"yas\", spin)\n"
        "}\n",
        "model_int_bind");
    ASSERT_TRUE(emitsClean(ir));
    EXPECT_TRUE(hasRuntimeCall(ir, "liva_ui_model_set_int"))
        << "setInt must lower to liva_ui_model_set_int";
    EXPECT_TRUE(hasRuntimeCall(ir, "liva_ui_model_bind_int"))
        << "bindInt must lower to liva_ui_model_bind_int";
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.ModelIntBindCompiles" --output-on-failure`
Expected: FAIL — `no IR emitted` (`setInt`/`bindInt` builtin'leri yok).

- [ ] **Step 3a: Add int + bind_int runtime FFIs**

`stdlib/ui/wx_runtime.cpp` Phase 5 bölümünde, `liva_ui_model_bind_text` fonksiyonundan sonra ekle (prototipler Task 1'de eklendi):

```cpp
// `key`'e bağlı int widget'lara `val` yaz (kaynak handle atlanır).
static void propagateInt(LivaModel &M, const std::string &key,
                         int32_t val, int32_t source) {
    auto it = M.bindings.find(key);
    if (it == M.bindings.end()) return;
    g_modelUpdating = true;
    for (auto &b : it->second)
        if (b.kind == 1 && b.widget != source)
            liva_ui_set_value(b.widget, val);
    g_modelUpdating = false;
}

void liva_ui_model_set_int(int32_t model, const char *key, int32_t val) {
    auto it = g_models.find(model);
    if (it == g_models.end()) return;
    std::string k = key ? key : "";
    it->second.intVals[k] = val;
    propagateInt(it->second, k, val, -1);
}

int32_t liva_ui_model_get_int(int32_t model, const char *key) {
    auto it = g_models.find(model);
    if (it == g_models.end()) return 0;
    auto vit = it->second.intVals.find(key ? key : "");
    return (vit != it->second.intVals.end()) ? vit->second : 0;
}

void liva_ui_model_bind_int(int32_t model, const char *key, int32_t widget) {
    auto it = g_models.find(model);
    if (it == g_models.end()) return;
    auto *w = getHandle<wxWindow>(widget);
    if (!w) return;
    std::string k = key ? key : "";
    LivaModel &M = it->second;
    M.bindings[k].push_back({widget, 1});
    auto vit = M.intVals.find(k);
    if (vit != M.intVals.end())
        liva_ui_set_value(widget, vit->second);
    // sayısal değişim olayını tip-bazlı hookle
    int32_t mh = model;
    auto onChange = [mh, k, widget](wxCommandEvent &) {
        if (g_modelUpdating) return;
        auto mit = g_models.find(mh);
        if (mit == g_models.end()) return;
        int32_t v = liva_ui_get_value(widget);
        mit->second.intVals[k] = v;
        propagateInt(mit->second, k, v, widget);
    };
    if (dynamic_cast<wxComboBox *>(w))   w->Bind(wxEVT_COMBOBOX, onChange);
    else if (dynamic_cast<wxChoice *>(w)) w->Bind(wxEVT_CHOICE, onChange);
    else if (dynamic_cast<wxListBox *>(w)) w->Bind(wxEVT_LISTBOX, onChange);
    else if (dynamic_cast<wxCheckBox *>(w)) w->Bind(wxEVT_CHECKBOX, onChange);
    else if (dynamic_cast<wxSpinCtrl *>(w)) w->Bind(wxEVT_SPINCTRL, onChange);
    else if (dynamic_cast<wxSlider *>(w)) w->Bind(wxEVT_SLIDER, onChange);
}
```

> Not: `wxComboBox` MSW'de `wxChoice`'tan türer (Faz 3 dersi) → `wxComboBox` kontrolü `wxChoice`'tan ÖNCE. `liva_ui_set_value`/`get_value` dosyada daha önce tanımlı.

- [ ] **Step 3b: Add IRGen externs**

`src/IR/IRGen.cpp` Phase 5 bloğuna (Task 1 extern'lerinden sonra) ekle:

```cpp
    module_->getOrInsertFunction("liva_ui_model_set_int", uiI32StrI32VoidTy);
    module_->getOrInsertFunction("liva_ui_model_get_int", uiI32StrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_model_bind_int", uiI32StrI32VoidTy);
```

> İkisi de Phase 2'den kapsamda, yeni yerel tip gerekmez: `uiI32StrI32VoidTy` ((i32,ptr,i32)→void) `set_int`/`bind_int` için; `uiI32StrRetI32Ty` ((i32,ptr)→i32) `get_int` için. Bu noktada her ikisi de tanımlı (Phase 2 bloğu, aynı fonksiyon gövdesi). Eğer derleyici `uiI32StrRetI32Ty`'i bulamazsa (beklenmiyor), yeni bir yerel `(i32,i8Ptr)->i32` tipi tanımla ve raporla.

- [ ] **Step 3c: Add IRGenCall intrinsics**

`src/IR/IRGenCall.cpp` Phase 5 bloğuna (modelBindText'ten sonra) ekle:

```cpp
    // modelSetInt(model, key, val) -> void
    if (funcName == "modelSetInt" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *v = visit(node->getArgs()[2].get());
        if (!m || !k || !v) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_set_int"), {m, k, v});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // modelGetInt(model, key) -> i32
    if (funcName == "modelGetInt" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        if (!m || !k) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_model_get_int"), {m, k}, "ui.mgeti");
    }
    // modelBindInt(model, key, widget) -> void
    if (funcName == "modelBindInt" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *k = visit(node->getArgs()[1].get());
        auto *w = visit(node->getArgs()[2].get());
        if (!m || !k || !w) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_model_bind_int"), {m, k, w});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
```

- [ ] **Step 3d: Register builtin names**

`src/Sema/ModuleLoader.cpp` Phase 5 satırını güncelle:

```cpp
         "setAlign", "setAnchors",
         // Phase 5: data binding
         "modelCreate", "modelSetText", "modelGetText", "modelBindText",
         "modelSetInt", "modelGetInt", "modelBindInt"});
```

- [ ] **Step 3e: Add Model int methods**

`stdlib/ui/widgets.liva` içinde, `Model` sınıfındaki `bindText` metodundan sonra ekle:

```liva
    pub func setInt(key: string, val: i32) { modelSetInt(self.handle, key, val) }
    pub func getInt(key: string) -> i32 { return modelGetInt(self.handle, key) }
    pub func bindInt(key: string, w: Control) { modelBindInt(self.handle, key, w.handle) }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.ModelIntBindCompiles" --output-on-failure`
Expected: PASS. Tam `cmake --build build-clang` `wx_runtime.cpp`'yi derler (bind_int dispatch C++ hataları burada görünür). Ayrıca Task 1 testinin hâlâ geçtiğini doğrula: `ctest --test-dir build-clang -R "UICodegenExec.Model" --output-on-failure`.

- [ ] **Step 5: Commit**

```bash
git add stdlib/ui/wx_runtime.cpp src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/ModuleLoader.cpp stdlib/ui/widgets.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase5: int values + numeric widget binding (setInt/getInt/bindInt)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Örnek + birleşik test + regresyon + GUI

Data binding'i tek pencerede gösteren örnek; birleşik emit-IR testi; tam regresyon; manuel GUI teyidi.

**Files:**
- Create: `examples/ui_data_binding.liva`
- Test: `tests/unit/UICodegenExecTest.cpp`

- [ ] **Step 1: Write the combined test**

`tests/unit/UICodegenExecTest.cpp` içinde, `ModelIntBindCompiles`'dan sonra ekle:

```cpp
TEST(UICodegenExec, DataBindingExampleCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(420, 260, \"Veri Baglama\")\n"
        "  let root = Panel(win)\n"
        "  let model = Model()\n"
        "  model.setText(\"ad\", \"Ali\")\n"
        "  let input = TextInput(root, \"\")\n"
        "  model.bindText(\"ad\", input)\n"
        "  let echo = Label(root, \"\")\n"
        "  model.bindText(\"ad\", echo)\n"
        "  let spin = SpinCtrl(root, 0, 120, 0)\n"
        "  model.setInt(\"yas\", 30)\n"
        "  model.bindInt(\"yas\", spin)\n"
        "  let reset = Button(root, \"Sifirla\")\n"
        "  reset.onClick(|_h: i32| { model.setText(\"ad\", \"Varsayilan\") })\n"
        "  win.show()\n"
        "  appRun()\n"
        "}\n",
        "data_binding_example");
    EXPECT_TRUE(emitsClean(ir));
}
```

- [ ] **Step 2: Run the combined test**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.DataBindingExampleCompiles" --output-on-failure`
Expected: PASS (Task 1-2 entegre; text + int bağlama bir arada derlenir).

- [ ] **Step 3: Create the example file**

`examples/ui_data_binding.liva` oluştur (ASCII-folded Türkçe; ş/ç/ğ/ü/ö/ı KULLANMA; em-dash — serbest):

```liva
// Faz 5 — Iki yonlu veri baglama (gozlemlenebilir Model).
// Build: livac examples/ui_data_binding.liva -o data_binding
//
// Model anahtar-deger tutar; bindText/bindInt widget'lari iki yonlu baglar.
// input'a yazinca model["ad"] + echo Label canli guncellenir. Spin'i oynatinca
// model["yas"] guncellenir. Sifirla butonu model'i degistirir -> widget'lar
// aninda yansir (model -> widget yonu).
import ui::widgets

func main() {
    appInit()
    let win = Window(440, 280, "Liva — Veri Baglama")
    let root = Panel(win)

    let model = Model()
    model.setText("ad", "Ali")
    model.setInt("yas", 30)

    let adLbl = Label(root, "Ad:")
    adLbl.setBounds(20, 24, 80, 24)
    let input = TextInput(root, "")
    input.setBounds(110, 20, 200, 28)
    model.bindText("ad", input)          // iki yonlu: input <-> model["ad"]

    let echoLbl = Label(root, "Yansima:")
    echoLbl.setBounds(20, 64, 80, 24)
    let echo = Label(root, "")
    echo.setBounds(110, 64, 280, 24)
    model.bindText("ad", echo)           // canli yansima (Label -> tek yon)

    let yasLbl = Label(root, "Yas:")
    yasLbl.setBounds(20, 104, 80, 24)
    let spin = SpinCtrl(root, 0, 120, 0)
    spin.setBounds(110, 100, 100, 28)
    model.bindInt("yas", spin)           // iki yonlu: spin <-> model["yas"]

    let reset = Button(root, "Sifirla")
    reset.setBounds(110, 150, 200, 30)
    reset.onClick(|_h: i32| {
        model.setText("ad", "Varsayilan")    // model -> input + echo
        model.setInt("yas", 18)              // model -> spin
    })

    win.show()
    appRun()
}
```

Oluşturduktan sonra örneğin temiz IR ürettiğini doğrula:

Run: `build-clang/livac.exe --emit-ir -o build-clang/_ex_bind.ll examples/ui_data_binding.liva`
`build-clang/_ex_bind.ll` içinde `define` olduğunu Grep ile teyit et, sonra `_ex_bind.ll`'i sil. livac hata verirse örneği gerçek API'ye göre düzelt (`stdlib/ui/widgets.liva` — Model.setText/getText/setInt/getInt/bindText/bindInt; Control.setBounds/onClick; Label/TextInput/SpinCtrl/Button).

- [ ] **Step 4: Run the full regression suite**

Run: `cmake --build build-clang && ctest --test-dir build-clang --output-on-failure`
Expected: TÜM testler PASS (mevcut 2310 + Faz 5'te eklenen 3 test = 2313). Tam pass/fail sayısını raporla.

- [ ] **Step 5: GUI doğrulama (wx-kurulu makinede — manuel)**

> Kullanıcının wx-kurulu ortamında çalıştırılır. Otomatik test edilemez (headless senkron).

```bash
build-clang/livac.exe examples/ui_data_binding.liva -o examples/ui_data_binding.exe
examples/ui_data_binding.exe
```

Doğrula:
- Açılışta input "Ali", echo "Ali", spin 30 gösterir (model→widget ilk senkron).
- input'a yaz → echo Label canlı değişir (widget→model→diğer widget).
- spin'i oynat → (bağlı başka widget yoksa görsel etki sınırlı; en azından crash yok ve model güncellenir).
- "Sifirla" → input + echo "Varsayilan", spin 18 olur (model→widget'lar).
- Sonsuz döngü / donma / crash yok (feedback-guard çalışıyor).

- [ ] **Step 6: Commit**

```bash
git add examples/ui_data_binding.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase5: data-binding example + combined regression test

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Tamamlama

İki task + örnek tamamlandığında `superpowers:finishing-a-development-branch` ile entegrasyon (master'a merge / PR) seçeneklerini sun. Branch: `feat/ui-vcl-phase5-data-binding`.
