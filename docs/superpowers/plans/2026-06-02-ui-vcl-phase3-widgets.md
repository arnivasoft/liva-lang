# Faz 3 — Yeni Widget'lar Implementasyon Planı

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** wxWidgets tabanlı `ui::widgets` kütüphanesine altı yeni VCL-tarzı widget eklemek: SpinCtrl, DatePicker, ComboBox, TreeView, DataGrid, Splitter.

**Architecture:** Faz 1/2'nin kanıtlanmış 3 katmanı korunur: `ui::widgets` Liva sınıfı (Control alt sınıfı) → `liva_ui_*` intrinsic (IRGenCall.cpp) → `wx_runtime.cpp` (wxWidgets). Her widget bir `Control` alt sınıfıdır; ortak metotlar (`setEnabled`/`onChange`/...) miras alınır. Yeni mimari yok — yalnız mevcut deseni tekrarlar. Callback fast-path'e dokunulmaz (yeni widget'lar Control alt sınıfı olduğundan otomatik kapsanır).

**Tech Stack:** C++20, LLVM 21 IRGen, wxWidgets 3.3 (vcpkg, `C:\Users\Kadir\.vcpkg-clion\vcpkg`), CMake/Ninja (`build-clang/`), GoogleTest.

---

## Dokunulacak Dosyalar

Her widget tam bir dikey dilimdir; aşağıdaki 5 dosyaya dokunur:

- **`stdlib/ui/wx_runtime.h`** — yeni FFI prototipleri (`extern "C"` blok içinde Faz 3 bölümü).
- **`stdlib/ui/wx_runtime.cpp`** — wxWidgets implementasyonu (yeni `#include`'lar, `g_treeNodes` yan tablosu, FFI gövdeleri, `on_change`/`on_select`'e `dynamic_cast` dalları).
- **`src/IR/IRGen.cpp`** — her FFI için LLVM extern kaydı (`declareCoroutineIntrinsics();` çağrısından hemen önce Faz 3 bloğu).
- **`src/IR/IRGenCall.cpp`** — her FFI için intrinsic eşleme (`toolItemSetEnabled` bloğundan sonra, `// Closure-taking free-function forms` yorumundan önce Faz 3 bloğu).
- **`src/Sema/ModuleLoader.cpp`** — `std::ui` builtin ad listesine yeni adlar.
- **`stdlib/ui/widgets.liva`** — yeni `Control` alt sınıfları (dosya sonuna, `Toolbar`'dan sonra).
- **`tests/unit/UICodegenExecTest.cpp`** — her widget için emit-IR derleme testi.
- **`examples/ui_widgets_advanced.liva`** — (Task 7) birleşik örnek.

### Doğrulama stratejisi (önemli)

- **Derleyici zinciri:** Testler `livac --emit-ir` kullanır (Lexer→Parser→Sema→IRGen, **link yok**). Bir widget sınıfı veya builtin ad eksikse Sema hata verir → IR üretilmez → test düşer. Tümü eklenince test geçer. Bu, klasik TDD kırmızı→yeşil döngüsüdür.
- **Runtime C++ derlemesi:** Bu makinede wxWidgets KURULU (`LIVA_HAS_WXWIDGETS=ON`). `cmake --build build-clang`, `wx_runtime.cpp`'yi de derler → C++ hataları derleme aşamasında yakalanır.
- **GUI davranışı:** Gerçek pencere davranışı yalnız wx-kurulu makinede `.exe` çalıştırılarak teyit edilir (Task 7). Bu plandaki adımlar derleme + IR doğrulamasıyla sınırlıdır.

### Ortak komutlar

- Tam derleme (livac + testler + `liva_ui` runtime): `cmake --build build-clang`
- Tek test çalıştırma: `ctest --test-dir build-clang -R "<TestAdı>" --output-on-failure`
- Tüm UI testleri: `ctest --test-dir build-clang -R "UICodegenExec" --output-on-failure`
- Tam regresyon: `ctest --test-dir build-clang --output-on-failure`

---

## Task 1: SpinCtrl (wxSpinCtrl)

En basit widget — `getValue`/`setValue` mevcut `liva_ui_get_value`/`set_value`'ye `wxSpinCtrl` `dynamic_cast` dalı ekleyerek çalışır; tek yeni FFI `create_spin_ctrl`.

**Files:**
- Test: `tests/unit/UICodegenExecTest.cpp`
- Modify: `stdlib/ui/wx_runtime.h`, `stdlib/ui/wx_runtime.cpp`, `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/ModuleLoader.cpp`, `stdlib/ui/widgets.liva`

- [ ] **Step 1: Write the failing test**

`tests/unit/UICodegenExecTest.cpp` içinde, `StatusBarAndToolbarCompile` testinden sonra (dosyadaki son `TEST(...)`'tan sonra, `#endif // LIVA_HAS_LLVM`'den önce) ekle:

```cpp
// ── Phase 3: new widgets ───────────────────────────────────────────────
TEST(UICodegenExec, SpinCtrlCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let sp = SpinCtrl(win, 0, 100, 5)\n"
        "  sp.setValue(10)\n"
        "  println(sp.getValue())\n"
        "  sp.onChange(|_h: i32| { })\n"
        "}\n",
        "spin_ctrl");
    EXPECT_TRUE(emitsClean(ir));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.SpinCtrlCompiles" --output-on-failure`
Expected: FAIL — `no IR emitted` (`SpinCtrl` sınıfı ve `createSpinCtrl` builtin'i henüz yok, Sema hata verir).

- [ ] **Step 3a: Add runtime FFI prototype**

`stdlib/ui/wx_runtime.h` içinde, Toolbar bölümünden sonra (`liva_ui_tool_item_on_click` satırından sonra, `/* ── List / Tab operations ── */` yorumundan önce) ekle:

```c
/* ── Phase 3: new widgets ─────────────────────────────────────────── */
int32_t  liva_ui_create_spin_ctrl(int32_t parent, int32_t minVal, int32_t maxVal, int32_t val);
```

- [ ] **Step 3b: Add runtime include + implementation**

`stdlib/ui/wx_runtime.cpp` içinde, include bloğuna (`#include <wx/artprov.h>` satırından sonra) ekle:

```cpp
#include <wx/spinctrl.h>
#include <wx/datectrl.h>
#include <wx/dateevt.h>
#include <wx/combobox.h>
#include <wx/treectrl.h>
#include <wx/grid.h>
#include <wx/splitter.h>
```

> Not: Bu yedi include Faz 3'ün tamamı için bir kez eklenir (Task 1'de). Sonraki task'lar tekrar eklemez.

Aynı dosyada, `liva_ui_create_slider` fonksiyonunun hemen ÜZERİNE bir Faz 3 create bölümü açmak yerine, dosya sonundaki Toolbar bölümünden sonra (son fonksiyondan sonra) yeni bir bölüm ekle:

```cpp
/* ═══════════════════════════════════════════════════════════════════
   Phase 3: new widgets
   ═══════════════════════════════════════════════════════════════════ */

int32_t liva_ui_create_spin_ctrl(int32_t parent, int32_t minVal, int32_t maxVal, int32_t val) {
    auto *p = getHandle<wxWindow>(parent);
    auto *sc = new wxSpinCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition,
                              wxDefaultSize, wxSP_ARROWS, minVal, maxVal, val);
    return allocHandle(sc);
}
```

Aynı dosyada, `liva_ui_set_value`'nun `wxCheckBox` dalından sonra `wxSpinCtrl` dalı ekle:

```cpp
    else if (auto *spin = dynamic_cast<wxSpinCtrl *>(w))
        spin->SetValue(val);
```

Ve `liva_ui_get_value`'da `wxCheckBox` dalından sonra (final `return 0;`'dan önce):

```cpp
    if (auto *spin = dynamic_cast<wxSpinCtrl *>(w))
        return spin->GetValue();
```

`liva_ui_on_change`'e `wxSpinCtrl` dalı ekle (`wxCheckBox` dalından sonra):

```cpp
    } else if (dynamic_cast<wxSpinCtrl *>(w)) {
        w->Bind(wxEVT_SPINCTRL, [cb](wxSpinEvent &) { cb.invoke(); });
```

- [ ] **Step 3c: Add IRGen extern**

`src/IR/IRGen.cpp` içinde, `// Coroutine + async runtime` yorumundan (ve `declareCoroutineIntrinsics();` çağrısından) hemen ÖNCE yeni bir Faz 3 bloğu aç:

```cpp
    // ── Phase 3: new widgets ─────────────────────────────────────────
    // create_spin_ctrl(i32 parent, i32 min, i32 max, i32 val) -> i32
    module_->getOrInsertFunction("liva_ui_create_spin_ctrl", uiCreateSliderTy);
```

> `uiCreateSliderTy` = `(i32,i32,i32,i32)->i32`, bu noktada zaten tanımlı (satır ~1410).

- [ ] **Step 3d: Add IRGenCall intrinsic**

`src/IR/IRGenCall.cpp` içinde, `toolItemSetEnabled` bloğundan sonra, `// Closure-taking free-function forms` yorumundan ÖNCE yeni bir Faz 3 bloğu aç:

```cpp
    // ── Phase 3: new widgets ─────────────────────────────────────────
    // createSpinCtrl(parent, min, max, val) -> i32
    if (funcName == "createSpinCtrl" && node->getArgs().size() >= 4) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *minV = visit(node->getArgs()[1].get());
        auto *maxV = visit(node->getArgs()[2].get());
        auto *val = visit(node->getArgs()[3].get());
        if (!parent || !minV || !maxV || !val) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_spin_ctrl"),
                                    {parent, minV, maxV, val}, "ui.spin");
    }
```

> `getValue`/`setValue` intrinsic'leri zaten mevcut (SpinCtrl bunları yeniden kullanır).

- [ ] **Step 3e: Register builtin name**

`src/Sema/ModuleLoader.cpp` içinde, builtin ad listesinin son satırını (`"toolItemSetEnabled", "toolItemOnClick"});`) değiştir:

```cpp
         "toolItemSetEnabled", "toolItemOnClick",
         // Phase 3: new widgets
         "createSpinCtrl"});
```

- [ ] **Step 3f: Add widget class**

`stdlib/ui/widgets.liva` dosyasının SONUNA (Toolbar sınıfından sonra) ekle:

```liva
// ═══ Phase 3: new widgets ═══════════════════════════════════════

// ── SpinCtrl (wxSpinCtrl) ──────────────────────────────────────
pub class SpinCtrl: Control {
    init(parent: Control, minVal: i32, maxVal: i32, val: i32) {
        super.init(createSpinCtrl(parent.handle, minVal, maxVal, val))
    }
    pub func getValue() -> i32 { return getValue(self.handle) }
    pub func setValue(v: i32) { setValue(self.handle, v) }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.SpinCtrlCompiles" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add stdlib/ui/wx_runtime.h stdlib/ui/wx_runtime.cpp src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/ModuleLoader.cpp stdlib/ui/widgets.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase3: SpinCtrl widget (wxSpinCtrl + get/set/onChange)"
```

---

## Task 2: DatePicker (wxDatePickerCtrl)

Yeni `create` FFI + ISO string döndüren `date_get_value` (`returnTempStr` deseni) + `wxEVT_DATE_CHANGED` → `on_change`.

**Files:**
- Test: `tests/unit/UICodegenExecTest.cpp`
- Modify: `stdlib/ui/wx_runtime.h`, `stdlib/ui/wx_runtime.cpp`, `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/ModuleLoader.cpp`, `stdlib/ui/widgets.liva`

- [ ] **Step 1: Write the failing test**

`tests/unit/UICodegenExecTest.cpp` içinde, `SpinCtrlCompiles`'dan sonra ekle:

```cpp
TEST(UICodegenExec, DatePickerCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let dp = DatePicker(win)\n"
        "  println(dp.getDate())\n"
        "  dp.onChange(|_h: i32| { })\n"
        "}\n",
        "date_picker");
    EXPECT_TRUE(emitsClean(ir));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.DatePickerCompiles" --output-on-failure`
Expected: FAIL — `no IR emitted`.

- [ ] **Step 3a: Add runtime FFI prototypes**

`stdlib/ui/wx_runtime.h` Faz 3 bölümüne (Task 1'in eklediği `create_spin_ctrl` satırından sonra) ekle:

```c
int32_t      liva_ui_create_date_picker(int32_t parent);
const char  *liva_ui_date_get_value(int32_t handle);     /* "YYYY-MM-DD" */
```

- [ ] **Step 3b: Add runtime implementation**

`stdlib/ui/wx_runtime.cpp` Faz 3 bölümüne (SpinCtrl fonksiyonundan sonra) ekle:

```cpp
int32_t liva_ui_create_date_picker(int32_t parent) {
    auto *p = getHandle<wxWindow>(parent);
    auto *dp = new wxDatePickerCtrl(p, wxID_ANY);
    return allocHandle(dp);
}

const char *liva_ui_date_get_value(int32_t handle) {
    auto *dp = getHandle<wxDatePickerCtrl>(handle);
    if (!dp) return "";
    wxDateTime d = dp->GetValue();
    if (!d.IsValid()) return "";
    return returnTempStr(d.FormatISODate());
}
```

Aynı dosyada, `liva_ui_on_change`'e `wxDatePickerCtrl` dalı ekle (`wxSpinCtrl` dalından sonra):

```cpp
    } else if (dynamic_cast<wxDatePickerCtrl *>(w)) {
        w->Bind(wxEVT_DATE_CHANGED, [cb](wxDateEvent &) { cb.invoke(); });
```

- [ ] **Step 3c: Add IRGen externs**

`src/IR/IRGen.cpp` Faz 3 bloğuna (SpinCtrl extern'inden sonra) ekle:

```cpp
    // create_date_picker(i32 parent) -> i32 ; date_get_value(i32) -> ptr
    module_->getOrInsertFunction("liva_ui_create_date_picker", uiCreateParentTy);
    module_->getOrInsertFunction("liva_ui_date_get_value", uiGetTextTy);
```

> `uiCreateParentTy` = `(i32)->i32` (satır ~1390), `uiGetTextTy` = `(i32)->i8Ptr` (satır ~1422) — ikisi de kapsamda.

- [ ] **Step 3d: Add IRGenCall intrinsics**

`src/IR/IRGenCall.cpp` Faz 3 bloğuna (createSpinCtrl'den sonra) ekle:

```cpp
    // createDatePicker(parent) -> i32
    if (funcName == "createDatePicker" && node->getArgs().size() >= 1) {
        auto *parent = visit(node->getArgs()[0].get());
        if (!parent) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_date_picker"),
                                    {parent}, "ui.date");
    }
    // dateGetValue(handle) -> string
    if (funcName == "dateGetValue" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_date_get_value"),
                                    {handle}, "ui.dgv");
    }
```

- [ ] **Step 3e: Register builtin names**

`src/Sema/ModuleLoader.cpp` Faz 3 satırını güncelle (`"createSpinCtrl"});` → ):

```cpp
         "createSpinCtrl", "createDatePicker", "dateGetValue"});
```

- [ ] **Step 3f: Add widget class**

`stdlib/ui/widgets.liva` Faz 3 bölümüne (SpinCtrl sınıfından sonra) ekle:

```liva
// ── DatePicker (wxDatePickerCtrl) ──────────────────────────────
pub class DatePicker: Control {
    init(parent: Control) { super.init(createDatePicker(parent.handle)) }
    pub func getDate() -> string { return dateGetValue(self.handle) }   // "YYYY-MM-DD"
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.DatePickerCompiles" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add stdlib/ui/wx_runtime.h stdlib/ui/wx_runtime.cpp src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/ModuleLoader.cpp stdlib/ui/widgets.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase3: DatePicker widget (wxDatePickerCtrl + ISO getDate/onChange)"
```

---

## Task 3: ComboBox (wxComboBox — editable dropdown)

Yeni `create` (değer string'li) + `combo_add_item` (`Append`); `getText`/`getValue` mevcut intrinsic'leri yeniden kullanır; `wxEVT_COMBOBOX`→`on_select`, `wxEVT_TEXT`→`on_change`.

**Files:**
- Test: `tests/unit/UICodegenExecTest.cpp`
- Modify: `stdlib/ui/wx_runtime.h`, `stdlib/ui/wx_runtime.cpp`, `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/ModuleLoader.cpp`, `stdlib/ui/widgets.liva`

- [ ] **Step 1: Write the failing test**

`tests/unit/UICodegenExecTest.cpp` içinde, `DatePickerCompiles`'dan sonra ekle:

```cpp
TEST(UICodegenExec, ComboBoxCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let cb = ComboBox(win, \"baslangic\")\n"
        "  cb.addItem(\"bir\")\n"
        "  cb.addItem(\"iki\")\n"
        "  println(cb.getText())\n"
        "  println(cb.getSelection())\n"
        "  cb.onSelect(|_h: i32| { })\n"
        "  cb.onChange(|_h: i32| { })\n"
        "}\n",
        "combo_box");
    EXPECT_TRUE(emitsClean(ir));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.ComboBoxCompiles" --output-on-failure`
Expected: FAIL — `no IR emitted`.

- [ ] **Step 3a: Add runtime FFI prototypes**

`stdlib/ui/wx_runtime.h` Faz 3 bölümüne (DatePicker satırlarından sonra) ekle:

```c
int32_t  liva_ui_create_combo_box(int32_t parent, const char *value);
void     liva_ui_combo_add_item(int32_t handle, const char *item);
```

- [ ] **Step 3b: Add runtime implementation**

`stdlib/ui/wx_runtime.cpp` Faz 3 bölümüne (DatePicker fonksiyonlarından sonra) ekle:

```cpp
int32_t liva_ui_create_combo_box(int32_t parent, const char *value) {
    auto *p = getHandle<wxWindow>(parent);
    auto *cb = new wxComboBox(p, wxID_ANY, wxString::FromUTF8(value ? value : ""));
    return allocHandle(cb);
}

void liva_ui_combo_add_item(int32_t handle, const char *item) {
    if (auto *cb = getHandle<wxComboBox>(handle))
        cb->Append(wxString::FromUTF8(item ? item : ""));
}
```

Aynı dosyada `liva_ui_get_text`'e `wxComboBox` dalı ekle (`wxTextCtrl` dalından sonra, son `return returnTempStr(w->GetLabel());`'den önce):

```cpp
    if (auto *combo = dynamic_cast<wxComboBox *>(w))
        return returnTempStr(combo->GetValue());
```

`liva_ui_get_value`'a `wxComboBox` dalı ekle (`wxSpinCtrl` dalından sonra, final `return 0;`'dan önce):

```cpp
    if (auto *combo = dynamic_cast<wxComboBox *>(w))
        return combo->GetSelection();
```

`liva_ui_on_change`'e `wxComboBox` dalı ekle (`wxDatePickerCtrl` dalından sonra):

```cpp
    } else if (dynamic_cast<wxComboBox *>(w)) {
        w->Bind(wxEVT_TEXT, [cb](wxCommandEvent &) { cb.invoke(); });
```

`liva_ui_on_select`'e `wxComboBox` dalı ekle (`wxChoice` dalından sonra):

```cpp
    } else if (dynamic_cast<wxComboBox *>(w)) {
        w->Bind(wxEVT_COMBOBOX, [cb](wxCommandEvent &) { cb.invoke(); });
```

> Not: `wxComboBox`, `wxChoice`'tan türemez; `dynamic_cast<wxChoice*>` ComboBox için null döner, yani sıra önemli değil ama netlik için Choice'tan sonra eklenir.

- [ ] **Step 3c: Add IRGen externs**

`src/IR/IRGen.cpp` Faz 3 bloğuna (DatePicker extern'lerinden sonra) ekle:

```cpp
    // create_combo_box(i32, ptr) -> i32 ; combo_add_item(i32, ptr) -> void
    module_->getOrInsertFunction("liva_ui_create_combo_box", uiCreateParentStrTy);
    module_->getOrInsertFunction("liva_ui_combo_add_item", uiHandleStrTy);
```

> `uiCreateParentStrTy` = `(i32,i8Ptr)->i32` (satır ~1399), `uiHandleStrTy` = `(i32,i8Ptr)->void` — ikisi de kapsamda.

- [ ] **Step 3d: Add IRGenCall intrinsics**

`src/IR/IRGenCall.cpp` Faz 3 bloğuna (dateGetValue'dan sonra) ekle:

```cpp
    // createComboBox(parent, value) -> i32
    if (funcName == "createComboBox" && node->getArgs().size() >= 2) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *value = visit(node->getArgs()[1].get());
        if (!parent || !value) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_combo_box"),
                                    {parent, value}, "ui.combo");
    }
    // comboAddItem(handle, item) -> void
    if (funcName == "comboAddItem" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *item = visit(node->getArgs()[1].get());
        if (!handle || !item) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_combo_add_item"), {handle, item});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
```

- [ ] **Step 3e: Register builtin names**

`src/Sema/ModuleLoader.cpp` Faz 3 satırını güncelle:

```cpp
         "createSpinCtrl", "createDatePicker", "dateGetValue",
         "createComboBox", "comboAddItem"});
```

- [ ] **Step 3f: Add widget class**

`stdlib/ui/widgets.liva` Faz 3 bölümüne (DatePicker sınıfından sonra) ekle:

```liva
// ── ComboBox (wxComboBox — editable dropdown) ──────────────────
pub class ComboBox: Control {
    init(parent: Control, value: string) { super.init(createComboBox(parent.handle, value)) }
    pub func addItem(item: string) { comboAddItem(self.handle, item) }
    pub func getText() -> string { return getText(self.handle) }
    pub func getSelection() -> i32 { return getValue(self.handle) }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.ComboBoxCompiles" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add stdlib/ui/wx_runtime.h stdlib/ui/wx_runtime.cpp src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/ModuleLoader.cpp stdlib/ui/widgets.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase3: ComboBox widget (editable wxComboBox + addItem/getText/onSelect)"
```

---

## Task 4: TreeView (wxTreeCtrl — node = i32 handle)

Yeni `create` + `g_treeNodes` yan tablosu (Faz 2 menü-item deseni; `wxTreeItemId` değerlerini i32 handle'a eşler) + `add_root`/`add_node`/`get_selection` + `wxEVT_TREE_SEL_CHANGED`→`on_select`.

**Files:**
- Test: `tests/unit/UICodegenExecTest.cpp`
- Modify: `stdlib/ui/wx_runtime.h`, `stdlib/ui/wx_runtime.cpp`, `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/ModuleLoader.cpp`, `stdlib/ui/widgets.liva`

- [ ] **Step 1: Write the failing tests**

`tests/unit/UICodegenExecTest.cpp` içinde, `ComboBoxCompiles`'dan sonra ekle. İlki temel derleme, ikincisi node-handle akışı + fast-path heap-own (Control alt sınıfında inline `onSelect`):

```cpp
TEST(UICodegenExec, TreeViewCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let tree = TreeView(win)\n"
        "  let root = tree.addRoot(\"Proje\")\n"
        "  let src = tree.addNode(root, \"src\")\n"
        "  tree.addNode(src, \"main.liva\")\n"
        "  println(tree.getSelection())\n"
        "}\n",
        "tree_view");
    EXPECT_TRUE(emitsClean(ir));
}

// True if any liva_ui_on_select CALL site passes a non-zero env size (4th arg),
// i.e. the heap-own fast path fired for a Control-subclass widget.
static bool anySelectHeapOwns(const std::string &ir) {
    const std::string needle = "call void @liva_ui_on_select(";
    for (size_t p = ir.find(needle); p != std::string::npos;
         p = ir.find(needle, p + 1)) {
        size_t end = ir.find(')', p);
        if (end == std::string::npos) break;
        if (ir.substr(p, end - p + 1).find(", i32 0)") == std::string::npos)
            return true;
    }
    return false;
}

TEST(UICodegenExec, TreeViewInlineSelectHeapOwns) {
    // Inline onSelect closure on a Control subclass (TreeView) must take the
    // generalized fast path and heap-own its env, even inside a helper that
    // returns the widget. Confirms the fast path auto-covers new widgets.
    auto ir = emitIR(
        "import ui::widgets\n"
        "func build(parent: Control) -> TreeView {\n"
        "  var n = 0\n"
        "  let tree = TreeView(parent)\n"
        "  tree.onSelect(|_h: i32| { n = n + 1 })\n"
        "  return tree\n"
        "}\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let t = build(win)\n"
        "}\n",
        "tree_select_heap");
    ASSERT_TRUE(emitsClean(ir));
    EXPECT_TRUE(anySelectHeapOwns(ir))
        << "inline onSelect on a Control subclass must heap-own the env";
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.TreeView" --output-on-failure`
Expected: FAIL — `no IR emitted` (her iki test).

- [ ] **Step 3a: Add runtime FFI prototypes**

`stdlib/ui/wx_runtime.h` Faz 3 bölümüne (ComboBox satırlarından sonra) ekle:

```c
int32_t  liva_ui_create_tree_view(int32_t parent);
int32_t  liva_ui_tree_add_root(int32_t handle, const char *label);              /* -> node handle */
int32_t  liva_ui_tree_add_node(int32_t handle, int32_t parentNode, const char *label);
int32_t  liva_ui_tree_get_selection(int32_t handle);                            /* -> node handle (0 if none) */
```

- [ ] **Step 3b: Add runtime node side-table + implementation**

`stdlib/ui/wx_runtime.cpp` içinde, `g_menuItems` tanımının yakınına (menü-item deseninin yanına, dosyanın handle-tablosu/yan-tablo bölümüne — örn. `static std::unordered_map<int32_t, LivaToolItem> g_toolItems;` satırından sonra) tree node yan tablosunu ekle:

```cpp
// TreeView nodes are wxTreeItemId values (not wxWindow*); map them to i32
// handles using the global handle counter (unique, but kept out of g_handles
// since they are never looked up via getHandle<>). Mirrors the menu-item table.
static std::unordered_map<int32_t, wxTreeItemId> g_treeNodes;
```

Faz 3 bölümüne (ComboBox fonksiyonlarından sonra) ekle:

```cpp
int32_t liva_ui_create_tree_view(int32_t parent) {
    auto *p = getHandle<wxWindow>(parent);
    auto *tree = new wxTreeCtrl(p, wxID_ANY);
    return allocHandle(tree);
}

int32_t liva_ui_tree_add_root(int32_t handle, const char *label) {
    auto *tree = getHandle<wxTreeCtrl>(handle);
    if (!tree) return 0;
    wxTreeItemId id = tree->AddRoot(wxString::FromUTF8(label ? label : ""));
    int32_t nh = g_nextHandle++;
    g_treeNodes[nh] = id;
    return nh;
}

int32_t liva_ui_tree_add_node(int32_t handle, int32_t parentNode, const char *label) {
    auto *tree = getHandle<wxTreeCtrl>(handle);
    if (!tree) return 0;
    auto it = g_treeNodes.find(parentNode);
    if (it == g_treeNodes.end()) return 0;
    wxTreeItemId id = tree->AppendItem(it->second, wxString::FromUTF8(label ? label : ""));
    int32_t nh = g_nextHandle++;
    g_treeNodes[nh] = id;
    return nh;
}

int32_t liva_ui_tree_get_selection(int32_t handle) {
    auto *tree = getHandle<wxTreeCtrl>(handle);
    if (!tree) return 0;
    wxTreeItemId sel = tree->GetSelection();
    if (!sel.IsOk()) return 0;
    for (auto &kv : g_treeNodes)
        if (kv.second == sel) return kv.first;
    return 0;
}
```

`liva_ui_on_select`'e `wxTreeCtrl` dalı ekle (`wxComboBox` dalından sonra):

```cpp
    } else if (dynamic_cast<wxTreeCtrl *>(w)) {
        w->Bind(wxEVT_TREE_SEL_CHANGED, [cb](wxTreeEvent &) { cb.invoke(); });
```

- [ ] **Step 3c: Add IRGen externs**

`src/IR/IRGen.cpp` Faz 3 bloğuna (ComboBox extern'lerinden sonra) ekle:

```cpp
    // TreeView: create(i32)->i32, add_root(i32,ptr)->i32,
    //           add_node(i32,i32,ptr)->i32, get_selection(i32)->i32
    auto *uiI32I32StrRetI32Ty =
        llvm::FunctionType::get(i32Ty, {i32Ty, i32Ty, i8PtrTy}, false);
    module_->getOrInsertFunction("liva_ui_create_tree_view", uiCreateParentTy);
    module_->getOrInsertFunction("liva_ui_tree_add_root", uiI32StrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_tree_add_node", uiI32I32StrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_tree_get_selection", uiHandleRetI32Ty);
```

> `uiI32StrRetI32Ty` = `(i32,i8Ptr)->i32` (satır ~1359), `uiHandleRetI32Ty` = `(i32)->i32` — ikisi de kapsamda. `uiI32I32StrRetI32Ty` yalnız bu task'ta tanımlanır.

- [ ] **Step 3d: Add IRGenCall intrinsics**

`src/IR/IRGenCall.cpp` Faz 3 bloğuna (comboAddItem'dan sonra) ekle:

```cpp
    // createTreeView(parent) -> i32
    if (funcName == "createTreeView" && node->getArgs().size() >= 1) {
        auto *parent = visit(node->getArgs()[0].get());
        if (!parent) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_tree_view"),
                                    {parent}, "ui.tree");
    }
    // treeAddRoot(handle, label) -> i32
    if (funcName == "treeAddRoot" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *label = visit(node->getArgs()[1].get());
        if (!handle || !label) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_tree_add_root"),
                                    {handle, label}, "ui.troot");
    }
    // treeAddNode(handle, parentNode, label) -> i32
    if (funcName == "treeAddNode" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *parentNode = visit(node->getArgs()[1].get());
        auto *label = visit(node->getArgs()[2].get());
        if (!handle || !parentNode || !label) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_tree_add_node"),
                                    {handle, parentNode, label}, "ui.tnode");
    }
    // treeGetSelection(handle) -> i32
    if (funcName == "treeGetSelection" && node->getArgs().size() >= 1) {
        auto *handle = visit(node->getArgs()[0].get());
        if (!handle) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_tree_get_selection"),
                                    {handle}, "ui.tsel");
    }
```

- [ ] **Step 3e: Register builtin names**

`src/Sema/ModuleLoader.cpp` Faz 3 satırlarını güncelle:

```cpp
         "createSpinCtrl", "createDatePicker", "dateGetValue",
         "createComboBox", "comboAddItem",
         "createTreeView", "treeAddRoot", "treeAddNode", "treeGetSelection"});
```

- [ ] **Step 3f: Add widget class**

`stdlib/ui/widgets.liva` Faz 3 bölümüne (ComboBox sınıfından sonra) ekle:

```liva
// ── TreeView (wxTreeCtrl — node = i32 handle) ──────────────────
pub class TreeView: Control {
    init(parent: Control) { super.init(createTreeView(parent.handle)) }
    pub func addRoot(label: string) -> i32 { return treeAddRoot(self.handle, label) }
    pub func addNode(parentNode: i32, label: string) -> i32 {
        return treeAddNode(self.handle, parentNode, label)
    }
    pub func getSelection() -> i32 { return treeGetSelection(self.handle) }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.TreeView" --output-on-failure`
Expected: PASS (her iki test).

- [ ] **Step 5: Commit**

```bash
git add stdlib/ui/wx_runtime.h stdlib/ui/wx_runtime.cpp src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/ModuleLoader.cpp stdlib/ui/widgets.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase3: TreeView widget (wxTreeCtrl + node-handle side-table + onSelect)"
```

---

## Task 5: DataGrid (wxGrid — hücre = row/col)

Yeni `create` (`CreateGrid(rows, cols)`) + `set_cell`/`get_cell`/`set_col_label` + `wxEVT_GRID_CELL_CHANGED`→`on_change`.

**Files:**
- Test: `tests/unit/UICodegenExecTest.cpp`
- Modify: `stdlib/ui/wx_runtime.h`, `stdlib/ui/wx_runtime.cpp`, `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/ModuleLoader.cpp`, `stdlib/ui/widgets.liva`

- [ ] **Step 1: Write the failing test**

`tests/unit/UICodegenExecTest.cpp` içinde, `TreeViewInlineSelectHeapOwns`'dan sonra ekle:

```cpp
TEST(UICodegenExec, DataGridCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let grid = DataGrid(win, 5, 3)\n"
        "  grid.setColLabel(0, \"Ad\")\n"
        "  grid.setCell(0, 0, \"Ali\")\n"
        "  println(grid.getCell(0, 0))\n"
        "  grid.onChange(|_h: i32| { })\n"
        "}\n",
        "data_grid");
    EXPECT_TRUE(emitsClean(ir));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.DataGridCompiles" --output-on-failure`
Expected: FAIL — `no IR emitted`.

- [ ] **Step 3a: Add runtime FFI prototypes**

`stdlib/ui/wx_runtime.h` Faz 3 bölümüne (TreeView satırlarından sonra) ekle:

```c
int32_t      liva_ui_create_data_grid(int32_t parent, int32_t rows, int32_t cols);
void         liva_ui_grid_set_cell(int32_t handle, int32_t row, int32_t col, const char *text);
const char  *liva_ui_grid_get_cell(int32_t handle, int32_t row, int32_t col);
void         liva_ui_grid_set_col_label(int32_t handle, int32_t col, const char *text);
```

- [ ] **Step 3b: Add runtime implementation**

`stdlib/ui/wx_runtime.cpp` Faz 3 bölümüne (TreeView fonksiyonlarından sonra) ekle:

```cpp
int32_t liva_ui_create_data_grid(int32_t parent, int32_t rows, int32_t cols) {
    auto *p = getHandle<wxWindow>(parent);
    auto *grid = new wxGrid(p, wxID_ANY);
    grid->CreateGrid(rows, cols);
    return allocHandle(grid);
}

void liva_ui_grid_set_cell(int32_t handle, int32_t row, int32_t col, const char *text) {
    if (auto *grid = getHandle<wxGrid>(handle))
        grid->SetCellValue(row, col, wxString::FromUTF8(text ? text : ""));
}

const char *liva_ui_grid_get_cell(int32_t handle, int32_t row, int32_t col) {
    auto *grid = getHandle<wxGrid>(handle);
    if (!grid) return "";
    return returnTempStr(grid->GetCellValue(row, col));
}

void liva_ui_grid_set_col_label(int32_t handle, int32_t col, const char *text) {
    if (auto *grid = getHandle<wxGrid>(handle))
        grid->SetColLabelValue(col, wxString::FromUTF8(text ? text : ""));
}
```

`liva_ui_on_change`'e `wxGrid` dalı ekle (`wxComboBox` dalından sonra):

```cpp
    } else if (auto *grid = dynamic_cast<wxGrid *>(w)) {
        grid->Bind(wxEVT_GRID_CELL_CHANGED, [cb](wxGridEvent &) { cb.invoke(); });
```

> Not: `wxGrid` olay bağlamayı kendi üzerinde yapar (`grid->Bind`), `w->Bind` değil — `wxEVT_GRID_*` olayları grid'e özgüdür.

- [ ] **Step 3c: Add IRGen externs**

`src/IR/IRGen.cpp` Faz 3 bloğuna (TreeView extern'lerinden sonra) ekle:

```cpp
    // DataGrid: create(i32,i32,i32)->i32, set_cell(i32,i32,i32,ptr)->void,
    //           get_cell(i32,i32,i32)->ptr, set_col_label(i32,i32,ptr)->void
    auto *uiI32I32I32RetI32Ty =
        llvm::FunctionType::get(i32Ty, {i32Ty, i32Ty, i32Ty}, false);
    auto *uiI32I32I32StrVoidTy =
        llvm::FunctionType::get(voidTy, {i32Ty, i32Ty, i32Ty, i8PtrTy}, false);
    auto *uiI32I32I32RetStrTy =
        llvm::FunctionType::get(i8PtrTy, {i32Ty, i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_create_data_grid", uiI32I32I32RetI32Ty);
    module_->getOrInsertFunction("liva_ui_grid_set_cell", uiI32I32I32StrVoidTy);
    module_->getOrInsertFunction("liva_ui_grid_get_cell", uiI32I32I32RetStrTy);
    module_->getOrInsertFunction("liva_ui_grid_set_col_label", uiI32I32StrVoidTy);
```

> `uiI32I32StrVoidTy` = `(i32,i32,i8Ptr)->void` (satır ~1365) kapsamda. Üç yeni tip yalnız bu task'ta tanımlanır.

- [ ] **Step 3d: Add IRGenCall intrinsics**

`src/IR/IRGenCall.cpp` Faz 3 bloğuna (treeGetSelection'dan sonra) ekle:

```cpp
    // createDataGrid(parent, rows, cols) -> i32
    if (funcName == "createDataGrid" && node->getArgs().size() >= 3) {
        auto *parent = visit(node->getArgs()[0].get());
        auto *rows = visit(node->getArgs()[1].get());
        auto *cols = visit(node->getArgs()[2].get());
        if (!parent || !rows || !cols) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_data_grid"),
                                    {parent, rows, cols}, "ui.grid2");
    }
    // gridSetCell(handle, row, col, text) -> void
    if (funcName == "gridSetCell" && node->getArgs().size() >= 4) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *row = visit(node->getArgs()[1].get());
        auto *col = visit(node->getArgs()[2].get());
        auto *text = visit(node->getArgs()[3].get());
        if (!handle || !row || !col || !text) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_grid_set_cell"),
                             {handle, row, col, text});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // gridGetCell(handle, row, col) -> string
    if (funcName == "gridGetCell" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *row = visit(node->getArgs()[1].get());
        auto *col = visit(node->getArgs()[2].get());
        if (!handle || !row || !col) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_grid_get_cell"),
                                    {handle, row, col}, "ui.gcell");
    }
    // gridSetColLabel(handle, col, text) -> void
    if (funcName == "gridSetColLabel" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *col = visit(node->getArgs()[1].get());
        auto *text = visit(node->getArgs()[2].get());
        if (!handle || !col || !text) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_grid_set_col_label"),
                             {handle, col, text});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
```

- [ ] **Step 3e: Register builtin names**

`src/Sema/ModuleLoader.cpp` Faz 3 satırlarını güncelle:

```cpp
         "createSpinCtrl", "createDatePicker", "dateGetValue",
         "createComboBox", "comboAddItem",
         "createTreeView", "treeAddRoot", "treeAddNode", "treeGetSelection",
         "createDataGrid", "gridSetCell", "gridGetCell", "gridSetColLabel"});
```

- [ ] **Step 3f: Add widget class**

`stdlib/ui/widgets.liva` Faz 3 bölümüne (TreeView sınıfından sonra) ekle:

```liva
// ── DataGrid (wxGrid — hücre = row/col) ────────────────────────
pub class DataGrid: Control {
    init(parent: Control, rows: i32, cols: i32) {
        super.init(createDataGrid(parent.handle, rows, cols))
    }
    pub func setCell(row: i32, col: i32, text: string) { gridSetCell(self.handle, row, col, text) }
    pub func getCell(row: i32, col: i32) -> string { return gridGetCell(self.handle, row, col) }
    pub func setColLabel(col: i32, text: string) { gridSetColLabel(self.handle, col, text) }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.DataGridCompiles" --output-on-failure`
Expected: PASS.

> wxGrid link riski: `wx_libs.cfg`, `wx*.lib` glob'uyla üretildiğinden ayrı bir `wxmsw*_grid.lib` (varsa) zaten otomatik dâhil edilir. `cmake --build` bu adımda `wx_runtime.cpp`'yi grid include'larıyla derler; başarısız olursa link/include sorunu burada görünür. Tam `.exe` link doğrulaması Task 7'de.

- [ ] **Step 5: Commit**

```bash
git add stdlib/ui/wx_runtime.h stdlib/ui/wx_runtime.cpp src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/ModuleLoader.cpp stdlib/ui/widgets.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase3: DataGrid widget (wxGrid + setCell/getCell/setColLabel/onChange)"
```

---

## Task 6: Splitter (wxSplitterWindow — iki Control'ü split eder)

Yeni `create` + `split_v`/`split_h` (iki child window'u böler) + `set_sash`.

**Files:**
- Test: `tests/unit/UICodegenExecTest.cpp`
- Modify: `stdlib/ui/wx_runtime.h`, `stdlib/ui/wx_runtime.cpp`, `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/ModuleLoader.cpp`, `stdlib/ui/widgets.liva`

- [ ] **Step 1: Write the failing test**

`tests/unit/UICodegenExecTest.cpp` içinde, `DataGridCompiles`'dan sonra ekle:

```cpp
TEST(UICodegenExec, SplitterCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let sp = Splitter(win)\n"
        "  let left = Panel(sp)\n"
        "  let right = Panel(sp)\n"
        "  sp.splitVertically(left, right)\n"
        "  sp.setSashPosition(150)\n"
        "}\n",
        "splitter");
    EXPECT_TRUE(emitsClean(ir));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.SplitterCompiles" --output-on-failure`
Expected: FAIL — `no IR emitted`.

- [ ] **Step 3a: Add runtime FFI prototypes**

`stdlib/ui/wx_runtime.h` Faz 3 bölümüne (DataGrid satırlarından sonra) ekle:

```c
int32_t  liva_ui_create_splitter(int32_t parent);
void     liva_ui_splitter_split_v(int32_t handle, int32_t left, int32_t right);
void     liva_ui_splitter_split_h(int32_t handle, int32_t top, int32_t bottom);
void     liva_ui_splitter_set_sash(int32_t handle, int32_t px);
```

- [ ] **Step 3b: Add runtime implementation**

`stdlib/ui/wx_runtime.cpp` Faz 3 bölümüne (DataGrid fonksiyonlarından sonra) ekle:

```cpp
int32_t liva_ui_create_splitter(int32_t parent) {
    auto *p = getHandle<wxWindow>(parent);
    auto *sp = new wxSplitterWindow(p, wxID_ANY);
    return allocHandle(sp);
}

void liva_ui_splitter_split_v(int32_t handle, int32_t left, int32_t right) {
    auto *sp = getHandle<wxSplitterWindow>(handle);
    auto *l = getHandle<wxWindow>(left);
    auto *r = getHandle<wxWindow>(right);
    if (sp && l && r) sp->SplitVertically(l, r);
}

void liva_ui_splitter_split_h(int32_t handle, int32_t top, int32_t bottom) {
    auto *sp = getHandle<wxSplitterWindow>(handle);
    auto *t = getHandle<wxWindow>(top);
    auto *b = getHandle<wxWindow>(bottom);
    if (sp && t && b) sp->SplitHorizontally(t, b);
}

void liva_ui_splitter_set_sash(int32_t handle, int32_t px) {
    if (auto *sp = getHandle<wxSplitterWindow>(handle))
        sp->SetSashPosition(px);
}
```

- [ ] **Step 3c: Add IRGen externs**

`src/IR/IRGen.cpp` Faz 3 bloğuna (DataGrid extern'lerinden sonra) ekle:

```cpp
    // Splitter: create(i32)->i32, split_v/h(i32,i32,i32)->void, set_sash(i32,i32)->void
    module_->getOrInsertFunction("liva_ui_create_splitter", uiCreateParentTy);
    module_->getOrInsertFunction("liva_ui_splitter_split_v", uiSetSizeTy);
    module_->getOrInsertFunction("liva_ui_splitter_split_h", uiSetSizeTy);
    module_->getOrInsertFunction("liva_ui_splitter_set_sash", uiSetValTy);
```

> `uiSetSizeTy` = `(i32,i32,i32)->void` (satır ~1437), `uiSetValTy` = `(i32,i32)->void` (satır ~1426) — ikisi de kapsamda.

- [ ] **Step 3d: Add IRGenCall intrinsics**

`src/IR/IRGenCall.cpp` Faz 3 bloğuna (gridSetColLabel'dan sonra) ekle:

```cpp
    // createSplitter(parent) -> i32
    if (funcName == "createSplitter" && node->getArgs().size() >= 1) {
        auto *parent = visit(node->getArgs()[0].get());
        if (!parent) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_splitter"),
                                    {parent}, "ui.split");
    }
    // splitterSplitV(handle, left, right) -> void
    if (funcName == "splitterSplitV" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *left = visit(node->getArgs()[1].get());
        auto *right = visit(node->getArgs()[2].get());
        if (!handle || !left || !right) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_splitter_split_v"),
                             {handle, left, right});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // splitterSplitH(handle, top, bottom) -> void
    if (funcName == "splitterSplitH" && node->getArgs().size() >= 3) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *top = visit(node->getArgs()[1].get());
        auto *bottom = visit(node->getArgs()[2].get());
        if (!handle || !top || !bottom) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_splitter_split_h"),
                             {handle, top, bottom});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // splitterSetSash(handle, px) -> void
    if (funcName == "splitterSetSash" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *px = visit(node->getArgs()[1].get());
        if (!handle || !px) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_splitter_set_sash"), {handle, px});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
```

- [ ] **Step 3e: Register builtin names**

`src/Sema/ModuleLoader.cpp` Faz 3 satırlarını güncelle:

```cpp
         "createSpinCtrl", "createDatePicker", "dateGetValue",
         "createComboBox", "comboAddItem",
         "createTreeView", "treeAddRoot", "treeAddNode", "treeGetSelection",
         "createDataGrid", "gridSetCell", "gridGetCell", "gridSetColLabel",
         "createSplitter", "splitterSplitV", "splitterSplitH", "splitterSetSash"});
```

- [ ] **Step 3f: Add widget class**

`stdlib/ui/widgets.liva` Faz 3 bölümüne (DataGrid sınıfından sonra) ekle:

```liva
// ── Splitter (wxSplitterWindow — iki Control'ü split eder) ─────
pub class Splitter: Control {
    init(parent: Control) { super.init(createSplitter(parent.handle)) }
    pub func splitVertically(left: Control, right: Control) {
        splitterSplitV(self.handle, left.handle, right.handle)
    }
    pub func splitHorizontally(top: Control, bottom: Control) {
        splitterSplitH(self.handle, top.handle, bottom.handle)
    }
    pub func setSashPosition(px: i32) { splitterSetSash(self.handle, px) }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.SplitterCompiles" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add stdlib/ui/wx_runtime.h stdlib/ui/wx_runtime.cpp src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/ModuleLoader.cpp stdlib/ui/widgets.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase3: Splitter widget (wxSplitterWindow + splitVertically/Horizontally/setSashPosition)"
```

---

## Task 7: Birleşik örnek + tam regresyon + GUI doğrulama

Altı widget'ı tek pencerede gösteren örnek; birleşik emit-IR testi; tam test paketi yeşil; ve wx-kurulu makinede `.exe` GUI teyidi.

**Files:**
- Create: `examples/ui_widgets_advanced.liva`
- Test: `tests/unit/UICodegenExecTest.cpp`

- [ ] **Step 1: Write the failing combined test**

`tests/unit/UICodegenExecTest.cpp` içinde, `SplitterCompiles`'dan sonra ekle:

```cpp
TEST(UICodegenExec, AdvancedWidgetsExampleCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(700, 500, \"Gelismis Widgetlar\")\n"
        "  let sp = Splitter(win)\n"
        "  let tree = TreeView(sp)\n"
        "  let root = tree.addRoot(\"Proje\")\n"
        "  let src = tree.addNode(root, \"src\")\n"
        "  tree.addNode(src, \"main.liva\")\n"
        "  tree.onSelect(|_h: i32| { })\n"
        "  let grid = DataGrid(sp, 5, 3)\n"
        "  grid.setColLabel(0, \"Ad\")\n"
        "  grid.setCell(0, 0, \"Ali\")\n"
        "  sp.splitVertically(tree, grid)\n"
        "  sp.setSashPosition(220)\n"
        "  let sc = SpinCtrl(win, 0, 100, 5)\n"
        "  let dp = DatePicker(win)\n"
        "  let cmb = ComboBox(win, \"sec\")\n"
        "  cmb.addItem(\"bir\")\n"
        "  win.show()\n"
        "  appRun()\n"
        "}\n",
        "advanced_widgets");
    EXPECT_TRUE(emitsClean(ir));
}
```

- [ ] **Step 2: Run test to verify it fails (or passes if widgets already integrated)**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.AdvancedWidgetsExampleCompiles" --output-on-failure`
Expected: Task 1-6 tamamlandıysa PASS olabilir; bu test tüm widget'ların BİR ARADA derlendiğini doğrular (split içinde TreeView+DataGrid akışı). Kırmızı kalırsa eksik entegrasyonu işaret eder.

- [ ] **Step 3: Create the example file**

`examples/ui_widgets_advanced.liva` oluştur:

```liva
// Faz 3 — Gelişmiş widget'lar tek pencerede.
// TreeView + DataGrid bir Splitter içinde; SpinCtrl/DatePicker/ComboBox alt panelde.
//
// Splitter kuralı: bölünecek panellerin parent'ı Splitter olmalı
// (tree ve grid burada Splitter'ın çocuklarıdır).
import ui::widgets

func main() {
    appInit()
    let win = Window(720, 520, "Liva — Gelişmiş Widget'lar")

    // Üst alan: TreeView | DataGrid (dikey bölünmüş)
    let sp = Splitter(win)

    let tree = TreeView(sp)
    let root = tree.addRoot("Proje")
    let src = tree.addNode(root, "src")
    tree.addNode(src, "main.liva")
    tree.addNode(src, "ui.liva")
    let docs = tree.addNode(root, "docs")
    tree.addNode(docs, "README.md")
    tree.onSelect(|_h: i32| {
        let sel = tree.getSelection()
        println(sel)
    })

    let grid = DataGrid(sp, 6, 3)
    grid.setColLabel(0, "Ad")
    grid.setColLabel(1, "Tür")
    grid.setColLabel(2, "Boyut")
    grid.setCell(0, 0, "main.liva")
    grid.setCell(0, 1, "kaynak")
    grid.setCell(0, 2, "2 KB")
    grid.onChange(|_h: i32| { println(42) })

    sp.splitVertically(tree, grid)
    sp.setSashPosition(240)

    // Alt form alanı
    let form = Panel(win)
    let spin = SpinCtrl(form, 0, 100, 10)
    spin.setBounds(10, 10, 80, 28)
    spin.onChange(|_h: i32| { println(spin.getValue()) })

    let date = DatePicker(form)
    date.setBounds(100, 10, 140, 28)

    let combo = ComboBox(form, "Seçiniz")
    combo.addItem("Düşük")
    combo.addItem("Orta")
    combo.addItem("Yüksek")
    combo.setBounds(250, 10, 140, 28)
    combo.onSelect(|_h: i32| { println(combo.getText()) })

    win.show()
    appRun()
}
```

- [ ] **Step 4: Run the full regression suite**

Run: `cmake --build build-clang && ctest --test-dir build-clang --output-on-failure`
Expected: TÜM testler PASS (mevcut 2298 + Faz 3'te eklenen ≥8 test). Yeni toplam ≥2306, %100 yeşil.

- [ ] **Step 5: GUI doğrulama (wx-kurulu makinede — manuel)**

> Bu adım, kullanıcının wx-kurulu ortamında çalıştırılır. Otomatik test edilemez (headless ortamda pencere açılamaz).

Örneği gerçek `.exe`'ye derle ve çalıştır:

```bash
build-clang/livac.exe examples/ui_widgets_advanced.liva -o examples/ui_widgets_advanced.exe
examples/ui_widgets_advanced.exe
```

Doğrula:
- Program LİNKLENİR (wxGrid sembolleri çözülür — `wx_libs.cfg`'nin `wx*.lib` glob'u grid lib'ini kapsar; LNK2019 çıkarsa CMakeLists `wx_libs.cfg` üretimine `"${_wx_lib_dir}/wxmsw*_grid.lib"` glob'u eklenir ve `cmake -B build-clang` yeniden koşturulur).
- Pencere açılır; sol/sağ bölünmüş alan görünür; ağaç düğümleri genişler; grid hücreleri/başlıkları görünür; spin/date/combo etkileşir.
- Assert/crash yok.

- [ ] **Step 6: Commit**

```bash
git add examples/ui_widgets_advanced.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase3: combined advanced-widgets example + regression test"
```

---

## Tamamlama

Altı widget tamamlandığında `superpowers:finishing-a-development-branch` ile entegrasyon (master'a merge / PR) seçeneklerini sun. Branch: `feat/ui-vcl-phase3`.
