# Faz 1 — VCL Benzeri UI Kütüphanesi (Sınıf Tabanlı Temel) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** wxWidgets tabanlı UI kütüphanesini, Delphi VCL'in `TControl` modeline yakın, sınıf tabanlı ve tip-güvenli bir Liva API'sine (`Button(panel,"x")`, `btn.onClick(...)`) dönüştürmek; callback dangling riskini "widget'a bağlı heap-env" ile gidermek; `onKey` keycode hatasını düzeltmek.

**Architecture:** Üç katman korunur — kullanıcı `ui::widgets` sınıflarını kullanır; sınıf gövdeleri dahili `liva_ui_*` intrinsic'lerini çağırır; bunlar `wx_runtime.cpp`'ye gider. Event metotları stdlib'de gerçek gövdeyle tanımlanır (Sema için), IRGen literal closure argümanını yakaladığında env'i runtime'a `size` ile geçirip heap-kopya/free yolunu tetikler. `malloc/memcpy/free` runtime'da yapılır; IRGen yalnız `size` argümanı ekler.

**Tech Stack:** C++20, LLVM 21, wxWidgets, CMake/Ninja, GoogleTest. Build: `build_clang.bat`. Test: `ctest --test-dir build-clang --output-on-failure`.

**Referans tasarım:** `docs/superpowers/specs/2026-05-31-ui-vcl-phase1-design.md`

---

## Dosya Haritası

| Dosya | Sorumluluk | Eylem |
|-------|-----------|-------|
| `stdlib/ui/wx_runtime.h` | FFI imzaları | Modify — callback fns `env+size`, `set_bounds`, onKey keycode |
| `stdlib/ui/wx_runtime.cpp` | wxWidgets runtime | Modify — per-widget env sahipliği, heap-kopya/free, keycode |
| `src/IR/IRGen.cpp` | `getClosureObjTy`, closure env | Modify — `lastClosureEnvSize_` kaydı |
| `src/IR/IRGenExpr.cpp` | `visitClosureExpr` | Modify — env boyutunu `lastClosureEnvSize_`'a yaz |
| `src/IR/IRGenCall.cpp` | UI intrinsic eşleme | Modify — `emitCallbackCall` 4-arg; Control-alıcı event-metot hızlı yolu; `setBounds` |
| `stdlib/ui/widgets.liva` | Genel API | Rewrite — `class Control` + widget alt sınıfları |
| `stdlib/ui/layout.liva` | Sizer sarmalayıcılar | Modify — `Control` parametreleri |
| `stdlib/ui/composite.liva` | Bileşik widget'lar | Modify — sınıf API |
| `stdlib/ui/theme.liva` | Tema | Modify — `Control` alır |
| `stdlib/ui/router.liva` | Sayfa yönlendirme | Modify — sınıf API |
| `stdlib/ui/listview.liva` | ListView | Modify — sınıf API |
| `stdlib/ui/tooltip.liva` | Tooltip | Modify — `Control` alır |
| `examples/ui_*.liva` | Örnekler | Rewrite — sınıf API + `buildCounter` demo |
| `tests/unit/UIModuleTest.cpp` | Sema testleri | Modify — sınıf API + onKey + buildCounter |
| `tests/unit/RuntimeExecTest.cpp` | Derle-çalıştır | Modify — saf-fonksiyon (types/animation) testleri |

**Pragmatik karar (modül gizleme):** `std::ui` builtin modülü (ham adlar) dahili FFI olarak kalır; sınıf gövdeleri `import std::ui` ile bunları çağırır. Kullanıcı-yüzeyinde "tek yol" = `ui::widgets` sınıfları; örnekler/dokümanlar yalnız sınıf API kullanır. `std::ui`'yi `ui::ffi`'ye yeniden adlandırıp ham adları tamamen gizlemek (link tespiti + umbrella + tüm import'lar) Faz 1 sonrası opsiyonel iş olarak bırakıldı (risk/değer dengesi).

---

## Task 1: Runtime — env sahipliği, heap-kopya/free, onKey keycode, setBounds

**Files:**
- Modify: `stdlib/ui/wx_runtime.h`
- Modify: `stdlib/ui/wx_runtime.cpp`

Bu görev pür C++; Liva derleyici testiyle doğrulanamaz, `liva_ui` kütüphanesinin **derlenmesiyle** doğrulanır.

- [ ] **Step 1: FFI imzalarını güncelle (`wx_runtime.h`)**

`wx_runtime.h` içinde callback bind imzalarını `env`'den sonra `int32_t size` alacak şekilde değiştir ve onKey'i ayrı tut, `set_bounds` ekle:

```c
/* ── Events (closure callbacks) — env + size for heap-owned envs ──── */
void     liva_ui_on_click(int32_t handle, void *func, void *env, int32_t size);
void     liva_ui_on_change(int32_t handle, void *func, void *env, int32_t size);
void     liva_ui_on_select(int32_t handle, void *func, void *env, int32_t size);
void     liva_ui_on_key(int32_t handle, void *func, void *env, int32_t size);
void     liva_ui_window_on_close(int32_t handle, void *func, void *env, int32_t size);
void     liva_ui_canvas_on_paint(int32_t handle, void *func, void *env, int32_t size);

/* ── Geometry ──────────────────────────────────────────────────────── */
void     liva_ui_set_bounds(int32_t handle, int32_t x, int32_t y, int32_t w, int32_t h);
```

- [ ] **Step 2: env sahiplik altyapısını ekle (`wx_runtime.cpp`)**

Handle tablosu bölümünün altına, widget→sahipli-env eşlemesi ve ortak bir "env'i sahiplen" yardımcısı ekle:

```cpp
// Widget handle -> heap-owned closure env pointers (freed on widget destroy)
static std::unordered_map<int32_t, std::vector<void *>> g_widgetEnvs;

// If size > 0, heap-copy the (stack) env and register it for free-on-destroy.
// Returns the pointer the callback lambda should capture (heap copy or original).
static void *ownEnv(int32_t widgetHandle, void *env, int32_t size) {
    if (size <= 0 || env == nullptr) return env;   // non-literal / no-capture: keep as-is
    void *heap = std::malloc(static_cast<size_t>(size));
    std::memcpy(heap, env, static_cast<size_t>(size));
    g_widgetEnvs[widgetHandle].push_back(heap);
    return heap;
}

static void freeWidgetEnvs(int32_t widgetHandle) {
    auto it = g_widgetEnvs.find(widgetHandle);
    if (it == g_widgetEnvs.end()) return;
    for (void *p : it->second) std::free(p);
    g_widgetEnvs.erase(it);
}
```

- [ ] **Step 3: callback bind fonksiyonlarını `env+size` ile güncelle**

`liva_ui_on_click` (ve _on_change/_on_select aynı kalıpla). Mevcut imzayı değiştir, `ownEnv` ile env'i sahiplen, lambda sahiplenmiş pointer'ı yakalasın:

```cpp
void liva_ui_on_click(int32_t handle, void *func, void *env, int32_t size) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w || !func) return;
    void *owned = ownEnv(handle, env, size);
    LivaCallback cb{(LivaCallbackFn)func, owned, handle};
    if (dynamic_cast<wxButton *>(w))
        w->Bind(wxEVT_BUTTON, [cb](wxCommandEvent &) { cb.invoke(); });
    else if (dynamic_cast<wxCheckBox *>(w))
        w->Bind(wxEVT_CHECKBOX, [cb](wxCommandEvent &) { cb.invoke(); });
    else
        w->Bind(wxEVT_LEFT_UP, [cb](wxMouseEvent &) { cb.invoke(); });
}
```

`liva_ui_on_change`, `liva_ui_on_select`, `liva_ui_window_on_close`, `liva_ui_canvas_on_paint` aynı şekilde: imzaya `int32_t size` ekle, `void *owned = ownEnv(handle, env, size);` çağır, `LivaCallback`/`LivaPaintFn` env'ini `owned` yap.

- [ ] **Step 4: onKey'i keycode geçirecek şekilde düzelt**

`LivaCallback` yanına keycode'lu çağrı tipi ekle (dosyanın callback bölümünde):

```cpp
using LivaKeyFn = void (*)(void *env, int32_t handle, int32_t keycode);
```

`liva_ui_on_key`'i değiştir:

```cpp
void liva_ui_on_key(int32_t handle, void *func, void *env, int32_t size) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w || !func) return;
    void *owned = ownEnv(handle, env, size);
    auto fn = (LivaKeyFn)func;
    w->Bind(wxEVT_KEY_DOWN, [fn, owned, handle](wxKeyEvent &evt) {
        if (fn) fn(owned, handle, evt.GetKeyCode());
        evt.Skip();
    });
}
```

- [ ] **Step 5: set_bounds ekle ve destroy'da env'leri serbest bırak**

Geometry bölümüne ekle:

```cpp
void liva_ui_set_bounds(int32_t handle, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (auto *win = getHandle<wxWindow>(handle))
        win->SetSize(x, y, w, h);
}
```

`liva_ui_destroy_widget` gövdesinin başına ekle (widget yok edilmeden önce env'leri serbest bırak):

```cpp
    freeWidgetEnvs(handle);
```

Üst-pencere kapanışında alt ağaç da yok olur; en azından `destroyWidget` ve pencere `on_close` yolunda `freeWidgetEnvs` çağrıldığından emin ol (window on_close handler'ında, frame Destroy edilmeden önce `freeWidgetEnvs(handle)`).

- [ ] **Step 6: liva_ui kütüphanesini derle**

Run: `build_clang.bat`
Expected: `liva_ui` ve tüm hedefler hatasız derlenir (link aşaması dahil). Hata yoksa devam.

- [ ] **Step 7: Commit**

```bash
git add stdlib/ui/wx_runtime.h stdlib/ui/wx_runtime.cpp
git commit -m "ui-runtime: per-widget heap-owned callback envs + onKey keycode + set_bounds"
```

---

## Task 2: stdlib — `Control` taban sınıfı + widget alt sınıfları

**Files:**
- Rewrite: `stdlib/ui/widgets.liva`
- Test: `tests/unit/UIModuleTest.cpp`

Bu görev sonunda sınıf API'si Sema'dan geçer ve callback'ler bugünküyle aynı güvenlik seviyesinde (stack-env method gövdesi) çalışır. Heap-env hızlı yolu Task 3'te eklenir.

- [ ] **Step 1: Başarısız Sema testini yaz**

`tests/unit/UIModuleTest.cpp` sonuna ekle:

```cpp
TEST_F(UIModuleTest, ControlClassHierarchy) {
    auto r = check(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let panel = Panel(win)\n"
        "  let btn = Button(panel, \"Tikla\")\n"
        "  btn.setEnabled(true)\n"
        "  btn.setBounds(0, 0, 100, 30)\n"
        "  let kids: [dyn Control] = [btn, Label(panel, \"x\")]\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "Control-based class hierarchy should type-check";
}
```

- [ ] **Step 2: Testin başarısız olduğunu doğrula**

Run: `ctest --test-dir build-clang -R UIModuleTest.ControlClassHierarchy --output-on-failure`
Expected: FAIL (Window/Button/Control henüz sınıf değil).

- [ ] **Step 3: `widgets.liva`'yı sınıf hiyerarşisiyle yeniden yaz**

`stdlib/ui/widgets.liva` içeriğini şu yapıyla değiştir (tüm widget'lar için aynı kalıp; kısalık için temsilciler gösterildi, hepsini doldur):

```liva
// std::ui widgets — VCL-style class hierarchy over wxWidgets
// Import with: import ui::widgets
import std::ui
import ui::types

// ── Control: taban sınıf (VCL TControl) ────────────────────────
pub class Control {
    var handle: i32
    init(h: i32) { self.handle = h }

    pub func setEnabled(b: bool) {
        var v = 0
        if b { v = 1 }
        setEnabled(self.handle, v)
    }
    pub func setVisible(b: bool) {
        var v = 0
        if b { v = 1 }
        setVisible(self.handle, v)
    }
    pub func setBounds(x: i32, y: i32, w: i32, h: i32) {
        setBounds(self.handle, x, y, w, h)
    }
    pub func setSize(w: i32, h: i32) { setWidgetSize(self.handle, w, h) }
    pub func setTooltip(t: string) { setTooltip(self.handle, t) }
    pub func setFont(size: i32, bold: bool) {
        var v = 0
        if bold { v = 1 }
        setWidgetFont(self.handle, size, v)
    }
    pub func setFgColor(r: i32, g: i32, b: i32) { setFgColor(self.handle, r, g, b) }
    pub func setBgColor(r: i32, g: i32, b: i32) { setBgColor(self.handle, r, g, b) }

    // Event metotları — gerçek gövde (non-literal fallback / Sema).
    // IRGen, alıcı Control-türeviyse ve argüman closure LITERALI ise
    // bu çağrıyı yakalayıp heap-env hızlı yoluna sapar (Task 3).
    pub func onClick(cb: (i32) -> void) { onClick(self.handle, cb) }
    pub func onChange(cb: (i32) -> void) { onChange(self.handle, cb) }
    pub func onSelect(cb: (i32) -> void) { onSelect(self.handle, cb) }
    pub func onKey(cb: (i32, i32) -> void) { onKey(self.handle, cb) }
}

// ── Window (wxFrame) ───────────────────────────────────────────
pub class Window: Control {
    init(w: i32, h: i32, title: string) { super.init(createWindow(w, h, title)) }
    pub func show() { windowShow(self.handle, 1) }
    pub func hide() { windowShow(self.handle, 0) }
    pub func setTitle(title: string) { windowSetTitle(self.handle, title) }
    pub func getWidth() -> i32 { return windowGetWidth(self.handle) }
    pub func getHeight() -> i32 { return windowGetHeight(self.handle) }
    pub func onClose(cb: (i32) -> void) { windowOnClose(self.handle, cb) }
    pub func setSizerHandle(sizerH: i32) { setSizer(self.handle, sizerH) }
}

// ── Panel ──────────────────────────────────────────────────────
pub class Panel: Control {
    init(parent: Control) { super.init(createPanel(parent.handle)) }
    pub func setSizerHandle(sizerH: i32) { setSizer(self.handle, sizerH) }
}

// ── Button ─────────────────────────────────────────────────────
pub class Button: Control {
    init(parent: Control, label: string) { super.init(createButton(parent.handle, label)) }
    pub func setText(t: string) { setText(self.handle, t) }
}

// ── Label ──────────────────────────────────────────────────────
pub class Label: Control {
    init(parent: Control, text: string) { super.init(createLabel(parent.handle, text)) }
    pub func setText(t: string) { setText(self.handle, t) }
}

// ── TextInput ──────────────────────────────────────────────────
pub class TextInput: Control {
    init(parent: Control, value: string) { super.init(createTextInput(parent.handle, value)) }
    pub func getText() -> string { return getText(self.handle) }
    pub func setText(t: string) { setText(self.handle, t) }
}

// ── Checkbox ───────────────────────────────────────────────────
pub class Checkbox: Control {
    init(parent: Control, label: string) { super.init(createCheckbox(parent.handle, label)) }
    pub func getValue() -> i32 { return getValue(self.handle) }
    pub func setValue(v: i32) { setValue(self.handle, v) }
}

// ── Slider ─────────────────────────────────────────────────────
pub class Slider: Control {
    init(parent: Control, minVal: i32, maxVal: i32, val: i32) {
        super.init(createSlider(parent.handle, minVal, maxVal, val))
    }
    pub func getValue() -> i32 { return getValue(self.handle) }
    pub func setValue(v: i32) { setValue(self.handle, v) }
}

// ── ProgressBar ────────────────────────────────────────────────
pub class ProgressBar: Control {
    init(parent: Control, range: i32) { super.init(createProgressBar(parent.handle, range)) }
    pub func getValue() -> i32 { return getValue(self.handle) }
    pub func setValue(v: i32) { setValue(self.handle, v) }
}

// ── RadioGroup ─────────────────────────────────────────────────
pub class RadioGroup: Control {
    init(parent: Control, choices: string) { super.init(createRadioGroup(parent.handle, choices)) }
    pub func getSelection() -> i32 { return getValue(self.handle) }
}

// ── Dropdown ───────────────────────────────────────────────────
pub class Dropdown: Control {
    init(parent: Control, choices: string) { super.init(createDropdown(parent.handle, choices)) }
    pub func getSelection() -> i32 { return getValue(self.handle) }
}

// ── TextArea ───────────────────────────────────────────────────
pub class TextArea: Control {
    init(parent: Control, value: string) { super.init(createTextArea(parent.handle, value)) }
    pub func getText() -> string { return getText(self.handle) }
    pub func setText(t: string) { setText(self.handle, t) }
}

// ── ListBox ────────────────────────────────────────────────────
pub class ListBox: Control {
    init(parent: Control) { super.init(createListBox(parent.handle)) }
    pub func addItem(item: string) { listAddItem(self.handle, item) }
    pub func clear() { listClear(self.handle) }
    pub func getSelection() -> i32 { return listGetSelection(self.handle) }
}

// ── TabView ────────────────────────────────────────────────────
pub class TabView: Control {
    init(parent: Control) { super.init(createTabView(parent.handle)) }
    pub func addPage(page: Control, title: string) { tabAddPage(self.handle, page.handle, title) }
    pub func getSelection() -> i32 { return tabGetSelection(self.handle) }
}

// ── ScrollView ─────────────────────────────────────────────────
pub class ScrollView: Control {
    init(parent: Control) { super.init(createScrollView(parent.handle)) }
    pub func setSizerHandle(sizerH: i32) { setSizer(self.handle, sizerH) }
}

// ── ImageView ──────────────────────────────────────────────────
pub class ImageView: Control {
    init(parent: Control, path: string) { super.init(createImageView(parent.handle, path)) }
}

// ── Divider ────────────────────────────────────────────────────
pub class Divider: Control {
    init(parent: Control) { super.init(createDivider(parent.handle)) }
}

// ── Canvas ─────────────────────────────────────────────────────
pub class Canvas: Control {
    init(parent: Control) { super.init(createCanvas(parent.handle)) }
    pub func onPaint(cb: (i32) -> void) { canvasOnPaint(self.handle, cb) }
    pub func refresh() { canvasRefresh(self.handle) }
}
```

> Not: `Sizer` struct'ı (handle döndüren layout) `layout.liva` ile birlikte Task 4'te ele alınır; burada widget'lara odaklan. `setBounds` Control metodu içindeki `setBounds(self.handle, ...)` çağrısı, Task 3'te eklenen serbest-fonksiyon intrinsic'ine gider.

- [ ] **Step 4: `setBounds` serbest-fonksiyon intrinsic'ini ekle (geçici, Task 3 ile birleşecek)**

`Control.setBounds` gövdesi `setBounds(handle, x,y,w,h)` serbest fonksiyonunu çağırır; bunun çözülmesi için `ModuleLoader.cpp` `std::ui` listesine `"setBounds"` ekle (Window bölümünün altına) ve `IRGenCall.cpp`'de createWindow yakınına intrinsic ekle:

`src/Sema/ModuleLoader.cpp` (std::ui listesi, "Widget properties" grubuna):
```cpp
         "setBgColor", "setFgColor", "setTooltip", "destroyWidget", "setBounds",
```

`src/IR/IRGenCall.cpp` (örn. `setWidgetSize` intrinsic'inin hemen ardına):
```cpp
    // setBounds(handle, x, y, w, h) -> void
    if (funcName == "setBounds" && node->getArgs().size() >= 5) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *x = visit(node->getArgs()[1].get());
        auto *y = visit(node->getArgs()[2].get());
        auto *w = visit(node->getArgs()[3].get());
        auto *h = visit(node->getArgs()[4].get());
        if (!handle || !x || !y || !w || !h) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_bounds"), {handle, x, y, w, h});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
```

`liva_ui_set_bounds`'un IRGen'de tanıtıldığından emin ol (Task 1'de eklendi; `getOrInsertFunction` kaydı `IRGen.cpp`'deki UI extern kayıtlarına eklenmeli — `liva_ui_create_window` kaydının yanına):
```cpp
    {
        llvm::Type *i32 = llvm::Type::getInt32Ty(*context_);
        auto *fnTy = llvm::FunctionType::get(llvm::Type::getVoidTy(*context_),
                                             {i32, i32, i32, i32, i32}, false);
        module_->getOrInsertFunction("liva_ui_set_bounds", fnTy);
    }
```

- [ ] **Step 5: Build + testi geçir**

Run: `build_clang.bat && ctest --test-dir build-clang -R UIModuleTest.ControlClassHierarchy --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add stdlib/ui/widgets.liva tests/unit/UIModuleTest.cpp src/Sema/ModuleLoader.cpp src/IR/IRGenCall.cpp src/IR/IRGen.cpp
git commit -m "ui: Control base class + widget subclasses (VCL-style hierarchy) + setBounds"
```

---

## Task 3: Compiler — Control-alıcı event metotları için heap-env hızlı yolu

**Files:**
- Modify: `src/IR/IRGen.cpp` (closureObjTy bölümü — `lastClosureEnvSize_` üyesi)
- Modify: `src/IR/IRGenExpr.cpp` (`visitClosureExpr` — boyutu kaydet)
- Modify: `src/IR/IRGenCall.cpp` (`emitCallbackCall` 4-arg; member-call hızlı yolu)
- Modify: ilgili header (`IRGen` sınıf bildirimi) — `lastClosureEnvSize_` alanı
- Test: `tests/unit/UIModuleTest.cpp`

- [ ] **Step 1: `lastClosureEnvSize_` alanını ekle**

`IRGen` sınıf bildiriminde (closure ile ilgili üyelerin yanında, örn. `closureObjTy_`'nin bildirildiği header) ekle:
```cpp
    uint64_t lastClosureEnvSize_ = 0; // env alloc size of the most recently visited closure literal (0 = no captures)
```

- [ ] **Step 2: `visitClosureExpr`'de env boyutunu kaydet**

`src/IR/IRGenExpr.cpp`, `visitClosureExpr` içinde env struct'ı kurulduktan sonra (`envStructTy` set edildiği `if (!captured.empty())` bloğunun sonunda, `envPtr = envAlloca;`'dan hemen önce/sonra):
```cpp
        lastClosureEnvSize_ =
            module_->getDataLayout().getTypeAllocSize(envStructTy);
```
Capture yoksa (`captured.empty()`), fonksiyonun başında `lastClosureEnvSize_ = 0;` set et (erken dönüşlerde de doğru kalsın diye capture analizinden hemen sonra sıfırla, dolu ise üstteki satır günceller).

- [ ] **Step 3: `emitCallbackCall`'u 4-arg FFI'ye geçir**

`src/IR/IRGenCall.cpp:4815` civarı — FFI çağrısına `size=0` ekle (serbest-fonksiyon/non-literal yol; runtime kopyalamaz):
```cpp
        auto *i32 = builder_->getInt32Ty();
        builder_->CreateCall(getOrPanic(cFuncName.c_str()),
                             {handle, funcPtr, envPtr, llvm::ConstantInt::get(i32, 0)});
        return llvm::Constant::getNullValue(i32);
```
Bu, `onClick`/`onChange`/`onSelect`/`windowOnClose`/`canvasOnPaint`/`onKey` serbest çağrılarının hepsini 4-arg yapar (hepsi `emitCallbackCall` kullanıyor).

- [ ] **Step 4: IRGen extern kayıtlarını 4-arg imzaya güncelle**

`IRGen.cpp`'de UI callback extern'leri (`liva_ui_on_click` vb.) `getOrInsertFunction` ile kaydediliyorsa imzalarına `i32` (size) ekle; `liva_ui_on_key` için `{i32, ptr, ptr, i32}` (handle, func, env, size). Kayıt yoksa `getOrPanic` runtime'dan çözer; yine de imza tutarlılığı için kayıtları `{i32, ptr, ptr, i32}` yap.

- [ ] **Step 5: Başarısız testi yaz (buildCounter kalıbı derlenmeli)**

`tests/unit/UIModuleTest.cpp`:
```cpp
TEST_F(UIModuleTest, CallbackInHelperReturns) {
    auto r = check(
        "import ui::widgets\n"
        "func buildCounter(panel: Control) -> Button {\n"
        "  var count = 0\n"
        "  let btn = Button(panel, \"Tikla\")\n"
        "  btn.onClick(|_h: i32| { count = count + 1 })\n"
        "  return btn\n"
        "}\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(300, 200, \"T\")\n"
        "  let panel = Panel(win)\n"
        "  let b = buildCounter(panel)\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "callback bound in a returning helper should type-check";
}

TEST_F(UIModuleTest, OnKeyHasKeycodeParam) {
    auto r = check(
        "import ui::widgets\nimport ui::focus\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(300, 200, \"T\")\n"
        "  let inp = TextInput(win, \"\")\n"
        "  inp.onKey(|_h: i32, key: i32| { if key == KEY_ENTER() { } })\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "onKey callback should accept (handle, keycode)";
}
```

- [ ] **Step 6: Testin başarısız olduğunu doğrula (Sema zaten geçebilir; IRGen yolu eklenir)**

Run: `ctest --test-dir build-clang -R "UIModuleTest.CallbackInHelperReturns|UIModuleTest.OnKeyHasKeycodeParam" --output-on-failure`
Expected: Sema seviyesinde muhtemelen PASS (gövdeler mevcut). Bu testler regresyon koruması; asıl IRGen davranışı Step 7'de eklenir. Eğer FAIL ederse mesajı not et.

- [ ] **Step 7: Member-call heap-env hızlı yolunu ekle**

`src/IR/IRGenCall.cpp`, MemberExpr-call işleme bloğunun ERKEN bir noktasında (normal kullanıcı-metot dispatch'inden ÖNCE; `structTypeName` ve `objAlloca` hesaplandıktan sonra) ekle. Yardımcı: alıcı Control-türevi mi?

```cpp
    // --- Control-receiver event method fast path (heap-owned env) ---
    static const std::set<std::string> kUiEventMethods = {
        "onClick", "onChange", "onSelect", "onKey"};
    auto isControlDescendant = [&](std::string tn) -> bool {
        for (int i = 0; i < 64 && !tn.empty(); ++i) {
            if (tn == "Control") return true;
            auto it = classParent_.find(tn);
            if (it == classParent_.end()) return false;
            tn = it->second;
        }
        return false;
    };
    if (!structTypeName.empty() && kUiEventMethods.count(methodName) &&
        isControlDescendant(structTypeName) &&
        node->getArgs().size() == 1 &&
        node->getArgs()[0]->getKind() == ASTNode::NodeKind::ClosureExpr) {

        // self.handle (i32) — load receiver, GEP field 0
        auto *ctrlTy = structTypes_["Control"];
        llvm::Value *selfPtr = objAlloca;
        if (objAlloca->getAllocatedType()->isPointerTy())
            selfPtr = builder_->CreateLoad(objAlloca->getAllocatedType(), objAlloca, "self.ptr");
        auto *handleGep = builder_->CreateStructGEP(ctrlTy, selfPtr, 0, "handle.gep");
        auto *handle = builder_->CreateLoad(builder_->getInt32Ty(), handleGep, "handle");

        // Visit the closure literal -> fat pointer; capture its env size
        lastClosureEnvSize_ = 0;
        auto *closureVal = visit(node->getArgs()[0].get());
        if (!closureVal) return nullptr;
        uint64_t envSize = lastClosureEnvSize_;

        auto *closureObjTy = getClosureObjTy();
        auto *cbAlloca = createEntryBlockAlloca(
            builder_->GetInsertBlock()->getParent(), "cb.tmp", closureObjTy);
        builder_->CreateStore(closureVal, cbAlloca);
        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
        auto *funcPtr = builder_->CreateLoad(ptrTy, builder_->CreateStructGEP(closureObjTy, cbAlloca, 0));
        auto *envPtr  = builder_->CreateLoad(ptrTy, builder_->CreateStructGEP(closureObjTy, cbAlloca, 1));

        const char *cFn = methodName == "onClick"  ? "liva_ui_on_click"
                        : methodName == "onChange" ? "liva_ui_on_change"
                        : methodName == "onSelect" ? "liva_ui_on_select"
                                                    : "liva_ui_on_key";
        auto *i32 = builder_->getInt32Ty();
        builder_->CreateCall(getOrPanic(cFn),
            {handle, funcPtr, envPtr, llvm::ConstantInt::get(i32, envSize)});
        return llvm::Constant::getNullValue(i32);
    }
    if (!structTypeName.empty() && kUiEventMethods.count(methodName) &&
        isControlDescendant(structTypeName)) {
        // non-literal closure argument bound to a widget — may dangle if the
        // binding scope ends before the widget. Warn and fall through to the
        // normal method body (stack env, size 0).
        diag_.report(node->getLoc(), DiagID::warn_ui_callback_may_dangle);
    }
```

> `DiagID::warn_ui_callback_may_dangle` mevcut diagnostic tablosuna eklenir (kısa mesaj: "callback may dangle if bound outside a long-lived scope; bind inline or in a scope that outlives the widget"). Diagnostic eklemenin tam yeri için `include/liva/Common/Diagnostics*.def` kalıbını izle (mevcut `err_yield_outside_generator` gibi). Eğer diagnostic altyapısına erişim bu görevde zahmetliyse, uyarıyı `// TODO`-suz biçimde `diag_.report(... , DiagID::warn_generic, "callback may dangle...")` gibi mevcut bir genel uyarı kanalıyla ver.

- [ ] **Step 8: Build + testleri geçir**

Run: `build_clang.bat && ctest --test-dir build-clang -R "UIModuleTest" --output-on-failure`
Expected: Tüm UIModuleTest PASS.

- [ ] **Step 9: IR smoke — buildCounter heap-env üretimini doğrula**

`RuntimeExecTest.cpp`'ye, programı **derleyip linkleyen ama appRun çağırmayan** ve düz çıkışla biten bir UI smoke testi ekle (GUI açılmaz, appRun yok → headless güvenli):
```cpp
#ifdef LIVA_HAS_WXWIDGETS
TEST(RuntimeExec, UiClassApiCompilesAndExits) {
    auto r = compileAndRun(
        "import ui::widgets\n"
        "func buildCounter(panel: Control) -> Button {\n"
        "  var count = 0\n"
        "  let btn = Button(panel, \"x\")\n"
        "  btn.onClick(|_h: i32| { count = count + 1 })\n"
        "  return btn\n"
        "}\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(200, 100, \"T\")\n"
        "  let panel = Panel(win)\n"
        "  let b = buildCounter(panel)\n"
        "  println(\"ok\")\n"
        "}\n",
        "ui_class_api");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("ok"), std::string::npos);
}
#endif
```
> Bu test heap-env malloc/memcpy/free yolunu çalıştırır (callback hiç tetiklenmese de env sahiplenip program çıkışında serbest bırakılır). Headless ortamda wxApp init başarısız olursa testi `#ifdef`-guard'lı bırak ve README'ye not düş; kullanıcı Windows masaüstünde çalıştırır.

- [ ] **Step 10: Commit**

```bash
git add src/IR/IRGen.cpp src/IR/IRGenExpr.cpp src/IR/IRGenCall.cpp tests/unit/UIModuleTest.cpp tests/unit/RuntimeExecTest.cpp
git commit -m "irgen: heap-owned env fast path for Control event methods (fixes callback dangling)"
```

---

## Task 4: stdlib — yardımcı modülleri sınıf API'sine taşı

**Files:**
- Modify: `stdlib/ui/layout.liva`, `stdlib/ui/composite.liva`, `stdlib/ui/theme.liva`, `stdlib/ui/router.liva`, `stdlib/ui/listview.liva`, `stdlib/ui/tooltip.liva`
- Test: `tests/unit/UIModuleTest.cpp`

- [ ] **Step 1: Başarısız testi yaz**

```cpp
TEST_F(UIModuleTest, HelperModulesClassApi) {
    auto r = check(
        "import ui::widgets\nimport ui::theme\nimport ui::tooltip\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(300, 200, \"T\")\n"
        "  let panel = Panel(win)\n"
        "  let theme = Theme.dark()\n"
        "  theme.applyToWidget(panel)\n"
        "  applyTooltip(panel, \"ipucu\")\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "helper modules should accept Control instances";
}
```

- [ ] **Step 2: Testin başarısız olduğunu doğrula**

Run: `ctest --test-dir build-clang -R UIModuleTest.HelperModulesClassApi --output-on-failure`
Expected: FAIL (`applyToWidget`/`applyTooltip` `i32` bekliyor, `Control` verildi).

- [ ] **Step 3: `theme.liva` — `Control` al**

`applyToPanel`/`applyToWidget` imzalarını değiştir:
```liva
    pub func applyToPanel(ref self, c: Control) {
        c.setBgColor(self.bgR, self.bgG, self.bgB)
        c.setFgColor(self.fgR, self.fgG, self.fgB)
    }
    pub func applyToWidget(ref self, c: Control) {
        c.setFgColor(self.fgR, self.fgG, self.fgB)
    }
```
Dosya başına `import ui::widgets` ekle (Control için).

- [ ] **Step 4: `tooltip.liva` — `Control` al**

```liva
import ui::widgets
pub func applyTooltip(c: Control, text: string) {
    c.setTooltip(text)
}
```

- [ ] **Step 5: `composite.liva`, `router.liva`, `listview.liva` — sınıf API**

`composite.liva`: `FormField.new(parent: Control, ...)`, `ButtonBar.addButton(parent: Control, ...)`, `StatusText.new(parent: Control, ...)` — `parent.handle` ile dahili `create*` çağır; widget'ları `Control` döndür/sakla. `router.liva`: `Router.new(parent: Control)`, `addPage(page: Control, title)` → `tabAddPage(self.tabHandle, page.handle, title)`. `listview.liva`: `ListView.new(parent: Control)`. Her dosyada `import ui::widgets` olduğundan emin ol.

`composite.liva` örnek dönüşüm (FormField):
```liva
import ui::widgets
import ui::types
pub class FormField {
    var label: Label
    var input: TextInput
    init(parent: Control, labelText: string, placeholder: string) {
        self.label = Label(parent, labelText)
        self.input = TextInput(parent, placeholder)
    }
    pub func getText() -> string { return self.input.getText() }
    pub func setText(t: string) { self.input.setText(t) }
    pub func onChange(cb: (i32) -> void) { self.input.onChange(cb) }
}
```

- [ ] **Step 6: `layout.liva` — `Control` kabul et**

`VStack`/`HStack`/`Grid`/`FlexGrid` `add` metotları `widget: i32` yerine `widget: Control` alsın; gövdede `widget.handle` kullan:
```liva
    pub func add(ref self, widget: Control, proportion: i32, border: i32) {
        sizerAdd(self.handle, widget.handle, proportion, WX_EXPAND(), border)
    }
```
Dosya başına `import ui::widgets` ekle. (Sizer handle'ları hâlâ `i32`; layout sınıfları Control değil — yalnız `add` parametreleri tipli olur.)

- [ ] **Step 7: Build + test geçir**

Run: `build_clang.bat && ctest --test-dir build-clang -R UIModuleTest.HelperModulesClassApi --output-on-failure`
Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add stdlib/ui/layout.liva stdlib/ui/composite.liva stdlib/ui/theme.liva stdlib/ui/router.liva stdlib/ui/listview.liva stdlib/ui/tooltip.liva tests/unit/UIModuleTest.cpp
git commit -m "ui: migrate helper modules (layout/composite/theme/router/listview/tooltip) to class API"
```

---

## Task 5: Örnekleri sınıf API'sine taşı + buildCounter demosu

**Files:**
- Rewrite: `examples/ui_form.liva`, `examples/ui_hello_wx.liva`, `examples/ui_counter.liva`, `examples/ui_callback_demo.liva`, `examples/ui_showcase_demo.liva`, `examples/ui_validation_demo.liva`, `examples/ui_composite_demo.liva`, `examples/ui_form_themed.liva`, `examples/ui_widgets_demo.liva`, `examples/ui_paint.liva`, `examples/ui_hello.liva`, `examples/ui_showcase.liva`
- Create: `examples/ui_counter_helper.liva` (buildCounter demosu)

- [ ] **Step 1: `ui_form.liva`'yı sınıf API'siyle yeniden yaz**

```liva
// UI Form Demo — sınıf API
// Build: livac examples/ui_form.liva -o form_demo
import ui::widgets
import ui::types

func main() {
    appInit()
    let win = Window(500, 400, "Kayit Formu")
    let panel = Panel(win)
    let sizer = createVBoxSizer()

    let title = Label(panel, "Kayit Formu")
    title.setFont(18, true)
    sizerAdd(sizer, title.handle, 0, WX_ALL(), 10)

    let nameInp = TextInput(panel, "")
    sizerAdd(sizer, Label(panel, "Isim:").handle, 0, WX_LEFT(), 10)
    sizerAdd(sizer, nameInp.handle, 0, WX_EXPAND(), 5)

    let slider = Slider(panel, 0, 100, 50)
    let prog = ProgressBar(panel, 100)
    prog.setValue(50)
    sizerAdd(sizer, slider.handle, 0, WX_EXPAND(), 5)
    sizerAdd(sizer, prog.handle, 0, WX_EXPAND(), 5)

    slider.onChange(|_h: i32| { prog.setValue(slider.getValue()) })

    let submit = Button(panel, "Gonder")
    submit.onClick(|_h: i32| { messageBox("Basarili", "Form gonderildi!", 1) })
    sizerAdd(sizer, submit.handle, 0, WX_ALL(), 10)

    panel.setSizerHandle(sizer)
    win.show()
    appRun()
}
```

> Sizer API hâlâ handle alır (`sizerAdd(sizer, widget.handle, ...)`); bu Faz 1 kapsamında kabul. Faz 2'de tipli sizer'a geçilir.

- [ ] **Step 2: Diğer örnekleri aynı kalıpla taşı**

Her örnekte: `createX(parent, ...)` → `X(parentObj, ...)`; `onClick(h, cb)` → `obj.onClick(cb)`; `setValue(h, v)` → `obj.setValue(v)`; `windowShow(win,1)` → `win.show()`. `ui_paint.liva` için `Canvas(parent)` + `canvas.onPaint(...)`.

- [ ] **Step 3: buildCounter demosunu oluştur**

`examples/ui_counter_helper.liva`:
```liva
// Yardımcı-fonksiyon kalıbı artık güvenli: bind edip return eden fonksiyon
import ui::widgets

func buildCounter(panel: Control, label: string) -> Button {
    var count = 0
    let btn = Button(panel, label)
    btn.onClick(|_h: i32| {
        count = count + 1
        btn.setText("Sayi: " + count)
    })
    return btn
}

func main() {
    appInit()
    let win = Window(300, 200, "Sayac")
    let panel = Panel(win)
    let sizer = createVBoxSizer()
    let b = buildCounter(panel, "Tikla")
    sizerAdd(sizer, b.handle, 0, WX_ALL(), 20)
    panel.setSizerHandle(sizer)
    win.show()
    appRun()
}
```

- [ ] **Step 4: Bir örneği fiilen derle (manuel doğrulama)**

Run: `build-clang\livac.exe examples\ui_counter_helper.liva -o build-clang\_ui_counter_helper.exe`
Expected: Derleme + link başarılı (`.exe` oluşur). GUI'yi açıp kapatmak manuel/opsiyonel.

- [ ] **Step 5: Commit**

```bash
git add examples/ui_*.liva
git commit -m "examples: migrate UI demos to class API + buildCounter helper demo"
```

---

## Task 6: Saf-fonksiyon runtime testleri + tam regresyon

**Files:**
- Modify: `tests/unit/RuntimeExecTest.cpp`
- Test: tüm suite

- [ ] **Step 1: `types`/`animation` saf-fonksiyon testlerini yaz**

`RuntimeExecTest.cpp`'ye (GUI yok, headless güvenli):
```cpp
TEST(RuntimeExec, UiTypesRectContains) {
    auto r = compileAndRun(
        "import ui::types\n"
        "func main() {\n"
        "  let rc = Rect.new(10, 10, 100, 50)\n"
        "  if rc.contains(20, 20) { println(\"in\") } else { println(\"out\") }\n"
        "  if rc.contains(200, 20) { println(\"in\") } else { println(\"out\") }\n"
        "}\n",
        "ui_types_rect");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("in"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("out"), std::string::npos);
}

TEST(RuntimeExec, UiAnimationEasing) {
    auto r = compileAndRun(
        "import ui::animation\n"
        "func main() {\n"
        "  println(easeLinear(0.5))\n"
        "  println(easeInQuad(0.5))\n"
        "}\n",
        "ui_anim_easing");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.stdout_output.find("0.5"), std::string::npos);
    EXPECT_NE(r.stdout_output.find("0.25"), std::string::npos);
}
```

- [ ] **Step 2: Testlerin geçtiğini doğrula**

Run: `ctest --test-dir build-clang -R "RuntimeExec.UiTypes|RuntimeExec.UiAnimation" --output-on-failure`
Expected: PASS.

- [ ] **Step 3: Tam test su'sini çalıştır (regresyon)**

Run: `ctest --test-dir build-clang --output-on-failure`
Expected: Tüm testler PASS (mevcut 2064 + yeni eklenenler). Kırılan varsa düzelt.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/RuntimeExecTest.cpp
git commit -m "test: pure-function runtime tests for ui::types and ui::animation"
```

---

## Self-Review Notları (yazım sırasında doğrulandı)

- **Spec kapsamı:** §4 Control hiyerarşisi → Task 2; §5 intrinsic event + heap-env + onKey → Task 1+3; §6 runtime → Task 1; §7 migrasyon → Task 2,4,5; §8 test → Task 2,3,6. Tümü karşılandı.
- **Tip tutarlılığı:** FFI callback imzaları her yerde `(handle, func, env, size)` (onKey runtime tarafında `func(env,handle,keycode)`). `Control.handle` alanı index 0; member hızlı yolu `structTypes_["Control"]` ile GEP. Event metot adları `{onClick,onChange,onSelect,onKey}` her iki tarafta aynı.
- **Bilinen kısıt (spec ile uyumlu):** non-literal callback → uyarı + stack-env; `string` yakalama sığ kopya; sizer API Faz 1'de handle-tabanlı kalır.
- **Risk:** Diagnostic ekleme altyapısı (Step 7) projeye göre değişebilir; mevcut diagnostic kalıbı (`err_yield_outside_generator`) izlenmeli. Headless wxApp init riski için UI exec smoke testi `#ifdef LIVA_HAS_WXWIDGETS` guard'lı.
```
