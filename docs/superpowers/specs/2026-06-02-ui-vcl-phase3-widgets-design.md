# Faz 3 — Yeni Widget'lar

**Tarih:** 2026-06-02
**Durum:** Tasarım onaylandı, implementasyon planı bekliyor
**Branch:** `feat/ui-vcl-phase3` (master üzerinde — Faz 1+2 merge edilmiş)
**Kapsam:** wxWidgets tabanlı `ui::widgets` kütüphanesine altı yeni VCL-tarzı widget eklemek: SpinCtrl, DatePicker, ComboBox, TreeView, DataGrid, Splitter.

---

## 1. Amaç ve Bağlam

Faz 1 sınıf-tabanlı widget temelini (Control hiyerarşisi + inline-closure heap-env callback'ler), Faz 2 uygulama çerçevesini (menü/toolbar/statusbar/context-menu) kurdu. Faz 3, VCL'in en çok eksik kalan widget'larını ekler — sayısal/tarih girişi, ağaç, tablo, bölünmüş pencere.

Bu dilim yeni mimari getirmez; Faz 1'in kanıtlanmış desenini (Control alt sınıfı + create-FFI + i32 handle) tekrarlar. Risk düşük, değer yüksek.

### Faz 1/2'den devralınan altyapı
- **Control hiyerarşisi + Swift-init inşa:** `SpinCtrl(parent, ...)`.
- **Genelleştirilmiş callback fast-path:** inline closure literal `widget.onChange(|..|{..})` heap-env alır; yeni widget'lar Control alt sınıfı olduğundan otomatik kapsanır (fast-path'e dokunulmaz).
- **Handle deseni:** wx olmayan alt-nesneler (menü item'ları gibi) i32 handle ile yan tabloda tutulur — TreeView node'ları aynı deseni kullanır.
- **wx link altyapısı:** `liva_ui.lib` + `wx_libs.cfg` (Faz 2 sonu düzeltildi); `ui::` import'u UI runtime'ı linkler.

### Teknik doğrulama (yapıldı)
- Tüm header'lar vcpkg'de mevcut: `wx/spinctrl.h`, `wx/datectrl.h`, `wx/combobox.h`, `wx/treectrl.h`, `wx/grid.h`, `wx/splitter.h`.
- wxGrid için ayrı `wxmsw*_grid` lib'i YOK (wx 3.3'te grid `_core`'a dahil). wx_libs.cfg `_core`+`_adv` linkliyor → link riski düşük; implementasyonda gerçek `.exe` linkiyle doğrulanacak (gerekirse cfg'ye eklenir).

---

## 2. Alınan Tasarım Kararları

| Karar | Seçim |
|-------|-------|
| Faz 3 kapsamı | Yalnız "yeni widget'lar" dilimi (layout/animation/dnd ayrı spec'ler) |
| Widget'lar | SpinCtrl, DatePicker, ComboBox, TreeView, DataGrid, Splitter (altısı) |
| Karmaşık-widget alt-erişimi | Node/hücre = **i32 handle / koordinat** (Faz 1-2 handle deseniyle tutarlı), TreeNode nesnesi değil |
| Splitter inşası | `splitVertically(left, right)` / `splitHorizontally(top, bottom)` — iki ayrı Control'ü split eder |
| Fast-path | Değişmez; yeni widget'lar Control alt sınıfı → mevcut genelleştirilmiş fast-path kapsar |
| Branch | Yeni `feat/ui-vcl-phase3` (master üzerinde) |

---

## 3. Mimari

Faz 1/2'nin 3 katmanı korunur:
```
Kullanıcı kodu ── import ui::widgets
    ▼
ui::widgets (yeni: SpinCtrl, DatePicker, ComboBox, TreeView, DataGrid, Splitter)
    ▼
liva_ui_spin_* / date_* / combo_* / tree_* / grid_* / splitter_* intrinsic'leri
    ▼
wx_runtime.cpp (wxSpinCtrl / wxDatePickerCtrl / wxComboBox / wxTreeCtrl / wxGrid / wxSplitterWindow)
```

İki karmaşıklık seviyesi:
- **Basit (handle + value):** SpinCtrl, DatePicker, ComboBox — Faz 1 widget'larıyla birebir.
- **Karmaşık:** TreeView (node = i32 handle, yan tablo), DataGrid (hücre = row/col), Splitter (iki Control'ü içerir).

Tüm widget'lar `Control` alt sınıfıdır; `setEnabled/setVisible/setBounds/setSize/...` ve event metotları (`onClick/onChange/onSelect/...`) Control'den miras alınır.

---

## 4. Sınıf API'si (widgets.liva'ya eklenir)

```liva
// ── SpinCtrl (wxSpinCtrl) ──────────────────────────────────────
pub class SpinCtrl: Control {
    init(parent: Control, minVal: i32, maxVal: i32, val: i32) {
        super.init(createSpinCtrl(parent.handle, minVal, maxVal, val))
    }
    pub func getValue() -> i32 { return getValue(self.handle) }
    pub func setValue(v: i32) { setValue(self.handle, v) }
}

// ── DatePicker (wxDatePickerCtrl) ──────────────────────────────
pub class DatePicker: Control {
    init(parent: Control) { super.init(createDatePicker(parent.handle)) }
    pub func getDate() -> string { return dateGetValue(self.handle) }   // "YYYY-MM-DD"
}

// ── ComboBox (wxComboBox — editable dropdown) ──────────────────
pub class ComboBox: Control {
    init(parent: Control, value: string) { super.init(createComboBox(parent.handle, value)) }
    pub func addItem(item: string) { comboAddItem(self.handle, item) }
    pub func getText() -> string { return getText(self.handle) }
    pub func getSelection() -> i32 { return getValue(self.handle) }
}

// ── TreeView (wxTreeCtrl — node = i32 handle) ──────────────────
pub class TreeView: Control {
    init(parent: Control) { super.init(createTreeView(parent.handle)) }
    pub func addRoot(label: string) -> i32 { return treeAddRoot(self.handle, label) }
    pub func addNode(parentNode: i32, label: string) -> i32 {
        return treeAddNode(self.handle, parentNode, label)
    }
    pub func getSelection() -> i32 { return treeGetSelection(self.handle) }  // selected node handle
}

// ── DataGrid (wxGrid — hücre = row/col) ────────────────────────
pub class DataGrid: Control {
    init(parent: Control, rows: i32, cols: i32) {
        super.init(createDataGrid(parent.handle, rows, cols))
    }
    pub func setCell(row: i32, col: i32, text: string) { gridSetCell(self.handle, row, col, text) }
    pub func getCell(row: i32, col: i32) -> string { return gridGetCell(self.handle, row, col) }
    pub func setColLabel(col: i32, text: string) { gridSetColLabel(self.handle, col, text) }
}

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

> Event metotları (`onChange`/`onSelect`) Control'den miras alınır — ayrı tanım gerekmez. SpinCtrl/ComboBox `onChange`, TreeView/ComboBox `onSelect`, DataGrid `onChange` kullanır; runtime tarafında ilgili wx event'leri mevcut bind fonksiyonlarına eklenir (§5).

### Kullanım örneği
```liva
import ui::widgets
func main() {
    appInit()
    let win = Window(700, 500, "Gelişmiş Widget'lar")

    let sp = Splitter(win)
    let tree = TreeView(sp)
    let root = tree.addRoot("Proje")
    let src = tree.addNode(root, "src")
    tree.addNode(src, "main.liva")
    tree.onSelect(|_h: i32| { /* seçilen node: tree.getSelection() */ })

    let grid = DataGrid(sp, 5, 3)
    grid.setColLabel(0, "Ad")
    grid.setCell(0, 0, "Ali")

    sp.splitVertically(tree, grid)
    sp.setSashPosition(220)

    win.show()
    appRun()
}
```

---

## 5. Runtime (wx_runtime.cpp / .h)

Yeni FFI imzaları (`.h`), gruplara göre:

```c
/* ── SpinCtrl ───────────────────────────────────────────────────────── */
int32_t  liva_ui_create_spin_ctrl(int32_t parent, int32_t minVal, int32_t maxVal, int32_t val);

/* ── DatePicker ─────────────────────────────────────────────────────── */
int32_t      liva_ui_create_date_picker(int32_t parent);
const char  *liva_ui_date_get_value(int32_t handle);     /* "YYYY-MM-DD" */

/* ── ComboBox ───────────────────────────────────────────────────────── */
int32_t  liva_ui_create_combo_box(int32_t parent, const char *value);
void     liva_ui_combo_add_item(int32_t handle, const char *item);

/* ── TreeView (nodes tracked as i32 handles) ────────────────────────── */
int32_t  liva_ui_create_tree_view(int32_t parent);
int32_t  liva_ui_tree_add_root(int32_t handle, const char *label);            /* -> node handle */
int32_t  liva_ui_tree_add_node(int32_t handle, int32_t parentNode, const char *label);
int32_t  liva_ui_tree_get_selection(int32_t handle);                          /* -> node handle (0 if none) */

/* ── DataGrid ───────────────────────────────────────────────────────── */
int32_t      liva_ui_create_data_grid(int32_t parent, int32_t rows, int32_t cols);
void         liva_ui_grid_set_cell(int32_t handle, int32_t row, int32_t col, const char *text);
const char  *liva_ui_grid_get_cell(int32_t handle, int32_t row, int32_t col);
void         liva_ui_grid_set_col_label(int32_t handle, int32_t col, const char *text);

/* ── Splitter ───────────────────────────────────────────────────────── */
int32_t  liva_ui_create_splitter(int32_t parent);
void     liva_ui_splitter_split_v(int32_t handle, int32_t left, int32_t right);
void     liva_ui_splitter_split_h(int32_t handle, int32_t top, int32_t bottom);
void     liva_ui_splitter_set_sash(int32_t handle, int32_t px);
```

### Implementasyon notları
- **Include'lar:** `<wx/spinctrl.h>`, `<wx/datectrl.h>`, `<wx/combobox.h>`, `<wx/treectrl.h>`, `<wx/grid.h>`, `<wx/splitter.h>`.
- **SpinCtrl:** `new wxSpinCtrl(parent, wxID_ANY, "", pos, size, style, min, max, val)`. Değer `GetValue/SetValue` mevcut `liva_ui_get_value/set_value`'ye eklenir (wxSpinCtrl `dynamic_cast`).
- **DatePicker:** `new wxDatePickerCtrl(...)`. `date_get_value` `GetValue()` (wxDateTime) → `FormatISODate()` → `static thread_local std::string` (mevcut string-döndüren FFI deseni gibi). `wxEVT_DATE_CHANGED` `liva_ui_on_change`'e eklenir.
- **ComboBox:** `new wxComboBox(...)`. `combo_add_item` → `Append`. `get_text` `GetValue()`, `get_value` `GetSelection()`. `wxEVT_COMBOBOX` (`on_select`) + `wxEVT_TEXT` (`on_change`) bind'lere eklenir.
- **TreeView:** `new wxTreeCtrl(...)`. Node'lar `wxTreeItemId` olarak yan tabloda: `static std::unordered_map<int32_t, wxTreeItemId> g_treeNodes;` + artan handle (Faz 2 menü item deseni). `tree_add_root` → `AddRoot`, `tree_add_node` → `AppendItem(g_treeNodes[parentNode], label)`. `tree_get_selection` → `GetSelection()` → o item'a karşılık gelen handle (yoksa 0). `wxEVT_TREE_SEL_CHANGED` `liva_ui_on_select`'e eklenir.
- **DataGrid:** `new wxGrid(...)` + `CreateGrid(rows, cols)`. `set_cell` → `SetCellValue(row, col, text)`, `get_cell` → `GetCellValue` → thread_local string, `set_col_label` → `SetColLabel`. `wxEVT_GRID_CELL_CHANGED` `liva_ui_on_change`'e eklenir.
- **Splitter:** `new wxSplitterWindow(...)`. `split_v` → `SplitVertically(getHandle<wxWindow>(left), getHandle<wxWindow>(right))`, `split_h` → `SplitHorizontally(...)`, `set_sash` → `SetSashPosition(px)`. (Faz 1 kuralı: bölünecek panellerin parent'ı splitter olmalı — örnek/dokümanda belirtilir; wx bunu split anında kabul eder.)
- **Event entegrasyonu:** mevcut `liva_ui_on_change`/`on_select`/`on_click`'in `dynamic_cast` zincirine yeni tipler eklenir; böylece Faz 1 fast-path (`widget.onChange(|..|{..})`) yeni widget'larda da heap-env ile çalışır.
- **string-döndüren FFI ömrü:** `date_get_value`/`grid_get_cell`, mevcut `get_text` gibi `static thread_local std::string` döndürür (çağrı arası geçerli; Liva tarafı hemen kopyalar).

---

## 6. Derleyici

- **ModuleLoader.cpp:** `std::ui` builtin listesine yeni adlar: `createSpinCtrl, createDatePicker, dateGetValue, createComboBox, comboAddItem, createTreeView, treeAddRoot, treeAddNode, treeGetSelection, createDataGrid, gridSetCell, gridGetCell, gridSetColLabel, createSplitter, splitterSplitV, splitterSplitH, splitterSetSash`.
- **IRGen.cpp:** her yeni FFI için extern kaydı. İmza tipleri: çoğu `(i32...)→i32`/`→void`; `dateGetValue` `(i32)→ptr`; `gridGetCell` `(i32,i32,i32)→ptr`; string-alan `comboAddItem`/`gridSetCell`/`gridSetColLabel` `i8Ptr` argümanlı.
- **IRGenCall.cpp:** her FFI için intrinsic eşleme (Faz 1-2 deseni; setBounds bloğunun yakınına). **Fast-path'e DOKUNULMAZ** — yeni widget'lar Control alt sınıfı olduğundan mevcut genelleştirilmiş fast-path onları otomatik kapsar.

---

## 7. Test & Örnekler

- **Doğrulama (wx-yok ortamda):** `livac --emit-ir` (link yok) + IR inceleme. `UICodegenExecTest.cpp`'ye eklenecekler:
  - Her widget create + temel metotları derlenir (SpinCtrl get/set, DatePicker getDate, ComboBox addItem/getText, TreeView addRoot/addNode/getSelection, DataGrid setCell/getCell/setColLabel, Splitter splitV/H/setSash).
  - TreeView node-handle akışı (`addRoot` → `addNode(root, ...)`) temiz derlenir.
  - Bir widget'ta inline `onChange(|..|{..})` heap-env (fast-path Control alt sınıfını tanır) → non-zero env size.
- **Örnek:** `examples/ui_widgets_advanced.liva` — altı widget tek pencerede (TreeView + DataGrid bir Splitter içinde; SpinCtrl/DatePicker/ComboBox form panelinde).
- **Regresyon:** mevcut suite (2298 test) yeşil kalır.
- **wx-kurulu doğrulama:** Faz 2'deki gibi gerçek `.exe` linklenir + GUI'de açıldığı teyit edilir. wxGrid'in `_core`/`_adv` lib'lerinde olduğu burada doğrulanır; değilse `wx_libs.cfg` üretimine `_grid` glob'u eklenir.

---

## 8. Kapsam Dışı (Non-Goals)

- **TreeView:** node silme/yeniden adlandırma, ikon, sürükle-bırak, çoklu seçim, lazy-load (bu spec'te ekleme + seçim).
- **DataGrid:** satır/sütun ekleme-silme (sabit rows×cols init), hücre editör tipleri, sıralama, biçimlendirme/renk (bu spec'te metin hücre + sütun başlığı).
- **ComboBox:** read-only (non-editable) varyant — bu spec'te editable; read-only zaten Faz 1 `Dropdown` ile karşılanır.
- **DatePicker:** tarih aralığı/min-max, saat (yalnız tarih, ISO string).
- **Splitter:** 3+ panel, unsplit/yeniden-split runtime'da, kayar oran sabitleme.
- **Diğer biriken Faz 3 adayları** (Form DSL/designer, data binding, animation→timer, drag&drop, Align/Anchors layout) — ayrı spec'ler.

---

## 9. Riskler ve Karşı Önlemler

| Risk | Önlem |
|------|-------|
| wxGrid ayrı lib gerektirir (link hatası) | Önce `_core`/`_adv` ile dene; `.exe` link doğrulamasında çıkarsa `wx_libs.cfg` üretimine `wxmsw*_grid` glob'u ekle. |
| TreeView node handle ↔ wxTreeItemId eşlemesi | Faz 2 menü-item deseni (yan tablo + artan handle); `get_selection` ters-arama yerine seçili item'ı tabloda bul (gerekirse `wxTreeItemId → handle` ikinci map). |
| Splitter parent kuralı (panellerin parent'ı splitter olmalı) | Örnek ve dokümanda belirt; runtime split anında wx'in kabul ettiği şekilde `getHandle<wxWindow>` ile geçir. wx yanlış parent'ta uyarır — testte yalnız IR/derleme doğrulanır. |
| DatePicker/Grid string ömrü | Mevcut `get_text` deseni (thread_local string) — kanıtlanmış. |
| wx-yok ortamda gerçek davranış test edilemez | IR-emit doğrulama + sonda wx-kurulu makinede `.exe` + GUI teyidi (Faz 2 ile aynı kabul). |
