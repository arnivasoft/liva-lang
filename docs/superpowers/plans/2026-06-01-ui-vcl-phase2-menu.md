# Faz 2 — Menü Sistemi & Uygulama Çerçevesi Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** wxWidgets tabanlı `ui::widgets` kütüphanesine VCL-tarzı uygulama çerçevesi eklemek — MenuBar/Menu/MenuItem, sağ-tık context menu, StatusBar, Toolbar — Faz 1'in sınıf + inline-closure-heap-env altyapısını yeniden kullanarak.

**Architecture:** Faz 1'in 3 katmanı korunur (kullanıcı → `ui::widgets` sınıfları → `liva_ui_*` intrinsic'leri → `wx_runtime.cpp`). Menü/tool item'ları `wxWindow` değildir, bu yüzden ayrı FFI alırlar ve Faz 1 fast-path'i "alıcı `handle: i32` alanı struct-index 1'de + tanınan event metodu" kuralına genelleştirilir. Birincil ergonomi kısa-yol: `menu.addItem(label, cb) -> MenuItem`.

**Tech Stack:** C++20, LLVM 21, wxWidgets, CMake/Ninja, GoogleTest. Build: `cmake --build build-clang` (cache `LIVA_HAS_WXWIDGETS=OFF` ile yapılandırılmış — wx yok, bu beklenen). Test: `ctest --test-dir build-clang`. Branch: `feat/ui-vcl-phase2`.

**Referans tasarım:** `docs/superpowers/specs/2026-06-01-ui-vcl-phase2-menu-design.md`

---

## Yürütme Adaptasyonu — wxWidgets KURULU DEĞİL (Faz 1 ile aynı)

`wx_runtime.cpp` bu ortamda derlenmez; `import std::ui`/`ui::widgets` içeren programlar **linklenemez**. Doğrulama:
- **Runtime (Task 1):** Spec'e göre yazılır; `cmake --build build-clang` ile livac + liva_runtime (wx hariç) hatasız derlenerek doğrulanır. Tam wx derlemesi kullanıcının wx-kurulu makinesine bırakılır.
- **Compiler + stdlib (Task 2-3):** `livac --emit-ir` (LİNK YOK) ile menü programları LLVM IR'a derlenir; üretilen `.ll` incelenir. **Birincil doğrulama katmanı.**
- **Testler (Task 4):** `tests/unit/UICodegenExecTest.cpp` — emit-ir tabanlı (mevcut `emitIR` + `emitsClean` yardımcıları kullanılır).
- **Regresyon:** tam `ctest`; bilinen-ve-kabul edilen başarısızlıklar yalnız `RuntimeExecTest.BTreeMapI64OrderedInsertGet` ve `RuntimeExecTest.BTreeMapStrLookup` (önceden var olan btree WIP). Başka her şey yeşil olmalı.

---

## Dosya Haritası

| Dosya | Sorumluluk | Eylem |
|-------|-----------|-------|
| `stdlib/ui/wx_runtime.h` | FFI imzaları | Modify — menü/context/statusbar/toolbar FFI'leri |
| `stdlib/ui/wx_runtime.cpp` | wxWidgets runtime | Modify — wxMenu*/wxStatusBar/wxToolBar implementasyonu, menü ID yönetimi, heap-env |
| `src/Sema/ModuleLoader.cpp` | `std::ui` builtin adları | Modify — 21 yeni fonksiyon adı |
| `src/IR/IRGen.cpp` | UI extern kayıtları | Modify — yeni FFI extern'leri |
| `src/IR/IRGenCall.cpp` | UI intrinsic eşleme + fast-path | Modify — yeni intrinsic'ler + genelleştirilmiş fast-path |
| `stdlib/ui/menu.liva` | Menü sınıf API | Create — MenuBar/Menu/MenuItem |
| `stdlib/ui/widgets.liva` | Window/Control/Toolbar | Modify — setMenuBar/setStatusBar/onRightClick/Toolbar/ToolItem/StatusBar |
| `tests/unit/UICodegenExecTest.cpp` | IR-emit testleri | Modify — menü/toolbar/statusbar/context-menu + heap-env testleri |
| `examples/ui_menu_demo.liva` | Örnek | Create — tam uygulama iskeleti |

> **Karar:** Menü sınıfları yeni `stdlib/ui/menu.liva` dosyasına konur (widgets.liva 279+ satır, odaklı tutulur). StatusBar/Toolbar/ToolItem ve Window/Control metot eklemeleri `widgets.liva`'ya girer (Window/Control orada). `menu.liva` `import ui::widgets` ile Control'e erişir (cross-module kalıtım Faz 1'de düzeltildi).

---

## Task 1: Runtime — menü/context/statusbar/toolbar FFI

**Files:**
- Modify: `stdlib/ui/wx_runtime.h`
- Modify: `stdlib/ui/wx_runtime.cpp`

Pür C++ task; wx olmadan derlenemez. Doğrulama: livac + liva_runtime'ın (wx hariç) hatasız derlenmesi.

- [ ] **Step 1: FFI imzalarını ekle (`wx_runtime.h`)**

`wx_runtime.h`'de, mevcut `liva_ui_set_bounds` bildiriminin altına (Geometry bölümünden sonra) yeni bir bölüm ekle:

```c
/* ── Menu ─────────────────────────────────────────────────────────── */
int32_t  liva_ui_create_menu_bar(void);
int32_t  liva_ui_create_menu(const char *title);
int32_t  liva_ui_menu_add_item(int32_t menu, const char *label);
int32_t  liva_ui_menu_add_check_item(int32_t menu, const char *label);
void     liva_ui_menu_add_separator(int32_t menu);
void     liva_ui_menu_add_submenu(int32_t menu, const char *label, int32_t sub);
void     liva_ui_menu_bar_add_menu(int32_t bar, int32_t menu);
void     liva_ui_window_set_menu_bar(int32_t window, int32_t bar);
void     liva_ui_menu_item_set_enabled(int32_t item, int32_t enabled);
void     liva_ui_menu_item_set_checked(int32_t item, int32_t checked);
void     liva_ui_menu_item_on_click(int32_t item, void *func, void *env, int32_t size);
void     liva_ui_menu_popup(int32_t menu, int32_t target);

/* ── Context menu ─────────────────────────────────────────────────── */
void     liva_ui_on_right_click(int32_t handle, void *func, void *env, int32_t size);

/* ── StatusBar ────────────────────────────────────────────────────── */
int32_t  liva_ui_create_status_bar(int32_t window, int32_t field_count);
void     liva_ui_status_bar_set_text(int32_t sb, int32_t field, const char *text);

/* ── Toolbar ──────────────────────────────────────────────────────── */
int32_t  liva_ui_create_toolbar(int32_t window);
int32_t  liva_ui_toolbar_add_tool(int32_t tb, const char *label);
void     liva_ui_toolbar_add_separator(int32_t tb);
void     liva_ui_toolbar_realize(int32_t tb);
void     liva_ui_tool_item_set_enabled(int32_t tool, int32_t enabled);
void     liva_ui_tool_item_on_click(int32_t tool, void *func, void *env, int32_t size);
```

- [ ] **Step 2: Menü altyapısı (`wx_runtime.cpp`) — include + menü kaydı**

`wx_runtime.cpp` başındaki include'lara ekle (eğer yoksa):
```cpp
#include <wx/menu.h>
#include <wx/toolbar.h>
#include <wx/statusbr.h>
```

`g_widgetEnvs` tanımının yakınına, menü item kaydı için yapı ekle:
```cpp
// Menu/tool items are not wxWindow*; track the wx object, its command id, and
// the owning frame (for late event binding).
struct LivaMenuItem {
    wxMenuItem *item = nullptr;
    int32_t id = 0;
    wxFrame *ownerFrame = nullptr;   // resolved when the menubar/popup is shown
    // Pending click binding captured before the owner frame is known:
    LivaCallbackFn pendingFn = nullptr;
    void *pendingEnv = nullptr;
    bool hasPending = false;
};
static std::unordered_map<int32_t, LivaMenuItem> g_menuItems;

struct LivaToolItem {
    wxToolBarToolBase *tool = nullptr;
    int32_t id = 0;
    wxToolBar *toolbar = nullptr;
};
static std::unordered_map<int32_t, LivaToolItem> g_toolItems;

static int g_nextCmdId = 20000;  // wx user command id range
```

- [ ] **Step 3: Menü oluşturma + yapı FFI'leri**

`wx_runtime.cpp`'nin sonuna (extern "C" bloğu içinde) ekle:
```cpp
int32_t liva_ui_create_menu_bar(void) {
    return allocHandle(new wxMenuBar());
}

int32_t liva_ui_create_menu(const char *title) {
    auto *m = new wxMenu();
    int32_t h = allocHandle(m);
    // Stash the title on the menu via client data is awkward; instead keep a
    // side map so menu_bar_add_menu can read it.
    g_menuTitles()[h] = wxString::FromUTF8(title ? title : "");
    return h;
}

int32_t liva_ui_menu_add_item(int32_t menu, const char *label) {
    auto *m = getHandle<wxMenu>(menu);
    if (!m) return 0;
    int id = g_nextCmdId++;
    auto *item = m->Append(id, wxString::FromUTF8(label ? label : ""));
    int32_t h = allocHandle(item);
    g_menuItems[h] = LivaMenuItem{item, id, nullptr, nullptr, nullptr, false};
    return h;
}

int32_t liva_ui_menu_add_check_item(int32_t menu, const char *label) {
    auto *m = getHandle<wxMenu>(menu);
    if (!m) return 0;
    int id = g_nextCmdId++;
    auto *item = m->AppendCheckItem(id, wxString::FromUTF8(label ? label : ""));
    int32_t h = allocHandle(item);
    g_menuItems[h] = LivaMenuItem{item, id, nullptr, nullptr, nullptr, false};
    return h;
}

void liva_ui_menu_add_separator(int32_t menu) {
    if (auto *m = getHandle<wxMenu>(menu)) m->AppendSeparator();
}

void liva_ui_menu_add_submenu(int32_t menu, const char *label, int32_t sub) {
    auto *m = getHandle<wxMenu>(menu);
    auto *s = getHandle<wxMenu>(sub);
    if (m && s) m->AppendSubMenu(s, wxString::FromUTF8(label ? label : ""));
}

void liva_ui_menu_bar_add_menu(int32_t bar, int32_t menu) {
    auto *mb = getHandle<wxMenuBar>(bar);
    auto *m = getHandle<wxMenu>(menu);
    if (mb && m) mb->Append(m, g_menuTitles()[menu]);
}
```

Dosyada `g_menuTitles` için (statik fonksiyon-içi map deseni; başlık kayıt sorununu çözer) yardımcı ekle (menü kayıt yapısının yakınına):
```cpp
static std::unordered_map<int32_t, wxString> &g_menuTitles() {
    static std::unordered_map<int32_t, wxString> m;
    return m;
}
```

- [ ] **Step 4: setMenuBar + item event binding (late-bind)**

```cpp
void liva_ui_window_set_menu_bar(int32_t window, int32_t bar) {
    auto *f = getHandle<wxFrame>(window);
    auto *mb = getHandle<wxMenuBar>(bar);
    if (!f || !mb) return;
    f->SetMenuBar(mb);
    // Now that items have an owner frame, flush any pending click bindings.
    for (auto &kv : g_menuItems) {
        auto &mi = kv.second;
        if (mi.hasPending && mi.ownerFrame == nullptr) {
            mi.ownerFrame = f;
            LivaCallbackFn fn = mi.pendingFn;
            void *env = mi.pendingEnv;
            int32_t ih = kv.first;
            f->Bind(wxEVT_MENU,
                    [fn, env, ih](wxCommandEvent &) { if (fn) fn(env, ih); },
                    mi.id);
            mi.hasPending = false;
        }
    }
}

void liva_ui_menu_item_set_enabled(int32_t item, int32_t enabled) {
    auto it = g_menuItems.find(item);
    if (it != g_menuItems.end() && it->second.item)
        it->second.item->Enable(enabled != 0);
}

void liva_ui_menu_item_set_checked(int32_t item, int32_t checked) {
    auto it = g_menuItems.find(item);
    if (it != g_menuItems.end() && it->second.item && it->second.item->IsCheckable())
        it->second.item->Check(checked != 0);
}

void liva_ui_menu_item_on_click(int32_t item, void *func, void *env, int32_t size) {
    auto it = g_menuItems.find(item);
    if (it == g_menuItems.end() || !func) return;
    void *owned = ownEnv(item, env, size);
    auto fn = (LivaCallbackFn)func;
    auto &mi = it->second;
    if (mi.ownerFrame) {
        int32_t ih = item;
        mi.ownerFrame->Bind(wxEVT_MENU,
            [fn, owned, ih](wxCommandEvent &) { if (fn) fn(owned, ih); }, mi.id);
    } else {
        // Owner frame not yet known (menubar not set). Defer until set_menu_bar.
        mi.pendingFn = fn;
        mi.pendingEnv = owned;
        mi.hasPending = true;
    }
}

void liva_ui_menu_popup(int32_t menu, int32_t target) {
    auto *m = getHandle<wxMenu>(menu);
    auto *w = getHandle<wxWindow>(target);
    if (!m || !w) return;
    // For a popup menu the items have no menubar owner; bind to the target
    // window so wxEVT_MENU reaches the lambdas. Flush pending bindings to w.
    for (auto &kv : g_menuItems) {
        auto &mi = kv.second;
        if (mi.hasPending && mi.ownerFrame == nullptr) {
            LivaCallbackFn fn = mi.pendingFn;
            void *env = mi.pendingEnv;
            int32_t ih = kv.first;
            w->Bind(wxEVT_MENU,
                    [fn, env, ih](wxCommandEvent &) { if (fn) fn(env, ih); }, mi.id);
            mi.hasPending = false;
        }
    }
    w->PopupMenu(m);
}
```

- [ ] **Step 5: Context menu (onRightClick) — x,y payload**

`LivaKeyFn` tanımının yakınına ekle:
```cpp
using LivaXYFn = void (*)(void *env, int32_t handle, int32_t x, int32_t y);
```
Sonra (extern "C" içinde):
```cpp
void liva_ui_on_right_click(int32_t handle, void *func, void *env, int32_t size) {
    auto *w = getHandle<wxWindow>(handle);
    if (!w || !func) return;
    void *owned = ownEnv(handle, env, size);
    auto fn = (LivaXYFn)func;
    w->Bind(wxEVT_CONTEXT_MENU, [fn, owned, handle, w](wxContextMenuEvent &evt) {
        wxPoint p = evt.GetPosition();
        if (p == wxDefaultPosition) p = ::wxGetMousePosition();
        wxPoint cli = w->ScreenToClient(p);
        if (fn) fn(owned, handle, cli.x, cli.y);
    });
}
```

- [ ] **Step 6: StatusBar + Toolbar FFI**

```cpp
int32_t liva_ui_create_status_bar(int32_t window, int32_t field_count) {
    auto *f = getHandle<wxFrame>(window);
    if (!f) return 0;
    wxStatusBar *sb = f->CreateStatusBar(field_count > 0 ? field_count : 1);
    return allocHandle(sb);
}

void liva_ui_status_bar_set_text(int32_t sb, int32_t field, const char *text) {
    if (auto *s = getHandle<wxStatusBar>(sb))
        s->SetStatusText(wxString::FromUTF8(text ? text : ""), field);
}

int32_t liva_ui_create_toolbar(int32_t window) {
    auto *f = getHandle<wxFrame>(window);
    if (!f) return 0;
    wxToolBar *tb = f->CreateToolBar();
    return allocHandle(tb);
}

int32_t liva_ui_toolbar_add_tool(int32_t tb, const char *label) {
    auto *t = getHandle<wxToolBar>(tb);
    if (!t) return 0;
    int id = g_nextCmdId++;
    // Label-only tool (no bitmap in Phase 2); use a null bitmap fallback.
    wxString lbl = wxString::FromUTF8(label ? label : "");
    auto *tool = t->AddTool(id, lbl, wxNullBitmap, lbl);
    int32_t h = allocHandle(tool);
    g_toolItems[h] = LivaToolItem{tool, id, t};
    return h;
}

void liva_ui_toolbar_add_separator(int32_t tb) {
    if (auto *t = getHandle<wxToolBar>(tb)) t->AddSeparator();
}

void liva_ui_toolbar_realize(int32_t tb) {
    if (auto *t = getHandle<wxToolBar>(tb)) t->Realize();
}

void liva_ui_tool_item_set_enabled(int32_t tool, int32_t enabled) {
    auto it = g_toolItems.find(tool);
    if (it != g_toolItems.end() && it->second.toolbar)
        it->second.toolbar->EnableTool(it->second.id, enabled != 0);
}

void liva_ui_tool_item_on_click(int32_t tool, void *func, void *env, int32_t size) {
    auto it = g_toolItems.find(tool);
    if (it == g_toolItems.end() || !func) return;
    void *owned = ownEnv(tool, env, size);
    auto fn = (LivaCallbackFn)func;
    auto &ti = it->second;
    int32_t th = tool;
    if (ti.toolbar)
        ti.toolbar->Bind(wxEVT_TOOL,
            [fn, owned, th](wxCommandEvent &) { if (fn) fn(owned, th); }, ti.id);
}
```

- [ ] **Step 7: Derle (livac + liva_runtime, wx hariç)**

Run: `cmake --build build-clang`
Expected: exit 0 (livac.exe + tüm test exe'leri linklenir). wx_runtime.cpp Ninja build'ine dahil değil — derlenmesi beklenmez; ama eklediğin C++'ın sözdizimsel doğruluğunu spec'e göre yazarak garanti et. Build hatasızsa devam.

- [ ] **Step 8: Commit**

```bash
git add stdlib/ui/wx_runtime.h stdlib/ui/wx_runtime.cpp
git commit -m "ui-runtime: menu/context-menu/statusbar/toolbar FFI with heap-owned envs"
```

---

## Task 2: Compiler — extern'ler, intrinsic eşlemeler, genelleştirilmiş fast-path

**Files:**
- Modify: `src/Sema/ModuleLoader.cpp`
- Modify: `src/IR/IRGen.cpp`
- Modify: `src/IR/IRGenCall.cpp`

- [ ] **Step 1: `std::ui` builtin adlarını ekle (`ModuleLoader.cpp`)**

`createBuiltinModule("std::ui", {...})` listesinde, `"setBounds"` satırının yakınına yeni bir grup ekle (Canvas grubundan önce, herhangi uygun yer):
```cpp
         // Menu / app frame (Phase 2)
         "createMenuBar", "createMenu", "menuAddItem", "menuAddCheckItem",
         "menuAddSeparator", "menuAddSubmenu", "menuBarAddMenu", "windowSetMenuBar",
         "menuItemSetEnabled", "menuItemSetChecked", "menuItemOnClick", "menuPopup",
         "onRightClick", "createStatusBar", "statusBarSetText",
         "createToolbar", "toolbarAddTool", "toolbarAddSeparator", "toolbarRealize",
         "toolItemSetEnabled", "toolItemOnClick",
```

- [ ] **Step 2: Extern kayıtlarını ekle (`IRGen.cpp`)**

`src/IR/IRGen.cpp`'de, `liva_ui_canvas_on_paint` extern kaydının (`uiCallbackTy` grubu, ~satır 1337) hemen ardına ekle. Mevcut yardımcı tipleri kullan (`voidTy`, `i32Ty`, `ptrTy`, `i8PtrTy`; `uiCallbackTy` = `{i32,ptr,ptr,i32}`):
```cpp
    // ── Phase 2: menu / statusbar / toolbar ──────────────────────────
    auto *uiRetI32Ty       = llvm::FunctionType::get(i32Ty, {}, false);
    auto *uiStrRetI32Ty    = llvm::FunctionType::get(i32Ty, {i8PtrTy}, false);
    auto *uiI32StrRetI32Ty = llvm::FunctionType::get(i32Ty, {i32Ty, i8PtrTy}, false);
    auto *uiI32I32RetI32Ty = llvm::FunctionType::get(i32Ty, {i32Ty, i32Ty}, false);
    auto *uiI32VoidTy      = llvm::FunctionType::get(voidTy, {i32Ty}, false);
    auto *uiI32I32VoidTy   = llvm::FunctionType::get(voidTy, {i32Ty, i32Ty}, false);
    auto *uiI32StrVoidTy   = llvm::FunctionType::get(voidTy, {i32Ty, i8PtrTy}, false);
    auto *uiI32StrI32VoidTy= llvm::FunctionType::get(voidTy, {i32Ty, i8PtrTy, i32Ty}, false);
    auto *uiI32I32StrVoidTy= llvm::FunctionType::get(voidTy, {i32Ty, i32Ty, i8PtrTy}, false);

    module_->getOrInsertFunction("liva_ui_create_menu_bar", uiRetI32Ty);
    module_->getOrInsertFunction("liva_ui_create_menu", uiStrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_menu_add_item", uiI32StrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_menu_add_check_item", uiI32StrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_menu_add_separator", uiI32VoidTy);
    module_->getOrInsertFunction("liva_ui_menu_add_submenu", uiI32StrI32VoidTy);
    module_->getOrInsertFunction("liva_ui_menu_bar_add_menu", uiI32I32VoidTy);
    module_->getOrInsertFunction("liva_ui_window_set_menu_bar", uiI32I32VoidTy);
    module_->getOrInsertFunction("liva_ui_menu_item_set_enabled", uiI32I32VoidTy);
    module_->getOrInsertFunction("liva_ui_menu_item_set_checked", uiI32I32VoidTy);
    module_->getOrInsertFunction("liva_ui_menu_item_on_click", uiCallbackTy);
    module_->getOrInsertFunction("liva_ui_menu_popup", uiI32I32VoidTy);
    module_->getOrInsertFunction("liva_ui_on_right_click", uiCallbackTy);
    module_->getOrInsertFunction("liva_ui_create_status_bar", uiI32I32RetI32Ty);
    module_->getOrInsertFunction("liva_ui_status_bar_set_text", uiI32I32StrVoidTy);
    module_->getOrInsertFunction("liva_ui_create_toolbar", uiI32VoidTy->getReturnType()
        ? llvm::FunctionType::get(i32Ty, {i32Ty}, false) : nullptr);
    module_->getOrInsertFunction("liva_ui_toolbar_add_tool", uiI32StrRetI32Ty);
    module_->getOrInsertFunction("liva_ui_toolbar_add_separator", uiI32VoidTy);
    module_->getOrInsertFunction("liva_ui_toolbar_realize", uiI32VoidTy);
    module_->getOrInsertFunction("liva_ui_tool_item_set_enabled", uiI32I32VoidTy);
    module_->getOrInsertFunction("liva_ui_tool_item_on_click", uiCallbackTy);
```
> Not: `create_toolbar` `(i32)->i32`. Yukarıdaki satırı sadeleştir:
> `module_->getOrInsertFunction("liva_ui_create_toolbar", llvm::FunctionType::get(i32Ty, {i32Ty}, false));`
> (Karışıklığı önlemek için ternary'yi kullanma; doğrudan bu satırı yaz.)

- [ ] **Step 3: Değer-döndüren ve void intrinsic eşlemelerini ekle (`IRGenCall.cpp`)**

`src/IR/IRGenCall.cpp`'de, `setBounds` intrinsic bloğunun (~satır 5122) ardına yeni intrinsic'leri ekle. **Closure-alan üçü hariç** (`menuItemOnClick`, `onRightClick`, `toolItemOnClick` — bunlar Step 5'te fast-path/ham yolla ele alınır), her FFI için bir blok. Örnek kalıplar (hepsini yaz):

```cpp
    // createMenuBar() -> i32
    if (funcName == "createMenuBar" && node->getArgs().empty()) {
        return builder_->CreateCall(getOrPanic("liva_ui_create_menu_bar"), {}, "ui.mb");
    }
    // createMenu(title) -> i32
    if (funcName == "createMenu" && node->getArgs().size() >= 1) {
        auto *title = visit(node->getArgs()[0].get());
        if (!title) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_menu"), {title}, "ui.menu");
    }
    // menuAddItem(menu, label) -> i32
    if (funcName == "menuAddItem" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        if (!m || !l) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_menu_add_item"), {m, l}, "ui.mi");
    }
    // menuAddCheckItem(menu, label) -> i32
    if (funcName == "menuAddCheckItem" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        if (!m || !l) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_menu_add_check_item"), {m, l}, "ui.mci");
    }
    // menuAddSeparator(menu) -> void
    if (funcName == "menuAddSeparator" && node->getArgs().size() >= 1) {
        auto *m = visit(node->getArgs()[0].get());
        if (!m) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_add_separator"), {m});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuAddSubmenu(menu, label, sub) -> void
    if (funcName == "menuAddSubmenu" && node->getArgs().size() >= 3) {
        auto *m = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        auto *s = visit(node->getArgs()[2].get());
        if (!m || !l || !s) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_add_submenu"), {m, l, s});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuBarAddMenu(bar, menu) -> void
    if (funcName == "menuBarAddMenu" && node->getArgs().size() >= 2) {
        auto *b = visit(node->getArgs()[0].get());
        auto *m = visit(node->getArgs()[1].get());
        if (!b || !m) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_bar_add_menu"), {b, m});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // windowSetMenuBar(window, bar) -> void
    if (funcName == "windowSetMenuBar" && node->getArgs().size() >= 2) {
        auto *w = visit(node->getArgs()[0].get());
        auto *b = visit(node->getArgs()[1].get());
        if (!w || !b) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_window_set_menu_bar"), {w, b});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuItemSetEnabled(item, enabled) -> void
    if (funcName == "menuItemSetEnabled" && node->getArgs().size() >= 2) {
        auto *i = visit(node->getArgs()[0].get());
        auto *e = visit(node->getArgs()[1].get());
        if (!i || !e) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_item_set_enabled"), {i, e});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuItemSetChecked(item, checked) -> void
    if (funcName == "menuItemSetChecked" && node->getArgs().size() >= 2) {
        auto *i = visit(node->getArgs()[0].get());
        auto *c = visit(node->getArgs()[1].get());
        if (!i || !c) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_item_set_checked"), {i, c});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // menuPopup(menu, target) -> void
    if (funcName == "menuPopup" && node->getArgs().size() >= 2) {
        auto *m = visit(node->getArgs()[0].get());
        auto *t = visit(node->getArgs()[1].get());
        if (!m || !t) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_menu_popup"), {m, t});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // createStatusBar(window, fieldCount) -> i32
    if (funcName == "createStatusBar" && node->getArgs().size() >= 2) {
        auto *w = visit(node->getArgs()[0].get());
        auto *fc = visit(node->getArgs()[1].get());
        if (!w || !fc) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_status_bar"), {w, fc}, "ui.sb");
    }
    // statusBarSetText(sb, field, text) -> void
    if (funcName == "statusBarSetText" && node->getArgs().size() >= 3) {
        auto *s = visit(node->getArgs()[0].get());
        auto *f = visit(node->getArgs()[1].get());
        auto *t = visit(node->getArgs()[2].get());
        if (!s || !f || !t) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_status_bar_set_text"), {s, f, t});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // createToolbar(window) -> i32
    if (funcName == "createToolbar" && node->getArgs().size() >= 1) {
        auto *w = visit(node->getArgs()[0].get());
        if (!w) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_create_toolbar"), {w}, "ui.tb");
    }
    // toolbarAddTool(tb, label) -> i32
    if (funcName == "toolbarAddTool" && node->getArgs().size() >= 2) {
        auto *tb = visit(node->getArgs()[0].get());
        auto *l = visit(node->getArgs()[1].get());
        if (!tb || !l) return nullptr;
        return builder_->CreateCall(getOrPanic("liva_ui_toolbar_add_tool"), {tb, l}, "ui.tool");
    }
    // toolbarAddSeparator(tb) -> void
    if (funcName == "toolbarAddSeparator" && node->getArgs().size() >= 1) {
        auto *tb = visit(node->getArgs()[0].get());
        if (!tb) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_toolbar_add_separator"), {tb});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // toolbarRealize(tb) -> void
    if (funcName == "toolbarRealize" && node->getArgs().size() >= 1) {
        auto *tb = visit(node->getArgs()[0].get());
        if (!tb) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_toolbar_realize"), {tb});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
    // toolItemSetEnabled(tool, enabled) -> void
    if (funcName == "toolItemSetEnabled" && node->getArgs().size() >= 2) {
        auto *t = visit(node->getArgs()[0].get());
        auto *e = visit(node->getArgs()[1].get());
        if (!t || !e) return nullptr;
        builder_->CreateCall(getOrPanic("liva_ui_tool_item_set_enabled"), {t, e});
        return llvm::Constant::getNullValue(builder_->getInt32Ty());
    }
```

- [ ] **Step 4: Closure-alan menü/tool/right-click intrinsic'lerini ham yolla ekle (`IRGenCall.cpp`)**

Yukarıdaki bloğun ardına, **serbest-fonksiyon** biçimleri için (sınıf metodu içinden çağrılırlar, stack-env size 0) ekle. Bunlar `emitCallbackCall` ile aynı kalıbı izler:
```cpp
    if (funcName == "menuItemOnClick" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_menu_item_on_click", 0, 1);
    if (funcName == "toolItemOnClick" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_tool_item_on_click", 0, 1);
    if (funcName == "onRightClick" && node->getArgs().size() >= 2)
        return emitCallbackCall("liva_ui_on_right_click", 0, 1);
```
> `emitCallbackCall` Faz 1'de zaten 4-arg (size 0) çağrı üretir; bu serbest-fonksiyon yolu doğru.

- [ ] **Step 4b: `addItem`/`addCheckItem`/`addTool` kısa-yol intrinsic'i (dangling-güvenli birincil API)**

`recv.addItem(label, <closure literal>)` (alıcı `Menu`) ve `recv.addTool(label, <closure literal>)` (alıcı `Toolbar`) — closure literali `addItem` PARAMETRESİNE bağlandığından normal yol heap-own alamaz; bu yüzden bu kısa-yolu Step 5'teki fast-path bloğunun İÇİNDE (closure-literal kontrolünden sonra, `pickFFI`'den önce) özel ele al. Step 5'in genelleştirilmiş bloğunun başına (recvClass hesaplandıktan hemen sonra) şu özel-durumu ekle:

```cpp
            // addItem/addCheckItem(label, closure) on Menu, addTool on Toolbar:
            // the closure literal is the 2nd arg. Add the item, then heap-own
            // the closure onto the new item, and return a MenuItem/ToolItem.
            {
                const char *addFn = nullptr;   // FFI to create the item
                const char *bindFn = nullptr;  // FFI to bind the click
                const char *itemClass = nullptr;
                if (recvClass == "Menu" && methodName == "addItem") {
                    addFn = "liva_ui_menu_add_item";
                    bindFn = "liva_ui_menu_item_on_click"; itemClass = "MenuItem";
                } else if (recvClass == "Menu" && methodName == "addCheckItem") {
                    addFn = "liva_ui_menu_add_check_item";
                    bindFn = "liva_ui_menu_item_on_click"; itemClass = "MenuItem";
                } else if (recvClass == "Toolbar" && methodName == "addTool") {
                    addFn = "liva_ui_toolbar_add_tool";
                    bindFn = "liva_ui_tool_item_on_click"; itemClass = "ToolItem";
                }
                if (addFn && node->getArgs().size() == 2 &&
                    node->getArgs()[1]->getKind() == ASTNode::NodeKind::ClosureExpr) {
                    auto rcIt2 = classTypes_.find(recvClass);
                    if (rcIt2 != classTypes_.end()) {
                        auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                        auto *i32Ty = builder_->getInt32Ty();
                        // recv handle (field idx 1)
                        llvm::Value *selfPtr = visit(memberExpr->getObject());
                        if (!selfPtr) return nullptr;
                        auto *hGEP = builder_->CreateStructGEP(rcIt2->second, selfPtr, 1, "rh.gep");
                        auto *rh = builder_->CreateLoad(i32Ty, hGEP, "rh");
                        // label
                        auto *label = visit(node->getArgs()[0].get());
                        if (!label) return nullptr;
                        // create item -> item handle
                        auto *itemH = builder_->CreateCall(getOrPanic(addFn), {rh, label}, "item.h");
                        // closure literal -> fat ptr + env size
                        lastClosureEnvSize_ = 0;
                        auto *closureVal = visit(node->getArgs()[1].get());
                        if (!closureVal) return nullptr;
                        uint64_t envSize = lastClosureEnvSize_;
                        auto *closureObjTy = getClosureObjTy();
                        auto *cbA = createEntryBlockAlloca(
                            builder_->GetInsertBlock()->getParent(), "cb.tmp", closureObjTy);
                        builder_->CreateStore(closureVal, cbA);
                        auto *fnPtr = builder_->CreateLoad(
                            ptrTy, builder_->CreateStructGEP(closureObjTy, cbA, 0));
                        auto *envPtr = builder_->CreateLoad(
                            ptrTy, builder_->CreateStructGEP(closureObjTy, cbA, 1));
                        builder_->CreateCall(getOrPanic(bindFn),
                            {itemH, fnPtr, envPtr, llvm::ConstantInt::get(i32Ty, envSize)});
                        // construct MenuItem/ToolItem(itemH) via mangled init
                        auto *ctor = module_->getFunction(std::string(itemClass) + "_init");
                        if (ctor)
                            return builder_->CreateCall(ctor, {itemH}, "item.obj");
                        return itemH; // fallback: raw handle (still i32-compatible)
                    }
                }
            }
```
> `MenuItem_init`/`ToolItem_init`, 1-arg (`h: i32`) init'in mangled adıdır (Faz 1 class inşa deseni: `ClassName(args)` → `ClassName_init(args)`). Bu intrinsic, stdlib'deki `Menu.addItem` metodunu (Task 3) BAYPAS eder; o metot yalnız non-literal arg için fallback olarak kalır.

- [ ] **Step 5: Fast-path'i genelleştir (`IRGenCall.cpp` ~satır 78)**

Mevcut UI event fast-path bloğunu (satır 78-136) şu genelleştirilmiş sürümle DEĞİŞTİR. Değişiklikler: (a) event-metot→FFI eşlemesi alıcı tipine göre; (b) `isControlDescendant` yerine "handle alanı idx1'de" kontrolü (Control + MenuItem + ToolItem); (c) handle GEP alıcının kendi class-type'ından:
```cpp
        // ── UI event-method fast path: heap-own inline closure envs ───────
        // widget.onClick / onChange / onSelect / onKey / onRightClick and
        // MenuItem.onClick / ToolItem.onClick, when the argument is a closure
        // LITERAL. We intercept it, compute the captured-env size, and pass it
        // so the runtime heap-copies the env (freed on widget/item destroy).
        // Receiver classes all store the wx handle as `i32 handle` at field
        // index 0 (LLVM struct index 1, after the vtable). Non-literal args
        // fall through to the ordinary method (stack env).
        if (node->getArgs().size() == 1 &&
            node->getArgs()[0]->getKind() == ASTNode::NodeKind::ClosureExpr) {
            std::string recvClass = resolveExprClassTypeName(memberExpr->getObject());
            // (receiver class name, event method) -> runtime FFI.
            auto pickFFI = [&](const std::string &cls,
                               const std::string &m) -> const char * {
                auto isControlDescendant = [&](std::string tn) -> bool {
                    for (int i = 0; i < 64 && !tn.empty(); ++i) {
                        if (tn == "Control") return true;
                        auto pit = classParent_.find(tn);
                        if (pit == classParent_.end()) return false;
                        tn = pit->second;
                    }
                    return false;
                };
                if (cls == "MenuItem" && m == "onClick") return "liva_ui_menu_item_on_click";
                if (cls == "ToolItem" && m == "onClick") return "liva_ui_tool_item_on_click";
                if (isControlDescendant(cls)) {
                    if (m == "onClick")      return "liva_ui_on_click";
                    if (m == "onChange")     return "liva_ui_on_change";
                    if (m == "onSelect")     return "liva_ui_on_select";
                    if (m == "onKey")        return "liva_ui_on_key";
                    if (m == "onRightClick") return "liva_ui_on_right_click";
                }
                return nullptr;
            };
            const char *cFn = recvClass.empty() ? nullptr : pickFFI(recvClass, methodName);
            auto rcIt = recvClass.empty() ? classTypes_.end()
                                          : classTypes_.find(recvClass);
            if (cFn && rcIt != classTypes_.end()) {
                auto *ptrTy = llvm::PointerType::getUnqual(*context_);
                llvm::Value *selfPtr = visit(memberExpr->getObject());
                if (!selfPtr) return nullptr;
                // handle: first field → struct index 1 (vtable at 0).
                auto *handleGEP = builder_->CreateStructGEP(
                    rcIt->second, selfPtr, 1, "handle.gep");
                auto *handle = builder_->CreateLoad(
                    builder_->getInt32Ty(), handleGEP, "handle");
                lastClosureEnvSize_ = 0;
                auto *closureVal = visit(node->getArgs()[0].get());
                if (!closureVal) return nullptr;
                uint64_t envSize = lastClosureEnvSize_;
                auto *closureObjTy = getClosureObjTy();
                auto *cbAlloca = createEntryBlockAlloca(
                    builder_->GetInsertBlock()->getParent(), "cb.tmp", closureObjTy);
                builder_->CreateStore(closureVal, cbAlloca);
                auto *fnPtr = builder_->CreateLoad(
                    ptrTy, builder_->CreateStructGEP(closureObjTy, cbAlloca, 0));
                auto *envPtr = builder_->CreateLoad(
                    ptrTy, builder_->CreateStructGEP(closureObjTy, cbAlloca, 1));
                auto *i32Ty = builder_->getInt32Ty();
                builder_->CreateCall(
                    getOrPanic(cFn),
                    {handle, fnPtr, envPtr, llvm::ConstantInt::get(i32Ty, envSize)});
                return llvm::Constant::getNullValue(i32Ty);
            }
        }
```

- [ ] **Step 6: Derle**

Run: `cmake --build build-clang --target livac`
Expected: exit 0. Hata varsa düzelt (özellikle Step 2'deki extern imza tiplerini ve `i8PtrTy` adının mevcut olduğunu doğrula — yoksa `llvm::PointerType::getUnqual(*context_)` kullan).

- [ ] **Step 7: Commit**

```bash
git add src/Sema/ModuleLoader.cpp src/IR/IRGen.cpp src/IR/IRGenCall.cpp
git commit -m "irgen: menu/statusbar/toolbar intrinsics + generalize event fast-path to MenuItem/ToolItem"
```

---

## Task 3: stdlib — menü sınıf API + Window/Control/Toolbar/StatusBar

**Files:**
- Create: `stdlib/ui/menu.liva`
- Modify: `stdlib/ui/widgets.liva`
- Test: `tests/unit/UICodegenExecTest.cpp` (TDD test önce)

- [ ] **Step 1: Başarısız emit-ir testini yaz (`UICodegenExecTest.cpp`)**

Dosyanın sonundaki `#endif // LIVA_HAS_LLVM`'den önce ekle:
```cpp
TEST(UICodegenExec, MenuSystemCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "import ui::menu\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(640, 480, \"T\")\n"
        "  let m = Menu(\"Dosya\")\n"
        "  let item = m.addItem(\"Ac\", |_h: i32| { messageBox(\"i\", \"a\", 1) })\n"
        "  item.setEnabled(false)\n"
        "  m.addSeparator()\n"
        "  m.addCheckItem(\"Kalin\", |_h: i32| { })\n"
        "  let mb = MenuBar()\n"
        "  mb.addMenu(m)\n"
        "  win.setMenuBar(mb)\n"
        "}\n",
        "menu_system");
    EXPECT_TRUE(emitsClean(ir));
}
```

- [ ] **Step 2: Testin başarısız olduğunu doğrula**

Run: `cmake --build build-clang --target ui_codegen_exec_test && ctest --test-dir build-clang -R UICodegenExec.MenuSystemCompiles --output-on-failure`
Expected: FAIL (`Menu`/`MenuBar` çözülemez — `ui::menu` yok).

- [ ] **Step 3: `menu.liva`'yı oluştur**

```liva
// std::ui menu — MenuBar / Menu / MenuItem (VCL-style)
// Import with: import ui::menu
import std::ui
import ui::widgets

// MenuItem — wxMenuItem (NOT a wxWindow); handle at field index 0.
pub class MenuItem {
    var handle: i32
    init(h: i32) { self.handle = h }
    pub func onClick(cb: (i32) -> void) { menuItemOnClick(self.handle, cb) }
    pub func setEnabled(b: bool) {
        var v = 0
        if b { v = 1 }
        menuItemSetEnabled(self.handle, v)
    }
    pub func setChecked(b: bool) {
        var v = 0
        if b { v = 1 }
        menuItemSetChecked(self.handle, v)
    }
}

// Menu — wxMenu. addItem/addCheckItem return a MenuItem (short-cut + object).
pub class Menu {
    var handle: i32
    init(title: string) { self.handle = createMenu(title) }
    pub func addItem(label: string, cb: (i32) -> void) -> MenuItem {
        let h = menuAddItem(self.handle, label)
        let item = MenuItem(h)
        item.onClick(cb)
        return item
    }
    pub func addCheckItem(label: string, cb: (i32) -> void) -> MenuItem {
        let h = menuAddCheckItem(self.handle, label)
        let item = MenuItem(h)
        item.onClick(cb)
        return item
    }
    pub func addSeparator() { menuAddSeparator(self.handle) }
    pub func addSubmenu(label: string, sub: Menu) { menuAddSubmenu(self.handle, label, sub.handle) }
    pub func popup(target: Control) { menuPopup(self.handle, target.handle) }
}

// MenuBar — wxMenuBar.
pub class MenuBar {
    var handle: i32
    init() { self.handle = createMenuBar() }
    pub func addMenu(menu: Menu) { menuBarAddMenu(self.handle, menu.handle) }
}
```
> Not: Bu `Menu.addItem` stdlib METODU yalnız **non-literal** argüman için bir fallback'tir (closure önce değişkene atanmışsa). **Birincil yol** `menu.addItem("x", |_|{...})` literalidir ve Task 2 Step 4b'deki intrinsic tarafından yakalanır → heap-env ile bind edilir (dangling-güvenli), bu stdlib metodunu baypas eder. İkisi de `MenuItem` döndürür, davranış tutarlıdır. (`item.onClick(cb)` içindeki `cb` parametresi non-literal olduğundan stdlib metodu çağrılır — fallback ile tutarlı.)

- [ ] **Step 4: `widgets.liva`'ya Window/Control/Toolbar/StatusBar ekle**

`widgets.liva`'da `Control` sınıfına (event metotlarının yanına) ekle:
```liva
    pub func onRightClick(cb: (i32, i32) -> void) { onRightClick(self.handle, cb) }
```

`Window` sınıfına ekle:
```liva
    pub func setMenuBar(mb: MenuBar) { windowSetMenuBar(self.handle, mb.handle) }
    pub func setStatusBar(fieldCount: i32) -> StatusBar {
        return StatusBar(createStatusBar(self.handle, fieldCount))
    }
```
> `Window` `MenuBar`/`StatusBar`'a atıfta bulunur. `MenuBar` `menu.liva`'da; `widgets.liva` `menu.liva`'yı import EDEMEZ (döngü: menu.liva zaten widgets'i import ediyor). Çözüm: `setMenuBar` parametresini `mb: MenuBar` yerine ham handle almaktan kaçınmak için, `StatusBar` sınıfını `widgets.liva`'da tanımla (aşağıda) ve `setMenuBar`'ı `menu.liva`'da `MenuBar`'a bir extension olarak değil — bunun yerine `Window.setMenuBar`'ı `menu.liva`'ya taşımak da döngü yaratır. **Karar:** `setMenuBar` `widgets.liva`'da kalır ama parametresi `mb: MenuBar` yerine yapısal olarak `mbHandle: i32` alır; `menu.liva` kullanıcıya `win.setMenuBar(mb.handle)` yerine kolaylık sağlamaz. DAHA İYİ KARAR (uygulanacak): `MenuBar`/`Menu`/`MenuItem` sınıflarını da `widgets.liva`'ya koy (menu.liva'yı iptal et), böylece tek modülde döngü olmaz. Aşağıdaki Step 5 bunu uygular.

- [ ] **Step 5: Menü sınıflarını `widgets.liva`'ya taşı (döngüsel import'tan kaçın), `menu.liva`'yı re-export yap**

`menu.liva` içeriğini (MenuItem/Menu/MenuBar sınıfları) `widgets.liva`'nın sonuna taşı (Control/Window'dan SONRA, böylece `Control` görünür). `widgets.liva`'ya ayrıca ekle:
```liva
// ── StatusBar (wxStatusBar) ────────────────────────────────────
pub class StatusBar {
    var handle: i32
    init(h: i32) { self.handle = h }
    pub func setText(field: i32, text: string) { statusBarSetText(self.handle, field, text) }
}

// ── ToolItem (wxToolBarToolBase) ───────────────────────────────
pub class ToolItem {
    var handle: i32
    init(h: i32) { self.handle = h }
    pub func onClick(cb: (i32) -> void) { toolItemOnClick(self.handle, cb) }
    pub func setEnabled(b: bool) {
        var v = 0
        if b { v = 1 }
        toolItemSetEnabled(self.handle, v)
    }
}

// ── Toolbar (wxToolBar — a wxWindow) ───────────────────────────
pub class Toolbar: Control {
    init(parent: Control) { super.init(createToolbar(parent.handle)) }
    pub func addTool(label: string, cb: (i32) -> void) -> ToolItem {
        let h = toolbarAddTool(self.handle, label)
        let t = ToolItem(h)
        t.onClick(cb)
        return t
    }
    pub func addSeparator() { toolbarAddSeparator(self.handle) }
    pub func realize() { toolbarRealize(self.handle) }
}
```
Ve `Window`'a `setMenuBar`/`setStatusBar` ile `Control`'e `onRightClick` (Step 4'teki gibi) — hepsi artık aynı dosyada, döngü yok.

`menu.liva` dosyasını, geriye-uyumlu bir re-export'a indir (kullanıcı `import ui::menu` yazabilsin):
```liva
// std::ui menu — re-exports the menu classes from ui::widgets.
// (Menu/MenuBar/MenuItem live in ui::widgets to avoid an import cycle with
// Window.setMenuBar / Control.onRightClick.)
// Import with: import ui::menu   (or just import ui::widgets)
import ui::widgets
```

- [ ] **Step 6: Testi geçir**

Run: `cmake --build build-clang --target ui_codegen_exec_test && ctest --test-dir build-clang -R UICodegenExec.MenuSystemCompiles --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add stdlib/ui/widgets.liva stdlib/ui/menu.liva tests/unit/UICodegenExecTest.cpp
git commit -m "ui: MenuBar/Menu/MenuItem + StatusBar/Toolbar/ToolItem class API"
```

---

## Task 4: Testler (heap-env, context menu, statusbar, toolbar) + örnek + regresyon

**Files:**
- Modify: `tests/unit/UICodegenExecTest.cpp`
- Create: `examples/ui_menu_demo.liva`

- [ ] **Step 1: heap-env + context menu + toolbar/statusbar testlerini yaz**

`UICodegenExecTest.cpp` sonuna (`#endif`'ten önce) ekle. `anyOnClickHeapOwns`/`hasOnClickCall` Faz 1'de `liva_ui_on_click`'e özeldi; menü için yeni bir yardımcı kullan — doğrudan IR'da menü FFI çağrısının non-zero size'ını ara:
```cpp
// True if any liva_ui_menu_item_on_click call passes a non-zero env size.
static bool anyMenuItemHeapOwns(const std::string &ir) {
    const std::string needle = "call void @liva_ui_menu_item_on_click(";
    for (size_t p = ir.find(needle); p != std::string::npos;
         p = ir.find(needle, p + 1)) {
        size_t end = ir.find(')', p);
        if (end == std::string::npos) break;
        if (ir.substr(p, end - p + 1).find(", i32 0)") == std::string::npos)
            return true;
    }
    return false;
}

TEST(UICodegenExec, MenuAddItemInlineHeapOwns) {
    // The primary short-cut menu.addItem("x", |..|{..}) with a capture must
    // heap-own the env (the addItem intrinsic binds the literal directly),
    // even inside a helper that returns the item.
    auto ir = emitIR(
        "import ui::widgets\n"
        "func build() -> MenuItem {\n"
        "  var n = 0\n"
        "  let m = Menu(\"M\")\n"
        "  let item = m.addItem(\"x\", |_h: i32| { n = n + 1 })\n"
        "  return item\n"
        "}\n"
        "func main() { appInit(); let i = build() }\n",
        "menu_additem_heap");
    ASSERT_TRUE(emitsClean(ir));
    EXPECT_TRUE(anyMenuItemHeapOwns(ir))
        << "menu.addItem inline closure literal must heap-own the env";
}

TEST(UICodegenExec, ContextMenuCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let panel = Panel(win)\n"
        "  panel.onRightClick(|x: i32, y: i32| {\n"
        "    let ctx = Menu(\"\")\n"
        "    ctx.addItem(\"Kopyala\", |_h: i32| { })\n"
        "    ctx.popup(panel)\n"
        "  })\n"
        "}\n",
        "context_menu");
    EXPECT_TRUE(emitsClean(ir));
}

TEST(UICodegenExec, StatusBarAndToolbarCompile) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let win = Window(400, 300, \"T\")\n"
        "  let sb = win.setStatusBar(2)\n"
        "  sb.setText(0, \"Hazir\")\n"
        "  let tb = Toolbar(win)\n"
        "  let t = tb.addTool(\"Yeni\", |_h: i32| { })\n"
        "  t.setEnabled(true)\n"
        "  tb.addSeparator()\n"
        "  tb.realize()\n"
        "}\n",
        "statusbar_toolbar");
    EXPECT_TRUE(emitsClean(ir));
}
```

- [ ] **Step 2: Testleri derle ve geçir**

Run: `cmake --build build-clang --target ui_codegen_exec_test && ctest --test-dir build-clang -R "UICodegenExec.MenuAddItemInlineHeapOwns|UICodegenExec.ContextMenuCompiles|UICodegenExec.StatusBarAndToolbarCompile" --output-on-failure`
Expected: 3/3 PASS. (Eğer `MenuAddItemInlineHeapOwns` FAIL ederse: Task 2 Step 4b intrinsic'inin `Menu.addItem` literalini yakaladığını ve `m`'in `varClassTypes`'a `Menu` olarak kaydolduğunu doğrula.)

- [ ] **Step 3: `ui_menu_demo.liva` örneğini oluştur**

```liva
// Menu / Toolbar / StatusBar / Context-menu demo — VCL-style app frame
// Build: livac examples/ui_menu_demo.liva -o menu_demo
import ui::widgets

func main() {
    appInit()
    let win = Window(640, 480, "Liva Editor")

    // Menu bar
    let fileMenu = Menu("Dosya")
    fileMenu.addItem("Yeni", |_h: i32| { messageBox("Bilgi", "Yeni", 1) })
    let saveItem = fileMenu.addItem("Kaydet", |_h: i32| { messageBox("Bilgi", "Kaydet", 1) })
    saveItem.setEnabled(false)
    fileMenu.addSeparator()
    fileMenu.addItem("Cikis", |_h: i32| { appQuit() })

    let viewMenu = Menu("Gorunum")
    viewMenu.addCheckItem("Durum Cubugu", |_h: i32| { })

    let mb = MenuBar()
    mb.addMenu(fileMenu)
    mb.addMenu(viewMenu)
    win.setMenuBar(mb)

    // Status bar
    let sb = win.setStatusBar(2)
    sb.setText(0, "Hazir")
    sb.setText(1, "Satir 1")

    // Toolbar
    let tb = Toolbar(win)
    tb.addTool("Yeni", |_h: i32| { messageBox("Bilgi", "Toolbar Yeni", 1) })
    tb.addSeparator()
    tb.addTool("Ac", |_h: i32| { messageBox("Bilgi", "Toolbar Ac", 1) })
    tb.realize()

    // Content + context menu
    let panel = Panel(win)
    panel.onRightClick(|x: i32, y: i32| {
        let ctx = Menu("")
        ctx.addItem("Kopyala", |_h: i32| { messageBox("Bilgi", "Kopyala", 1) })
        ctx.addItem("Yapistir", |_h: i32| { messageBox("Bilgi", "Yapistir", 1) })
        ctx.popup(panel)
    })

    win.show()
    appRun()
}
```

- [ ] **Step 4: Örneği emit-ir ile doğrula**

Run: `build-clang/livac.exe --emit-ir -o build-clang/_menu.ll examples/ui_menu_demo.liva`
Expected: exit 0, `.ll` üretilir, stderr'de `error:` yok. Sonra: `del build-clang\_menu.ll` (PowerShell: `Remove-Item build-clang\_menu.ll`).

- [ ] **Step 5: Tam regresyon**

Run: `cmake --build build-clang && ctest --test-dir build-clang`
Expected: yalnız `RuntimeExecTest.BTreeMapI64OrderedInsertGet` ve `RuntimeExecTest.BTreeMapStrLookup` FAIL (önceden var olan btree WIP). Başka FAIL varsa düzelt.

- [ ] **Step 6: Commit**

```bash
git add tests/unit/UICodegenExecTest.cpp examples/ui_menu_demo.liva
git commit -m "test+example: menu/context-menu/statusbar/toolbar IR-emit tests + ui_menu_demo"
```

---

## Self-Review Notları (yazım sırasında doğrulandı)

- **Spec kapsamı:** §4 sınıf API → Task 3; §5 runtime → Task 1; §6 derleyici (extern/intrinsic/fast-path) → Task 2; §7 test+örnek → Task 4. Tümü karşılandı.
- **Tip tutarlılığı:** FFI adları `.h`/IRGen extern/IRGenCall intrinsic/stdlib çağrısı arasında birebir aynı (`liva_ui_menu_*`, `liva_ui_tool_*`, `liva_ui_on_right_click`, `liva_ui_status_bar_*`). Builtin adlar (`menuAddItem` vb.) ModuleLoader + IRGenCall + stdlib arasında aynı. Sınıf adları (`MenuItem`/`Menu`/`MenuBar`/`StatusBar`/`Toolbar`/`ToolItem`) fast-path + stdlib arasında tutarlı.
- **Döngüsel import çözümü:** Menü sınıfları `widgets.liva`'da (Window/Control ile aynı dosya), `menu.liva` re-export — Task 3 Step 5'te netleştirildi.
- **Birincil API dangling-güvenli:** `menu.addItem("x", |_|{...})` / `menu.addCheckItem` / `toolbar.addTool` literalleri Task 2 Step 4b intrinsic'i ile heap-env alır (return-eden helper'da bile güvenli). `MenuAddItemInlineHeapOwns` testi bunu doğrular. Yalnız closure önce değişkene atanırsa (non-literal) stdlib metoduna düşer ve stack-env kullanır (Faz-1 ile tutarlı belgelenmiş sınır).
- **`MenuItem_init`/`ToolItem_init` bağımlılığı:** Step 4b intrinsic'i `MenuItem(h)` inşası için mangled `MenuItem_init` fonksiyonunu çağırır; bu fonksiyon `MenuItem` sınıfı (Task 3) derlendiğinde modülde mevcut olur. Sınıf tanımı stdlib'de `import ui::widgets` ile her UI programına dahil olduğundan kayıt sıralaması güvenli.
- **Risk:** Step 2 (IRGen extern) imza tipleri — `i8PtrTy` adının mevcut olduğunu derleme sırasında doğrula; yoksa `llvm::PointerType::getUnqual(*context_)`. Menü item event'i için late-bind (owner frame `setMenuBar`'da çözülür) runtime'da ele alındı.
