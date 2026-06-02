# Faz 4 — Align/Anchors Yerleşim Implementasyon Planı

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `ui::widgets`'e Delphi VCL'in Align (kenar-dock) + Anchors (orantılı sabitleme) yerleşim modelini eklemek — sizer'a alternatif, ebeveynin `wxEVT_SIZE`'ına bağlı özel yerleşim yöneticisiyle.

**Architecture:** Faz 1-3'ün 3 katmanı korunur (Liva `Control` metodu → IRGenCall intrinsic → `wx_runtime.cpp` FFI). Yeni: runtime'da `g_layouts` (`wxWindow*`→`LivaLayout`) + `relayoutParent` VCL `AlignControls` algoritması; her ebeveyne bir kez `wxEVT_SIZE` işleyici bağlanır. `Align` enum + `Control.setAlign`/`setAnchors` API'si. Fast-path'e dokunulmaz (callback değil).

**Tech Stack:** C++20, LLVM 21 IRGen, wxWidgets 3.3 (vcpkg, `C:\Users\Kadir\.vcpkg-clion\vcpkg`), CMake/Ninja (`build-clang/`), GoogleTest.

---

## Dokunulacak Dosyalar

- **`stdlib/ui/wx_runtime.h`** — Phase 4 FFI prototipleri (`liva_ui_set_align`, `liva_ui_set_anchors`).
- **`stdlib/ui/wx_runtime.cpp`** — `<unordered_set>` include; `g_layouts`/`g_layoutParents`; `LivaLayout`; `captureRef`/`ensureParentBound`/`relayoutParent`; iki FFI; destroy temizliği.
- **`src/IR/IRGen.cpp`** — iki FFI için LLVM extern (Phase 3 splitter extern'lerinden sonra, `declareCoroutineIntrinsics();`'den önce).
- **`src/IR/IRGenCall.cpp`** — `setAlign`/`setAnchors` intrinsic eşlemesi (Phase 3 `splitterSetSash` bloğundan sonra).
- **`src/Sema/ModuleLoader.cpp`** — builtin adları `setAlign`, `setAnchors`.
- **`stdlib/ui/widgets.liva`** — `pub enum Align` (Control'den önce); `Control`'e `setAlign`/`setAnchors` metotları.
- **`tests/unit/UICodegenExecTest.cpp`** — emit-IR testleri.
- **`examples/ui_layout_align.liva`** — (Task 3) örnek.

### Doğrulama stratejisi
- **Derleyici zinciri:** `livac --emit-ir` (link yok). `Align` enum / `setAlign` builtin eksikse Sema hata verir → IR üretilmez → test düşer; eklenince geçer (TDD kırmızı→yeşil).
- **Runtime C++:** Bu makinede wxWidgets KURULU. `cmake --build build-clang`, `wx_runtime.cpp`'yi derler → `AlignControls`/anchor matematiği C++ hataları derleme aşamasında yakalanır.
- **GUI/yerleşim davranışı:** Yalnız wx-kurulu makinede `.exe` çalıştırılıp pencere yeniden boyutlandırılarak teyit edilir (headless yerleşimi test edemez — Task 3).

### Ortak komutlar
- Tam derleme: `cmake --build build-clang`
- Tek test: `ctest --test-dir build-clang -R "<TestAdı>" --output-on-failure`
- Tam regresyon: `ctest --test-dir build-clang --output-on-failure`

---

## Task 1: Align (kenar-dock) + yerleşim yöneticisi

`Align` enum + `Control.setAlign` + tam derleyici bağlama + runtime yerleşim motoru (g_layouts, captureRef, ensureParentBound, relayoutParent'ın DOCK kısmı) + destroy temizliği. `none` çocuklar bu task'ta referans konumlarında kalır (varsayılan sol-üst sabit); anchor matematiği Task 2'de eklenir.

**Files:**
- Test: `tests/unit/UICodegenExecTest.cpp`
- Modify: `stdlib/ui/wx_runtime.h`, `stdlib/ui/wx_runtime.cpp`, `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/ModuleLoader.cpp`, `stdlib/ui/widgets.liva`

- [ ] **Step 1: Write the failing test**

`tests/unit/UICodegenExecTest.cpp` içinde, `AdvancedWidgetsExampleCompiles` testinden sonra (son `TEST`'ten sonra, `#endif // LIVA_HAS_LLVM`'den önce) ekle:

```cpp
// ── Phase 4: Align/Anchors layout ──────────────────────────────────────
TEST(UICodegenExec, AlignDockCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let root = Panel(win)\n"
        "  let top = Panel(root)\n"
        "  top.setSize(400, 36)\n"
        "  top.setAlign(Align.top)\n"
        "  let bottom = Panel(root)\n"
        "  bottom.setAlign(Align.bottom)\n"
        "  let side = Panel(root)\n"
        "  side.setAlign(Align.left)\n"
        "  let center = Panel(root)\n"
        "  center.setAlign(Align.client)\n"
        "}\n",
        "align_dock");
    EXPECT_TRUE(emitsClean(ir));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.AlignDockCompiles" --output-on-failure`
Expected: FAIL — `no IR emitted` (`Align` enum ve `setAlign` builtin'i yok).

- [ ] **Step 3a: Add runtime include + FFI prototype**

`stdlib/ui/wx_runtime.cpp` içinde, `#include <unordered_map>` satırından (satır 37) sonra ekle:

```cpp
#include <unordered_set>
```

`stdlib/ui/wx_runtime.h` içinde, Phase 3 bölümünden sonra (son `liva_ui_splitter_set_sash` satırından sonra, `/* ── List / Tab operations ── */` yorumundan önce) ekle:

```c
/* ── Phase 4: Align/Anchors layout ────────────────────────────────── */
void liva_ui_set_align(int32_t handle, int32_t align);
void liva_ui_set_anchors(int32_t handle, int32_t left, int32_t top, int32_t right, int32_t bottom);
```

- [ ] **Step 3b: Add runtime layout engine + set_align FFI**

`stdlib/ui/wx_runtime.cpp` içinde, dosyanın SONUNA (Phase 3 Splitter fonksiyonlarından sonra) ekle:

```cpp
/* ═══════════════════════════════════════════════════════════════════
   Phase 4: Align/Anchors layout (VCL AlignControls)
   ═══════════════════════════════════════════════════════════════════ */

struct LivaLayout {
    int  align = 0;                 // 0=none,1=top,2=bottom,3=left,4=right,5=client
    bool aLeft = true, aTop = true, aRight = false, aBottom = false;  // VCL default
    wxRect refBounds;               // anchor referans dikdörtgeni
    wxSize refParentClient;         // anchor referans ebeveyn istemci boyutu
    bool   hasRef = false;
};
// Anahtar wxWindow* (handle değil): relayout doğrudan GetChildren() pointer'larıyla bakar.
static std::unordered_map<wxWindow *, LivaLayout> g_layouts;
static std::unordered_set<wxWindow *> g_layoutParents;

static void relayoutParent(wxWindow *parent);   // forward

// Çocuğun mevcut rect'ini + ebeveyn istemci boyutunu anchor referansı olarak yakala.
static void captureLayoutRef(wxWindow *w) {
    LivaLayout &L = g_layouts[w];
    L.refBounds = w->GetRect();
    if (wxWindow *p = w->GetParent()) {
        L.refParentClient = p->GetClientSize();
        L.hasRef = true;
    }
}

// Ebeveynin yeniden boyutlanınca VCL yerleşimini çalıştırmasını sağla (bir kez bağla).
static void ensureParentBound(wxWindow *parent) {
    if (!parent || g_layoutParents.count(parent)) return;
    if (parent->GetSizer()) parent->SetSizer(nullptr, false);  // ebeveyn başına biri
    parent->Bind(wxEVT_SIZE, [parent](wxSizeEvent &e) {
        relayoutParent(parent);
        e.Skip();
    });
    g_layoutParents.insert(parent);
}

// VCL AlignControls: kenar-dock çocuklar istemci dikdörtgenini tüketir; sonra
// 'none' çocuklar anchor'lara göre konumlanır (anchor matematiği Task 2'de).
static void relayoutParent(wxWindow *parent) {
    if (!parent) return;
    wxSize cs = parent->GetClientSize();
    wxRect R(0, 0, cs.x, cs.y);

    // 1) Kenar-dock (top, bottom, left, right) — z-sırasında.
    for (wxWindow *child : parent->GetChildren()) {
        auto it = g_layouts.find(child);
        if (it == g_layouts.end()) continue;
        wxSize sz = child->GetSize();
        switch (it->second.align) {
            case 1: child->SetSize(R.x, R.y, R.width, sz.y);
                    R.y += sz.y; R.height -= sz.y; break;             // top
            case 2: child->SetSize(R.x, R.y + R.height - sz.y, R.width, sz.y);
                    R.height -= sz.y; break;                          // bottom
            case 3: child->SetSize(R.x, R.y, sz.x, R.height);
                    R.x += sz.x; R.width -= sz.x; break;              // left
            case 4: child->SetSize(R.x + R.width - sz.x, R.y, sz.x, R.height);
                    R.width -= sz.x; break;                           // right
            default: break;
        }
    }
    // 2) client: kalan R'yi doldur (yalnız ilki).
    bool clientUsed = false;
    for (wxWindow *child : parent->GetChildren()) {
        auto it = g_layouts.find(child);
        if (it != g_layouts.end() && it->second.align == 5 && !clientUsed) {
            child->SetSize(R.x, R.y, R.width, R.height);
            clientUsed = true;
        }
    }
    // 3) 'none' çocuklar: anchors (Task 2). Task 1'de varsayılan [left,top] =
    //    sabit konum → dokunulmaz (zaten yerlerinde).
}

void liva_ui_set_align(int32_t handle, int32_t align) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w) return;
    g_layouts[w].align = align;
    captureLayoutRef(w);
    wxWindow *parent = w->GetParent();
    ensureParentBound(parent);
    relayoutParent(parent);
}
```

- [ ] **Step 3c: Add destroy cleanup**

`stdlib/ui/wx_runtime.cpp` içinde `liva_ui_destroy_widget` (satır ~559) gövdesini güncelle — `w->Destroy();`'den ÖNCE layout kayıtlarını temizle:

```cpp
void liva_ui_destroy_widget(int32_t handle) {
    freeWidgetEnvs(handle);
    auto *w = getHandle<wxWindow>(handle);
    if (!w) return;
    g_layouts.erase(w);
    g_layoutParents.erase(w);
    w->Destroy();
    g_handles.erase(handle);
}
```

- [ ] **Step 3d: Add IRGen extern**

`src/IR/IRGen.cpp` içinde, Phase 3 bloğunun sonunda (`liva_ui_splitter_set_sash` extern'inden sonra), `// Coroutine + async runtime` / `declareCoroutineIntrinsics();`'den ÖNCE ekle:

```cpp
    // ── Phase 4: Align/Anchors layout ────────────────────────────────
    // set_align(i32 handle, i32 align) -> void
    module_->getOrInsertFunction("liva_ui_set_align", uiSetValTy);
```

> `uiSetValTy` = `(i32,i32)->void` (satır ~1426), kapsamda.

- [ ] **Step 3e: Add IRGenCall intrinsic**

`src/IR/IRGenCall.cpp` içinde, Phase 3 bloğunun sonunda (`splitterSetSash` intrinsic'inden sonra), `// Closure-taking free-function forms` yorumundan ÖNCE ekle:

```cpp
    // ── Phase 4: Align/Anchors layout ────────────────────────────────
    // setAlign(handle, align) -> void  (align: basit enum → i32 discriminant)
    if (funcName == "setAlign" && node->getArgs().size() >= 2) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *align = visit(node->getArgs()[1].get());
        if (!handle || !align) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_align"), {handle, align});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
```

- [ ] **Step 3f: Register builtin name**

`src/Sema/ModuleLoader.cpp` içinde, Phase 3 builtin grubunun son satırını (`"createSplitter", "splitterSplitV", "splitterSplitH", "splitterSetSash"});`) değiştir:

```cpp
         "createSplitter", "splitterSplitV", "splitterSplitH", "splitterSetSash",
         // Phase 4: align/anchors
         "setAlign"});
```

- [ ] **Step 3g: Add Align enum + Control.setAlign**

`stdlib/ui/widgets.liva` içinde, `import ui::types` satırından sonra, `pub class Control`'den ÖNCE ekle:

```liva
// ── Align (VCL kenar-dock) ─────────────────────────────────────
pub enum Align {
    case none = 0
    case top = 1
    case bottom = 2
    case left = 3
    case right = 4
    case client = 5
}
```

Aynı dosyada, `Control` sınıfının gövdesine, `onRightClick` metodundan (satır ~41) sonra, sınıfı kapatan `}`'dan önce ekle:

```liva
    // VCL yerlesim: kenar-dock. Once setSize/setBounds, sonra setAlign.
    pub func setAlign(a: Align) { setAlign(self.handle, a) }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.AlignDockCompiles" --output-on-failure`
Expected: PASS. Bu makinede wx kurulu → tam `cmake --build build-clang` `wx_runtime.cpp`'yi de derler; `relayoutParent`/`wxEVT_SIZE`/`GetChildren`/`SetSize` çözülmeli. Bir wx sembolü eksik/yeniden-adlandırılmışsa doğrusunu bul ve sapmayı raporla.

- [ ] **Step 5: Commit**

```bash
git add stdlib/ui/wx_runtime.h stdlib/ui/wx_runtime.cpp src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/ModuleLoader.cpp stdlib/ui/widgets.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase4: Align dock layout (VCL AlignControls + Align enum + setAlign)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Anchors (orantılı sabitleme)

`Control.setAnchors` + builtin + extern + intrinsic + runtime FFI; `relayoutParent`'ın 3. pass'ine (none çocuklar) anchor matematiği eklenir.

**Files:**
- Test: `tests/unit/UICodegenExecTest.cpp`
- Modify: `stdlib/ui/wx_runtime.h`, `stdlib/ui/wx_runtime.cpp`, `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/ModuleLoader.cpp`, `stdlib/ui/widgets.liva`

- [ ] **Step 1: Write the failing test**

`tests/unit/UICodegenExecTest.cpp` içinde, `AlignDockCompiles`'dan sonra ekle:

```cpp
TEST(UICodegenExec, AnchorsCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let root = Panel(win)\n"
        "  let ok = Button(root, \"OK\")\n"
        "  ok.setBounds(300, 250, 80, 28)\n"
        "  ok.setAnchors(false, false, true, true)\n"
        "  let inp = TextInput(root, \"\")\n"
        "  inp.setBounds(10, 250, 280, 28)\n"
        "  inp.setAnchors(true, true, true, false)\n"
        "}\n",
        "anchors");
    EXPECT_TRUE(emitsClean(ir));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.AnchorsCompiles" --output-on-failure`
Expected: FAIL — `no IR emitted` (`setAnchors` builtin'i yok).

- [ ] **Step 3a: Add runtime FFI prototype**

`stdlib/ui/wx_runtime.h` Phase 4 bölümüne (set_align satırından sonra) zaten Task 1'de `liva_ui_set_anchors` prototipi eklenmişti. Onaylı; değişiklik yok. (Task 1 Step 3a iki prototipi de ekledi.)

- [ ] **Step 3b: Add set_anchors FFI + anchor math in relayout**

`stdlib/ui/wx_runtime.cpp` Phase 4 bölümünde, `relayoutParent` fonksiyonundaki 3. pass yorumunu (`// 3) 'none' çocuklar: ...`) ŞUNUNLA değiştir:

```cpp
    // 3) 'none' çocuklar: anchors (refBounds + ebeveyn boyut deltası).
    for (wxWindow *child : parent->GetChildren()) {
        auto it = g_layouts.find(child);
        if (it == g_layouts.end()) continue;        // dokunulmamış → sabit
        LivaLayout &L = it->second;
        if (L.align != 0 || !L.hasRef) continue;
        int dw = cs.x - L.refParentClient.x;
        int dh = cs.y - L.refParentClient.y;
        int nx = L.refBounds.x, nw = L.refBounds.width;
        int ny = L.refBounds.y, nh = L.refBounds.height;
        if (L.aLeft && L.aRight)        nw = L.refBounds.width + dw;   // yatay esner
        else if (!L.aLeft && L.aRight)  nx = L.refBounds.x + dw;       // sag kenarla
        else if (!L.aLeft && !L.aRight) nx = L.refBounds.x + dw / 2;   // orantili
        if (L.aTop && L.aBottom)        nh = L.refBounds.height + dh;  // dikey esner
        else if (!L.aTop && L.aBottom)  ny = L.refBounds.y + dh;       // alt kenarla
        else if (!L.aTop && !L.aBottom) ny = L.refBounds.y + dh / 2;   // orantili
        child->SetSize(nx, ny, nw, nh);
    }
```

Aynı dosyada, `liva_ui_set_align` fonksiyonundan sonra ekle:

```cpp
void liva_ui_set_anchors(int32_t handle, int32_t left, int32_t top,
                         int32_t right, int32_t bottom) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w) return;
    LivaLayout &L = g_layouts[w];
    L.aLeft = left != 0; L.aTop = top != 0;
    L.aRight = right != 0; L.aBottom = bottom != 0;
    captureLayoutRef(w);
    wxWindow *parent = w->GetParent();
    ensureParentBound(parent);
    relayoutParent(parent);
}
```

- [ ] **Step 3c: Add IRGen extern**

`src/IR/IRGen.cpp` Phase 4 bloğuna (set_align extern'inden sonra) ekle:

```cpp
    // set_anchors(i32 handle, i32 l, i32 t, i32 r, i32 b) -> void
    auto *uiI32x5VoidTy =
        llvm::FunctionType::get(voidTy, {i32Ty, i32Ty, i32Ty, i32Ty, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_set_anchors", uiI32x5VoidTy);
```

> `voidTy`, `i32Ty` kapsamda. `uiI32x5VoidTy` yalnız burada tanımlanır.

- [ ] **Step 3d: Add IRGenCall intrinsic**

`src/IR/IRGenCall.cpp` Phase 4 bloğuna (setAlign intrinsic'inden sonra) ekle:

```cpp
    // setAnchors(handle, left, top, right, bottom) -> void
    if (funcName == "setAnchors" && node->getArgs().size() >= 5) {
        auto *handle = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        auto *t = visit(node->getArgs()[2].get());
        auto *r = visit(node->getArgs()[3].get());
        auto *b = visit(node->getArgs()[4].get());
        if (!handle || !l || !t || !r || !b) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_set_anchors"), {handle, l, t, r, b});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
```

- [ ] **Step 3e: Register builtin name**

`src/Sema/ModuleLoader.cpp` Phase 4 satırını güncelle:

```cpp
         "createSplitter", "splitterSplitV", "splitterSplitH", "splitterSetSash",
         // Phase 4: align/anchors
         "setAlign", "setAnchors"});
```

- [ ] **Step 3f: Add Control.setAnchors**

`stdlib/ui/widgets.liva` içinde, `Control` sınıfındaki `setAlign` metodundan sonra ekle:

```liva
    // VCL yerlesim: kenar sabitleme. Once setBounds, sonra setAnchors.
    pub func setAnchors(left: bool, top: bool, right: bool, bottom: bool) {
        var l = 0; if left { l = 1 }
        var t = 0; if top { t = 1 }
        var r = 0; if right { r = 1 }
        var b = 0; if bottom { b = 1 }
        setAnchors(self.handle, l, t, r, b)
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.AnchorsCompiles" --output-on-failure`
Expected: PASS. Tam `cmake --build build-clang` `wx_runtime.cpp`'yi derler (anchor matematiği C++ hataları burada görünür).

- [ ] **Step 5: Commit**

```bash
git add stdlib/ui/wx_runtime.h stdlib/ui/wx_runtime.cpp src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/ModuleLoader.cpp stdlib/ui/widgets.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase4: Anchors (proportional resize) + setAnchors

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Örnek + birleşik test + regresyon + GUI doğrulama

Align + anchors'ı tek pencerede gösteren örnek; birleşik emit-IR testi; tam regresyon; wx-kurulu makinede GUI teyidi.

**Files:**
- Create: `examples/ui_layout_align.liva`
- Test: `tests/unit/UICodegenExecTest.cpp`

- [ ] **Step 1: Write the failing combined test**

`tests/unit/UICodegenExecTest.cpp` içinde, `AnchorsCompiles`'dan sonra ekle:

```cpp
TEST(UICodegenExec, AlignAnchorsLayoutCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(700, 500, \"Align Anchors\")\n"
        "  let root = Panel(win)\n"
        "  let toolbar = Panel(root)\n"
        "  toolbar.setSize(700, 36)\n"
        "  toolbar.setAlign(Align.top)\n"
        "  let status = Label(root, \"Hazir\")\n"
        "  status.setAlign(Align.bottom)\n"
        "  let side = Panel(root)\n"
        "  side.setSize(180, 0)\n"
        "  side.setAlign(Align.left)\n"
        "  let editor = TextArea(root, \"\")\n"
        "  editor.setAlign(Align.client)\n"
        "  let ok = Button(root, \"Tamam\")\n"
        "  ok.setBounds(600, 430, 80, 28)\n"
        "  ok.setAnchors(false, false, true, true)\n"
        "  win.show()\n"
        "  appRun()\n"
        "}\n",
        "align_anchors_layout");
    EXPECT_TRUE(emitsClean(ir));
}
```

- [ ] **Step 2: Run test (Task 1-2 tamamsa geçer)**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.AlignAnchorsLayoutCompiles" --output-on-failure`
Expected: PASS — align + anchors bir arada derlenir.

- [ ] **Step 3: Create the example file**

`examples/ui_layout_align.liva` oluştur:

```liva
// Faz 4 — Align/Anchors yerlesim (sizer'siz, saf VCL).
// Build: livac examples/ui_layout_align.liva -o layout_align
//
// Frame'in tek icerik cocugu root Panel'dir (otomatik dolar). Icindeki
// cocuklar Align ile kenarlara dock olur; client kalani doldurur. Buton
// Anchors ile sag-alt koseye sabitlenir. Pencere yeniden boyutlaninca:
// kenarlar sabit kalir, client (editor) buyur, buton sag-alt'ta kalir.
// Kural: kenar-dock icin once setSize (dock yonundeki kalinlik), sonra setAlign.
import ui::widgets

func main() {
    appInit()
    let win = Window(720, 520, "Liva — Align/Anchors")
    let root = Panel(win)

    // Ust arac cubugu (yukseklik 36, tam genislik)
    let toolbar = Panel(root)
    toolbar.setSize(720, 36)
    toolbar.setBgColor(60, 60, 90)
    toolbar.setAlign(Align.top)

    // Alt durum satiri
    let status = Label(root, "Hazir — pencereyi yeniden boyutlandirin")
    status.setAlign(Align.bottom)

    // Sol kenar paneli (genislik 180)
    let side = Panel(root)
    side.setSize(180, 0)
    side.setBgColor(230, 230, 235)
    side.setAlign(Align.left)

    // Merkez editor — kalan tum alani doldurur
    let editor = TextArea(root, "Bu alan client align'lidir; pencere buyudukce buyur.")
    editor.setAlign(Align.client)

    // Sag-alt'a sabit buton (anchors: sag + alt)
    let ok = Button(root, "Tamam")
    ok.setBounds(620, 440, 80, 28)
    ok.setAnchors(false, false, true, true)
    ok.onClick(|_h: i32| { messageBox("Bilgi", "Tamam", 1) })

    win.show()
    appRun()
}
```

Oluşturduktan sonra örneğin temiz IR ürettiğini doğrula:

Run: `build-clang/livac.exe --emit-ir -o build-clang/_ex_align.ll examples/ui_layout_align.liva`
Sonra `build-clang/_ex_align.ll` içinde `define` olduğunu Grep ile teyit et, ardından `_ex_align.ll`'i sil. livac hata verirse örneği gerçek API'ye göre düzelt (API `stdlib/ui/widgets.liva`'daki Control/Align'dır).

- [ ] **Step 4: Run the full regression suite**

Run: `cmake --build build-clang && ctest --test-dir build-clang --output-on-failure`
Expected: TÜM testler PASS (mevcut 2306 + Faz 4'te eklenen 3 test = 2309). Tam pass/fail sayısını raporla.

- [ ] **Step 5: GUI doğrulama (wx-kurulu makinede — manuel)**

> Kullanıcının wx-kurulu ortamında çalıştırılır. Otomatik test edilemez (headless yerleşim).

```bash
build-clang/livac.exe examples/ui_layout_align.liva -o examples/ui_layout_align.exe
examples/ui_layout_align.exe
```

Doğrula:
- Üst koyu toolbar şeridi, alt durum etiketi, sol açık-gri panel, merkezde editor (kalanı doldurur), sağ-altta Tamam butonu görünür.
- Pencere **yeniden boyutlandırılınca**: toolbar/status/side sabit kalır, editor büyür/küçülür, buton sağ-alt köşeyle birlikte taşınır.
- Assert/crash yok.

- [ ] **Step 6: Commit**

```bash
git add examples/ui_layout_align.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui-phase4: align/anchors example + combined regression test

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Tamamlama

İki task + örnek tamamlandığında `superpowers:finishing-a-development-branch` ile entegrasyon (master'a merge / PR) seçeneklerini sun. Branch: `feat/ui-vcl-phase4-align-anchors`.
