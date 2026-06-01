# Faz 2 — Menü Sistemi & Uygulama Çerçevesi

**Tarih:** 2026-06-01
**Durum:** Tasarım onaylandı, implementasyon planı bekliyor
**Branch:** `feat/ui-vcl-phase2` (Faz 1 üzerine)
**Kapsam:** wxWidgets tabanlı `ui::widgets` kütüphanesine VCL-tarzı uygulama çerçevesi eklemek — menü çubuğu, context menu, durum çubuğu, araç çubuğu.

---

## 1. Amaç ve Bağlam

Faz 1, VCL-tarzı sınıf hiyerarşisini (`Control` + 18 widget alt sınıfı, Swift-init inşa, inline-closure heap-env callback'ler) kurdu. Faz 2, bir masaüstü uygulamasının "çerçevesini" tamamlar: bir uygulamayı VCL hissi veren yapan menü çubuğu, sağ-tık context menüsü, durum çubuğu ve araç çubuğu.

Faz 1 spec'i (`2026-05-31-ui-vcl-phase1-design.md`) bu işi Faz 2'ye bırakmıştı. Faz 2'nin tamamı tek dilim halinde menü sistemine odaklanır; yeni widget'lar (TreeView/Grid) ve layout (Align/Anchors) ayrı, sonraki spec'lere bırakılır.

### Faz 1'den devralınan altyapı
- **Callback heap-env fast-path:** inline closure literal'i `widget.onClick(|..|{..})` ile bind edilince env heap'e kopyalanır, widget destroy'da serbest bırakılır (`ownEnv`/`g_widgetEnvs`). Menü item'ları bunu yeniden kullanır.
- **Callback ABI:** `liva_ui_on_*(handle, func, env, i32 size)` 4-arg.
- **Event payload deseni:** `onKey` callback'e `keycode` geçirir; `onRightClick` aynı desenle `(x, y)` geçirecek.
- **Genel intrinsic deseni:** `ModuleLoader` builtin adı → `IRGenCall` intrinsic → `IRGen.cpp` extern → `wx_runtime.cpp` FFI.

### Kritik teknik kısıt
wxWidgets'te menü item'ları (`wxMenuItem`) ve toolbar tool'ları `wxWindow` **değildir** — Faz 1'in `liva_ui_on_click`'i `getHandle<wxWindow>` + `w->Bind` yapar, bu menü item'lara uymaz. Menü/tool event'leri ID-tabanlıdır ve Frame seviyesinde bind edilir. Bu yüzden menü/tool için ayrı FFI ve ayrı bind mekanizması gerekir; ve Faz 1'in "alıcı Control-türevi" fast-path kontrolü genelleştirilmelidir.

---

## 2. Alınan Tasarım Kararları

| Karar | Seçim |
|-------|-------|
| Faz 2 kapsamı | Yalnız menü sistemi dilimi (yeni widget/layout sonraki spec'ler) |
| Bileşenler | MenuBar/Menu/MenuItem + context menu + StatusBar + Toolbar (dördü de) |
| Menü API tarzı | Nesne tabanlı, Faz 1 closure'larıyla |
| Birincil ergonomi | Kısa yol her yerde: `menu.addItem(label, cb) -> MenuItem`, `toolbar.addTool(label, cb) -> ToolItem`; dönüş nesnesi gerekince kullanılır |
| Menü callback ömrü | Menü için ayrı FFI + Faz 1 heap-env mekanizması |
| Fast-path | Genelleştir: "alıcı = `handle: i32` alanı struct-index 1'de olan herhangi bir sınıf + tanınan event metodu" (Control + MenuItem + ToolItem) |
| Branch | Ayrı `feat/ui-vcl-phase2` (Faz 1 üzerine) |

---

## 3. Mimari

Faz 1'in 3 katmanı korunur:
```
Kullanıcı kodu  ── import ui::widgets
    ▼
ui::widgets (yeni: MenuBar, Menu, MenuItem, Toolbar, ToolItem, StatusBar;
             Window/Control'e yeni metotlar)
    ▼
liva_ui_menu_* / toolbar_* / status_bar_* / on_right_click intrinsic'leri
    ▼
wx_runtime.cpp (wxMenuBar / wxMenu / wxMenuItem / wxToolBar / wxStatusBar)
```

Tüm yeni nesneler mevcut `void*` handle tablosunda saklanır (sizer'lar için zaten var olan "wxWindow olmayan handle" precedent'i izlenir).

---

## 4. Sınıf API'si

```liva
// ── MenuItem — wxWindow DEĞİL; handle struct-index 1'de (genelleştirilmiş fast-path) ──
pub class MenuItem {
    var handle: i32
    init(h: i32) { self.handle = h }   // dahili; doğrudan inşa edilmez
    pub func onClick(cb: (i32) -> void) { menuItemOnClick(self.handle, cb) }  // intrinsic, heap-env
    pub func setEnabled(b: bool) { menuItemSetEnabled(self.handle, b) }
    pub func setChecked(b: bool) { menuItemSetChecked(self.handle, b) }       // checkable
}

// ── Menu ──────────────────────────────────────────────────────────────
pub class Menu {
    var handle: i32
    init(title: string) { self.handle = createMenu(title) }
    pub func addItem(label: string, cb: (i32) -> void) -> MenuItem {
        let h = menuAddItem(self.handle, label)
        menuItemOnClick(h, cb)
        return MenuItem(h)
    }
    pub func addCheckItem(label: string, cb: (i32) -> void) -> MenuItem {
        let h = menuAddCheckItem(self.handle, label)
        menuItemOnClick(h, cb)
        return MenuItem(h)
    }
    pub func addSeparator() { menuAddSeparator(self.handle) }
    pub func addSubmenu(label: string, sub: Menu) { menuAddSubmenu(self.handle, label, sub.handle) }
    pub func popup(target: Control) { menuPopup(self.handle, target.handle) }  // context menu
}

// ── MenuBar ───────────────────────────────────────────────────────────
pub class MenuBar {
    var handle: i32
    init() { self.handle = createMenuBar() }
    pub func addMenu(menu: Menu) { menuBarAddMenu(self.handle, menu.handle) }
}

// ── StatusBar ─────────────────────────────────────────────────────────
pub class StatusBar {
    var handle: i32
    init(h: i32) { self.handle = h }
    pub func setText(field: i32, text: string) { statusBarSetText(self.handle, field, text) }
}

// ── ToolItem ──────────────────────────────────────────────────────────
pub class ToolItem {
    var handle: i32
    init(h: i32) { self.handle = h }
    pub func onClick(cb: (i32) -> void) { toolItemOnClick(self.handle, cb) }
    pub func setEnabled(b: bool) { toolItemSetEnabled(self.handle, b) }
}

// ── Toolbar (wxToolBar — bir wxWindow; Control alt sınıfı) ─────────────
pub class Toolbar: Control {
    init(parent: Control) { super.init(createToolbar(parent.handle)) }
    pub func addTool(label: string, cb: (i32) -> void) -> ToolItem {
        let h = toolbarAddTool(self.handle, label)
        toolItemOnClick(h, cb)
        return ToolItem(h)
    }
    pub func addSeparator() { toolbarAddSeparator(self.handle) }
    pub func realize() { toolbarRealize(self.handle) }   // wxToolBar::Realize, eklemeler bitince
}

// ── Window'a eklenen metotlar ─────────────────────────────────────────
//   pub func setMenuBar(mb: MenuBar)
//   pub func setStatusBar(fieldCount: i32) -> StatusBar
// ── Control'e eklenen metot ───────────────────────────────────────────
//   pub func onRightClick(cb: (i32, i32) -> void)   // (x, y) ekran-içi koordinat
```

### Kullanım örneği
```liva
import ui::widgets
func main() {
    appInit()
    let win = Window(640, 480, "Editör")

    let fileMenu = Menu("Dosya")
    fileMenu.addItem("Aç", |_| { messageBox("Bilgi", "Aç", 1) })
    let saveItem = fileMenu.addItem("Kaydet", |_| { save() })
    saveItem.setEnabled(false)
    fileMenu.addSeparator()
    fileMenu.addItem("Çıkış", |_| { appQuit() })

    let mb = MenuBar()
    mb.addMenu(fileMenu)
    win.setMenuBar(mb)

    let sb = win.setStatusBar(2)
    sb.setText(0, "Hazır")

    let tb = Toolbar(win)
    tb.addTool("Yeni", |_| { newDoc() })
    tb.realize()

    let panel = Panel(win)
    panel.onRightClick(|x, y| {
        let ctx = Menu("")
        ctx.addItem("Kopyala", |_| { copy() })
        ctx.popup(panel)
    })

    win.show()
    appRun()
}
```

---

## 5. Runtime (wx_runtime.cpp / .h)

Yeni FFI imzaları (`.h`), gruplara göre:

```c
/* ── Menu ─────────────────────────────────────────────────────────── */
int32_t  liva_ui_create_menu_bar(void);
int32_t  liva_ui_create_menu(const char *title);
int32_t  liva_ui_menu_add_item(int32_t menu, const char *label);          /* -> item handle */
int32_t  liva_ui_menu_add_check_item(int32_t menu, const char *label);    /* -> item handle */
void     liva_ui_menu_add_separator(int32_t menu);
void     liva_ui_menu_add_submenu(int32_t menu, const char *label, int32_t sub);
void     liva_ui_menu_bar_add_menu(int32_t bar, int32_t menu);            /* menü başlığı menu'den */
void     liva_ui_window_set_menu_bar(int32_t window, int32_t bar);
void     liva_ui_menu_item_set_enabled(int32_t item, int32_t enabled);
void     liva_ui_menu_item_set_checked(int32_t item, int32_t checked);
void     liva_ui_menu_item_on_click(int32_t item, void *func, void *env, int32_t size);
void     liva_ui_menu_popup(int32_t menu, int32_t target);               /* context menu */

/* ── Context menu (right-click on any widget) ─────────────────────── */
void     liva_ui_on_right_click(int32_t handle, void *func, void *env, int32_t size);

/* ── StatusBar ────────────────────────────────────────────────────── */
int32_t  liva_ui_create_status_bar(int32_t window, int32_t field_count);
void     liva_ui_status_bar_set_text(int32_t sb, int32_t field, const char *text);

/* ── Toolbar ──────────────────────────────────────────────────────── */
int32_t  liva_ui_create_toolbar(int32_t window);
int32_t  liva_ui_toolbar_add_tool(int32_t tb, const char *label);        /* -> tool handle */
void     liva_ui_toolbar_add_separator(int32_t tb);
void     liva_ui_toolbar_realize(int32_t tb);
void     liva_ui_tool_item_set_enabled(int32_t tool, int32_t enabled);
void     liva_ui_tool_item_on_click(int32_t tool, void *func, void *env, int32_t size);
```

### Implementasyon notları
- **Menü item ID'leri:** Her `menu_add_item`/`add_check_item` benzersiz bir wx ID (`wxNewId()` veya artan sayaç) alır; item handle tablosunda `{wxMenuItem*, wxId, ownerFrame}` saklanır. `menu_item_on_click`, item'ın sahibi Frame'e `frame->Bind(wxEVT_MENU, handler, wxId)` ile bağlar. MenuBar bir Frame'e set edildiğinde item'lar o Frame'i sahip kabul eder; popup menüler `menu_popup`'taki target'ın Frame'ini kullanır.
- **Heap-env:** `menu_item_on_click` ve `tool_item_on_click`, Faz 1'in `ownEnv(handle, env, size)` yardımcısını çağırır; env'ler item handle'ına bağlı `g_widgetEnvs`'e kaydedilir ve pencere kapanışında (`freeWidgetEnvs` zinciri) serbest bırakılır.
- **onRightClick:** `wxEVT_CONTEXT_MENU` bind eder; event'ten ekran koordinatını alıp widget-içine çevirir, callback'i `func(env, handle, x, y)` imzasıyla çağırır (yeni `LivaXYFn` tipi, `onKey`'in `LivaKeyFn`'ine paralel).
- **StatusBar:** `wxFrame::CreateStatusBar(field_count)`; handle tablosuna `wxStatusBar*` döner.
- **Toolbar:** `wxFrame::CreateToolBar()`; tool'lar `AddTool(id, label, bitmap)` — bitmap'siz/etiketli başlanır (ikon Faz 2 kapsamı dışında; etiket yeterli). `Realize()` ayrı çağrılır.
- **Sahiplik/yıkım:** Menü/toolbar/statusbar nesneleri wxWidgets'in Frame parent-child sahipliğine bağlıdır; Frame kapanınca yıkılır. Handle tablosu girişleri ve env'ler bu sırada temizlenir.

---

## 6. Derleyici

- **ModuleLoader.cpp:** yeni builtin fonksiyon adları `std::ui` listesine eklenir: `createMenuBar, createMenu, menuAddItem, menuAddCheckItem, menuAddSeparator, menuAddSubmenu, menuBarAddMenu, windowSetMenuBar, menuItemSetEnabled, menuItemSetChecked, menuItemOnClick, menuPopup, onRightClick, createStatusBar, statusBarSetText, createToolbar, toolbarAddTool, toolbarAddSeparator, toolbarRealize, toolItemSetEnabled, toolItemOnClick`.
- **IRGen.cpp:** her yeni FFI için extern kaydı (uygun imza tipiyle). `menu_item_on_click` / `tool_item_on_click` / `on_right_click` callback tipi: `{i32, ptr, ptr, i32}`.
- **IRGenCall.cpp:** her yeni FFI için intrinsic eşleme (Faz 1 deseni). Ayrıca:
  - **Genelleştirilmiş fast-path:** Faz 1'in `isControlDescendant`-tabanlı UI event fast-path'i şu kurala genişler — alıcı sınıfının ilk veri alanı (struct-index 1, vtable sonrası) `handle: i32` ise ve metot adı tanınan bir event metoduysa, inline closure literal'i heap-env yoluna girer. Event-metot adı + alıcı sınıf tipi → FFI seçimi:
    - alıcı `Control`-türevi + `onClick/onChange/onSelect/onKey` → mevcut `liva_ui_on_*`
    - alıcı `Control`-türevi + `onRightClick` → `liva_ui_on_right_click`
    - alıcı `MenuItem` + `onClick` → `liva_ui_menu_item_on_click`
    - alıcı `ToolItem` + `onClick` → `liva_ui_tool_item_on_click`
  - `onRightClick` callback'i 2 parametreli (`(i32,i32)`); FFI yine `(handle, func, env, size)` — koordinatlar runtime'da callback'e geçirilir.

---

## 7. Test & Örnekler

- **Doğrulama (wx-yok):** Faz 1'deki gibi `livac --emit-ir` (link yok) + IR inceleme. wxWidgets kurulu olmadığından menü programları linklenemez; IR-emit doğrulanır.
- **`UICodegenExecTest.cpp`'ye eklenecekler:**
  - Menü programı emit-ir temiz derlenir (MenuBar/Menu/MenuItem, addItem kısa yolu, submenu, separator, checkable).
  - `MenuItem.onClick(|..|{..})` inline literal → `liva_ui_menu_item_on_click` çağrısında **non-zero env size** (genelleştirilmiş fast-path).
  - `widget.onRightClick(|x,y|{..})` emit-ir temiz; context menu `popup` derlenir.
  - StatusBar ve Toolbar programları emit-ir temiz.
- **Örnek:** `examples/ui_menu_demo.liva` — MenuBar + context menu + StatusBar + Toolbar olan tam uygulama iskeleti (§4 örneğinin genişletilmişi).
- **Regresyon:** mevcut suite (2292 test; 2 önceden-var-olan btree hatası hariç) yeşil kalır.

---

## 8. Kapsam Dışı (Non-Goals)

- **Toolbar ikonları/bitmap'leri** (yalnız etiketli tool'lar; ikon yönetimi sonraki dilim).
- **Yeni widget'lar** (TreeView, DataGrid, SpinCtrl, DatePicker, Splitter) — ayrı spec.
- **Layout** (Align/Anchors) — ayrı spec.
- **Accelerator/kısayol tuşları** (Ctrl+S vb. menü hızlandırıcıları) — sonraki dilim; bu spec'te düz etiketli item'lar.
- **Non-literal menü callback heap-own** — Faz 1'deki gibi yalnız inline literal heap-env alır; değişkene atanmış closure stack-env kullanır (belgelenmiş sınır).

---

## 9. Riskler ve Karşı Önlemler

| Risk | Önlem |
|------|-------|
| Menü item event'i Frame'e bind — item'ın sahibi Frame belirsiz olabilir | MenuBar bir Window'a set edilince item'lar o Frame'i sahip alır; popup menüler target'ın Frame'ini kullanır. Sıralama: önce setMenuBar, sonra onClick bağlama (veya runtime geç-bağlama) — runtime notunda netleştir. |
| Genelleştirilmiş fast-path mevcut Control event'lerini bozabilir | Kural yalnız "handle idx1 + tanınan event metodu" ekler; Control yolu değişmez, MenuItem/ToolItem eklenir. Faz 1 callback testleri regresyon koruması. |
| onRightClick koordinat dönüşümü (ekran↔widget) | Runtime'da `ScreenToClient` ile çevir; testte yalnız IR/imza doğrulanır (gerçek koordinat wx-kurulu makinede). |
| wxWidgets yok → menü gerçekten çalıştırılamaz | IR-emit doğrulama; tam GUI testi kullanıcının wx-kurulu makinesine bırakılır (Faz 1 ile aynı kabul). |
