# Faz 6 — Koleksiyon Bağlama

**Tarih:** 2026-06-02
**Durum:** Tasarım onaylandı, implementasyon planı bekliyor
**Branch:** `feat/ui-vcl-phase6-collection-binding` (master üzerinde — Faz 1-5 merge edilmiş)
**Kapsam:** Faz 5 gözlemlenebilir `Model`'i bir **liste değeri** ile genişletmek; bir string listesini ListBox / Dropdown / ComboBox öğe listelerine tek-yönlü bağlamak.

---

## 1. Amaç ve Bağlam

Faz 5 skaler (text/int) iki-yönlü veri bağlamayı getirdi. Faz 6, VCL'in liste-aware kontrol davranışını ekler: bir model listesi değişince bağlı liste-widget'ları otomatik güncellenir. Üç liste-widget'ı (ListBox, Dropdown=wxChoice, ComboBox) öğe listelerini `wxControlWithItems` ortak tabanından paylaşır.

Liste = gösterilen veri; tek yön (model→widget). **Seçim** (hangi öğe seçili) ayrı bir skalerdir ve mevcut Faz 5 `bindInt` ile iki-yönlü bağlanır (Faz 5'te get/set_value Choice/ListBox/ComboBox seçimini kapsayacak şekilde genişletildi). `bindList` (öğeler) + `bindInt` (seçili indeks) birlikte kullanılır.

### Faz 1-5'ten devralınan altyapı
- **Gözlemlenebilir `Model`** (Faz 5): `g_models`, `LivaModel{textVals,intVals,bindings}`, `LivaBinding{widget,kind}`, `g_modelUpdating`, `propagateText`/`propagateInt`, 7 model FFI'si + `Model` Liva sınıfı.
- **Liste-widget FFI'leri (kısmi):** `liva_ui_list_add_item`/`list_clear` (yalnız ListBox); Faz 6 üçünü birden `wxControlWithItems` ile kapsar.
- **3 katman + extern tip yeniden kullanımı:** Phase 2 tipleri `uiI32StrI32VoidTy`/`uiI32StrVoidTy`/`uiI32StrRetI32Ty` ve Faz 5 `uiI32StrStrVoidTy` kapsamda.

### Teknik doğrulama (yapıldı)
- `wxListBox`, `wxChoice`, `wxComboBox` hepsi `wxControlWithItems`'tan türer → `Append(const wxString&)` ve `Clear()` polimorfik. `dynamic_cast<wxControlWithItems*>(wxWindow*)` cross-cast RTTI ile çalışır. Header'lar (`wx/listbox.h`, `wx/choice.h`, `wx/combobox.h`, dolaylı `wx/ctrlsub.h`) zaten dahil.
- Faz 5 extern tipleri: `uiI32StrI32VoidTy` ((i32,ptr,i32)→void), `uiI32StrVoidTy` ((i32,ptr)→void), `uiI32StrRetI32Ty` ((i32,ptr)→i32), `uiI32StrStrVoidTy` ((i32,ptr,ptr)→void) — hepsi `createRuntimeDecls` gövdesinde kapsamda.

---

## 2. Alınan Tasarım Kararları

| Karar | Seçim |
|-------|-------|
| Faz 6 dilimi | Koleksiyon bağlama (DataGrid 2B ayrı dilim) |
| Widget'lar | **ListBox + Dropdown + ComboBox** (öğe listesi; ortak `wxControlWithItems`) |
| Liste API | **Artımsal**: `listAdd`/`listClear` (+ `bindList`, `listCount`) — dizi `setList([string])` DEĞİL |
| Yön | **Tek yön** (model→widget); seçim ayrı `bindInt` (Faz 5) ile iki-yönlü |
| Depo | `LivaModel.listVals` (anahtar→`vector<string>`); bağlama `kind=2` |
| Branch | Yeni `feat/ui-vcl-phase6-collection-binding` (master üzerinde) |

---

## 3. Mimari

Faz 5 Model genişletilir; yeni mimari yok.

```
Kullanıcı kodu ── model.bindList("dosyalar", lb) / model.listAdd("dosyalar", "x")
    ▼
Model.bindList / listAdd / listClear / listCount → model* intrinsic'leri
    ▼
liva_ui_model_bind_list / list_add / list_clear / list_count (wx_runtime.cpp)
    ▼  (listVals deposu + kind==2 bağlı widget'lara widgetAppendItem/widgetClearItems)
wxControlWithItems::Append / Clear (ListBox/Choice/ComboBox)
```

### Çalışma zamanı durumu (Faz 5 LivaModel genişletme)

```cpp
struct LivaModel {
    std::unordered_map<std::string, std::string>              textVals;
    std::unordered_map<std::string, int32_t>                  intVals;
    std::unordered_map<std::string, std::vector<std::string>> listVals;   // YENİ
    std::unordered_map<std::string, std::vector<LivaBinding>> bindings;
};
// LivaBinding.kind: 0=text, 1=int, 2=list (YENİ)
```

### Ortak doldurma yardımcıları

```cpp
static void widgetAppendItem(wxWindow *w, const wxString &s) {
    if (auto *ci = dynamic_cast<wxControlWithItems *>(w)) ci->Append(s);
}
static void widgetClearItems(wxWindow *w) {
    if (auto *ci = dynamic_cast<wxControlWithItems *>(w)) ci->Clear();
}
```

### Akış (tek yön: model→widget)
- `bind_list(key, w)`: `bindings[key].push_back({w,2})`; `widgetClearItems(w)` + `listVals[key]`'in her öğesini `widgetAppendItem(w, ...)` (ilk doldurma).
- `list_add(key, item)`: `listVals[key].push_back(item)`; o anahtardaki her `kind==2` bağlamaya artımsal `widgetAppendItem` (tam yeniden-doldurma yok).
- `list_clear(key)`: `listVals[key].clear()`; her `kind==2` bağlamaya `widgetClearItems`.
- `list_count(key) -> i32`: `listVals[key].size()` (yoksa 0).

> Geri-besleme guard'ı (`g_modelUpdating`) liste işlemlerinde GEREKMEZ: bağlama tek yönlüdür (model→widget); `Append`/`Clear` model'i geri tetiklemez. (Skaler text/int yolları Faz 5'teki guard'ı kullanmaya devam eder.)

---

## 4. API (widgets.liva)

Faz 5 `Model` sınıfına eklenir (mevcut text/int metotlarının yanına):

```liva
pub func bindList(key: string, w: Control) { modelBindList(self.handle, key, w.handle) }
pub func listAdd(key: string, item: string) { modelListAdd(self.handle, key, item) }
pub func listClear(key: string) { modelListClear(self.handle, key) }
pub func listCount(key: string) -> i32 { return modelListCount(self.handle, key) }
```

> Adlar mevcut `ListBox.clear`→`listClear` / `ListBox.addItem`→`listAddItem` builtin'leriyle ÇAKIŞMAZ (model-önekli builtin'ler: `modelListClear`/`modelListAdd`/...).

### Kullanım örneği
```liva
import ui::widgets

func main() {
    appInit()
    let win = Window(460, 320, "Koleksiyon Baglama")
    let root = Panel(win)

    let model = Model()
    let lb = ListBox(root)
    lb.setBounds(20, 20, 200, 200)
    model.bindList("dosyalar", lb)         // tek yön: liste → ListBox

    let dd = Dropdown(root, "")
    dd.setBounds(240, 20, 180, 28)
    model.bindList("dosyalar", dd)         // aynı liste → Dropdown

    model.listAdd("dosyalar", "main.liva")
    model.listAdd("dosyalar", "ui.liva")   // her iki widget güncellenir

    model.bindInt("secili", lb)            // seçili indeks (iki yön, Faz 5)
    lb.onSelect(|_h: i32| { println(model.getInt("secili")) })

    let add = Button(root, "Ekle")
    add.setBounds(240, 70, 180, 28)
    add.onClick(|_h: i32| { model.listAdd("dosyalar", "yeni.liva") })

    win.show()
    appRun()
}
```

---

## 5. Runtime (wx_runtime.cpp / .h)

```c
/* ── Phase 6: collection binding ────────────────────────────────────── */
void     liva_ui_model_bind_list(int32_t model, const char *key, int32_t widget);
void     liva_ui_model_list_add(int32_t model, const char *key, const char *item);
void     liva_ui_model_list_clear(int32_t model, const char *key);
int32_t  liva_ui_model_list_count(int32_t model, const char *key);
```

### Implementasyon notları
- `widgetAppendItem`/`widgetClearItems` yardımcıları (yukarıda) — `wxControlWithItems` cross-cast; ListBox/Dropdown/ComboBox kapsanır, başka widget'ta no-op.
- **`bind_list`:** `g_models.find` + `getHandle<wxWindow>(widget)` null-kontrol; `{widget,2}` kaydı; `widgetClearItems(w)` sonra `listVals[key]`'in her öğesini `widgetAppendItem`.
- **`list_add`:** `listVals[key].push_back(item)`; `bindings[key]`'deki `kind==2` && `getHandle` non-null widget'lara `widgetAppendItem`.
- **`list_clear`:** `listVals[key].clear()`; `kind==2` widget'lara `widgetClearItems`.
- **`list_count`:** `listVals[key].size()` (`int32_t`'e cast; yoksa 0).
- **Yok edilmiş widget:** `dynamic_cast<wxControlWithItems*>(getHandle(...))` null → no-op (güvenli; bağlama sızar ama zararsız — Faz 5 deseniyle tutarlı).
- Faz 5 `LivaModel`'ine `listVals` alanı eklenir; `LivaBinding.kind` yorumuna `2=list` eklenir (struct değişmez, kind zaten int).

---

## 6. Derleyici

- **ModuleLoader.cpp:** `std::ui` builtin listesine: `modelBindList, modelListAdd, modelListClear, modelListCount`.
- **IRGen.cpp:** extern'ler (Phase 5 bloğundan sonra), hepsi mevcut tipleri yeniden kullanır (yeni yerel tip YOK):
  - `model_bind_list` `(i32,ptr,i32)→void` → `uiI32StrI32VoidTy`.
  - `model_list_add` `(i32,ptr,ptr)→void` → `uiI32StrStrVoidTy` (Faz 5).
  - `model_list_clear` `(i32,ptr)→void` → `uiI32StrVoidTy` (Phase 2).
  - `model_list_count` `(i32,ptr)→i32` → `uiI32StrRetI32Ty` (Phase 2).
- **IRGenCall.cpp:** Phase 6 bloğu (Faz 5 `modelBindInt`'ten sonra) — 4 intrinsic; `modelListCount` çağrı sonucunu (i32) döner, diğer üçü void (`getNullValue(i32)`). Fast-path'e DOKUNULMAZ.
- **Model sınıfı:** mevcut; 4 metot eklenir.

---

## 7. Test & Örnekler

- **Doğrulama (wx-yok / headless):** `livac --emit-ir` (link yok) — `UICodegenExecTest.cpp`'ye:
  - `Model()` + `bindList`/`listAdd`/`listClear`/`listCount` temiz derlenir.
  - Aynı anahtar ListBox + Dropdown + ComboBox'a `bindList` → üçü de derlenir.
  - `hasRuntimeCall` ile void FFI'lerin (`model_bind_list`, `model_list_add`, `model_list_clear`) IR'de lower olduğunu assert et.
- **Runtime C++ derlemesi:** bu makinede wx kurulu → `cmake --build` `wx_runtime.cpp`'yi derler (`widgetAppendItem`/`widgetClearItems` + `wxControlWithItems` cast C++ hataları yakalanır).
- **Örnek:** `examples/ui_collection_binding.liva` — §4 kullanım örneği (ListBox + Dropdown aynı listeye bindList; "Ekle"/"Temizle" butonları canlı mutasyon; ListBox seçimi bindInt).
- **GUI doğrulama (manuel, wx-kurulu makinede):** başlangıç öğeleri görünür; "Ekle" → ListBox+Dropdown anında büyür; "Temizle" → ikisi boşalır; seçim çalışır. (headless senkronu test edemez — Faz 3-5 ile aynı kabul.)
- **Regresyon:** mevcut 2313 test yeşil kalır; ~3-4 yeni emit-IR testi.

---

## 8. Kapsam Dışı (Non-Goals)

- **DataGrid 2B/satır-kayıt bağlama** (ayrı büyük dilim).
- Dizi-tabanlı `setList(items: [string])` (Liva `[string]` DynArray marshalling) — artımsal API tercih edildi.
- Liste öğesi silme-indeksten / öğe güncelleme / sıralama / filtreleme.
- İki-yönlü öğe düzenleme (liste öğeleri salt-gösterim).
- Liste öğelerini model'den geri okuma (yalnız `listCount`; `listGet`/`listItems` yok).
- RadioGroup koleksiyon bağlama (wxRadioBox seçenekleri inşa anında sabit; `wxControlWithItems` değil).

---

## 9. Riskler ve Karşı Önlemler

| Risk | Önlem |
|------|-------|
| `dynamic_cast<wxControlWithItems*>` cross-cast çalışmaz | wx RTTI açık; wxListBox/wxChoice/wxComboBox kanıtlı `wxControlWithItems` türevleri. Derlemede + GUI'de doğrulanır. Çalışmazsa tip-bazlı `dynamic_cast<wxListBox*>/<wxChoice*>/<wxComboBox*>` fallback'i (on_select deseni). |
| Ad çakışması (`listClear`/`listAdd`) | Model builtin'leri `model`-önekli (`modelListClear` vb.); mevcut ListBox builtin'leriyle ayrı. |
| Yok edilmiş widget'a doldurma | `getHandle`→`dynamic_cast` null → no-op; güvenli. |
| Liste sızıntısı (bindings/listVals kalır) | Model uygulama ömrü; Faz 5 deseniyle tutarlı, kapsam dışı temizlik. |
| Seçim ile karışma | Koleksiyon = öğeler (kind 2, tek yön); seçim = skaler indeks (kind 1, bindInt). Ayrı kind'ler, çapraz-tetikleme yok. |
| wx-yok ortamda gerçek davranış | IR-emit (+ `hasRuntimeCall`) + wx-kurulu makinede `.exe` + GUI teyidi (Faz 3-5 ile aynı kabul). |
