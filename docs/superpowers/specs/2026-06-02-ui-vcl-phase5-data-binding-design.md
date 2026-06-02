# Faz 5 — Data Binding (İki Yönlü)

**Tarih:** 2026-06-02
**Durum:** Tasarım onaylandı, implementasyon planı bekliyor
**Branch:** `feat/ui-vcl-phase5-data-binding` (master üzerinde — Faz 1-4 merge edilmiş)
**Kapsam:** `ui::widgets`'e VCL'in DB-aware kontrol felsefesini getiren hafif iki-yönlü veri bağlama: kütüphane-tarafı gözlemlenebilir `Model` (anahtar-değer, tipli text+int) ↔ widget'lar.

---

## 1. Amaç ve Bağlam

Faz 1-4 sınıf temelini, uygulama çerçevesini, altı yeni widget'ı ve VCL yerleşimini (Align/Anchors) kurdu. Faz 5, Delphi VCL'in en tanımlayıcı ikinci özelliğini ekler: veri-aware kontroller. Bir widget'ı bir veri kaynağına bağlarsın; widget değişince kaynak güncellenir, kaynak değişince widget güncellenir.

Liva'da sade bir sınıfın alanı gözlemlenemez (değişimde haber veren kanca yok). Bu yüzden kütüphane hafif bir **gözlemlenebilir Model** sağlar: anahtar→değer deposu + değişim bildirimi. Model runtime'da (C++) yaşar — widget olay sistemi zaten orada — Liva `Model` sınıfı ince bir handle sarmalayıcıdır.

### Faz 1-4'ten devralınan altyapı
- **3 katman:** Liva sınıfı → `liva_ui_*` intrinsic (IRGenCall.cpp) → `wx_runtime.cpp` (wxWidgets).
- **Handle deseni:** widget i32 handle; `getHandle<wxWindow>(h)`.
- **Yan tablo deseni:** wx-olmayan veri için handle→kayıt map (`g_treeNodes`/`g_layouts`); Model aynı deseni kullanır (`g_models`).
- **Değer FFI'leri (yeniden kullanılır):** `liva_ui_get_text`/`set_text` (returnTempStr ile), `liva_ui_get_value`/`set_value`.
- **Olay dispatch (yeniden kullanılır):** `liva_ui_on_change`/`on_select`'in `dynamic_cast` zinciri — hangi widget'ın hangi "değer değişti" olayını yaydığını bilir.
- **Standalone handle-sarmalayıcı sınıf deseni:** `Menu`/`MenuBar` (Control değil, `var handle: i32` + idx-0).

### Teknik doğrulama (yapıldı)
- `liva_ui_get_text`/`set_text`/`get_value`/`set_value` ve `returnTempStr` mevcut (`wx_runtime.cpp`).
- Değişim olayları: `wxEVT_TEXT` (TextCtrl/ComboBox), `wxEVT_SLIDER`, `wxEVT_SPINCTRL`, `wxEVT_CHECKBOX`, `wxEVT_CHOICE`, `wxEVT_LISTBOX` — Faz 1-3'te kullanıldı, kurulu wx 3.3'te mevcut.
- `Menu` sınıfı standalone handle-sarmalayıcı deseninin kanıtı (`widgets.liva`).

---

## 2. Alınan Tasarım Kararları

| Karar | Seçim |
|-------|-------|
| Faz 5 dilimi | Data binding (drag&drop/form-DSL/animation ayrı spec'ler) |
| Model soyutlama | **Kütüphane gözlemlenebilir Model** (anahtar-değer); sade-sınıf-alanı veya widget↔widget DEĞİL |
| Değer tipi | **Tipli: text + int** (setText/getText/bindText ve setInt/getInt/bindInt) |
| Model konumu | Runtime (C++), `g_models`; Liva `Model` ince handle sarmalayıcı |
| Yön | İki yönlü (editable widget'lar); salt-gösterim Label doğal tek-yön |
| Geri-besleme | `g_modelUpdating` bayrağı (sonsuz döngü/çift-güncelleme önlenir) |
| Branch | Yeni `feat/ui-vcl-phase5-data-binding` (master üzerinde) |

---

## 3. Mimari

Faz 1-4'ün 3 katmanı korunur. Yeni: runtime'da gözlemlenebilir model + bağlama.

```
Kullanıcı kodu ── model.bindText("ad", input) / model.setText("ad", "Ali")
    ▼
Model.bindText / setText → modelBindText / modelSetText intrinsic'leri
    ▼
liva_ui_model_bind_text / liva_ui_model_set_text (wx_runtime.cpp)
    ▼  (g_models deposu + bağlı widget'lara propagasyon, feedback-guard)
widget değer FFI'leri (set_text/set_value) + olay-hook (wxEVT_TEXT/SLIDER/...)
```

### Çalışma zamanı durumu

```cpp
struct LivaBinding { int32_t widget; int kind; };   // kind: 0=text, 1=int
struct LivaModel {
    std::unordered_map<std::string, std::string> textVals;
    std::unordered_map<std::string, int32_t>     intVals;
    std::unordered_map<std::string, std::vector<LivaBinding>> bindings;  // anahtar → widget'lar
};
static std::unordered_map<int32_t, LivaModel> g_models;   // model handle → model
static bool g_modelUpdating = false;   // geri-besleme (feedback-loop) koruması
```

### İki yönlü akış
- **Widget → Model:** `bindText`/`bindInt`, widget'ın değişim olayına işleyici bağlar. Kullanıcı değiştirince (ve `!g_modelUpdating`): `g_modelUpdating=true`; `getText`/`getValue` ile değer okunur; depo güncellenir; **aynı anahtara bağlı diğer** widget'lara (kaynak handle atlanır) `setText`/`setValue` yazılır; `g_modelUpdating=false`.
- **Model → Widget:** `setText`/`setInt`: `g_modelUpdating=true`; depo güncellenir; o anahtara bağlı **tüm** widget'lara yazılır; `g_modelUpdating=false`. (Programatik `setText`'in tetiklediği `wxEVT_TEXT`, bağlama işleyicisinin başındaki bayrak kontrolüyle erken döner.)
- **İlk senkron:** `bind*` çağrıldığında model'in mevcut değeri (yoksa boş "" / 0) hemen widget'a itilir.

---

## 4. API (widgets.liva)

```liva
// ── Model (gözlemlenebilir anahtar-değer; Control değil, handle-sarmalayıcı) ──
pub class Model {
    var handle: i32
    init() { self.handle = modelCreate() }

    pub func setText(key: string, val: string) { modelSetText(self.handle, key, val) }
    pub func getText(key: string) -> string { return modelGetText(self.handle, key) }
    pub func setInt(key: string, val: i32) { modelSetInt(self.handle, key, val) }
    pub func getInt(key: string) -> i32 { return modelGetInt(self.handle, key) }

    pub func bindText(key: string, w: Control) { modelBindText(self.handle, key, w.handle) }
    pub func bindInt(key: string, w: Control) { modelBindInt(self.handle, key, w.handle) }
}
```

> `Model`, `Menu` gibi standalone handle-sarmalayıcıdır (idx-0 handle); sınıf pre-pass otomatik kapsar. `w: Control` parametresi sınıf-tipli-param desteğiyle (`w.handle`) çalışır (Faz 1'de kurulu).

### Kullanım örneği
```liva
import ui::widgets

func main() {
    appInit()
    let win = Window(420, 260, "Veri Baglama")
    let root = Panel(win)

    let model = Model()
    model.setText("ad", "Ali")

    let input = TextInput(root, "")
    input.setBounds(120, 20, 200, 28)
    model.bindText("ad", input)         // iki yönlü: input ↔ model["ad"]

    let echo = Label(root, "")
    echo.setBounds(120, 60, 200, 24)
    model.bindText("ad", echo)          // canlı yansıma (Label → tek yön)

    let reset = Button(root, "Sifirla")
    reset.setBounds(120, 100, 200, 28)
    reset.onClick(|_h: i32| { model.setText("ad", "Varsayilan") })  // model → widget'lar

    win.show()
    appRun()
}
```

---

## 5. Runtime (wx_runtime.cpp / .h)

```c
/* ── Phase 5: data binding ──────────────────────────────────────────── */
int32_t      liva_ui_model_create(void);
void         liva_ui_model_set_text(int32_t model, const char *key, const char *val);
const char  *liva_ui_model_get_text(int32_t model, const char *key);   /* returnTempStr */
void         liva_ui_model_set_int(int32_t model, const char *key, int32_t val);
int32_t      liva_ui_model_get_int(int32_t model, const char *key);
void         liva_ui_model_bind_text(int32_t model, const char *key, int32_t widget);
void         liva_ui_model_bind_int(int32_t model, const char *key, int32_t widget);
```

### Implementasyon notları
- **`<unordered_map>`/`<string>`/`<vector>` zaten dahil** (wx_runtime.cpp başı). Yeni include yok.
- **`liva_ui_model_create`:** `int32_t h = g_nextHandle++` (global benzersiz sayaç; `g_handles`'a EKLENMEZ — `g_treeNodes` deseni); `g_models[h]` oluşturulur; `h` döner.
- **`liva_ui_model_set_text`:** `auto &M = g_models[model]`; `M.textVals[key] = val`; `propagateText(M, key, val, -1)` (kaynak yok → hepsine). `g_modelUpdating` guard ile.
- **`liva_ui_model_get_text`:** `returnTempStr(M.textVals[key])` (yoksa "").
- **`liva_ui_model_set_int` / `get_int`:** `intVals` ile aynısı; propagasyon `setValue`.
- **`propagateText/propagateInt` (yardımcı):** `g_modelUpdating=true`; o anahtardaki her `LivaBinding`'e (kind eşleşen, widget != source) `liva_ui_set_text`/`set_value` çağır; `g_modelUpdating=false`. `getHandle` null ise no-op (yok edilmiş widget güvenli).
- **`liva_ui_model_bind_text`:** `M.bindings[key].push_back({widget, 0})`; mevcut değeri widget'a it (`set_text`); widget değişim olayını bağla — `wxEVT_TEXT` (TextCtrl/ComboBox `dynamic_cast`); handler: `if (g_modelUpdating) return; g_modelUpdating=true; const char* v = get_text(widget); M.textVals[key]=v; propagateText'in "diğerlerine" kısmı (kaynak=widget atlanır); g_modelUpdating=false;`. (Label/StaticText `wxEVT_TEXT` yaymaz → tek yön; bind yine de mevcut değeri iter.)
- **`liva_ui_model_bind_int`:** benzer; olay widget tipine göre `dynamic_cast` (`wxSlider`→`wxEVT_SLIDER`, `wxSpinCtrl`→`wxEVT_SPINCTRL`, `wxCheckBox`→`wxEVT_CHECKBOX`, `wxChoice`→`wxEVT_CHOICE`, `wxListBox`→`wxEVT_LISTBOX`); handler `get_value` okur.
- **Geri-besleme:** tek global `g_modelUpdating` bayrağı; bağlama handler'ları ve propagasyon onu kontrol eder/set eder. wx tek-iş-parçacıklı UI'da yeterli.
- **string ömrü:** `model_get_text`, mevcut `returnTempStr` (thread_local) deseni; Liva tarafı hemen kopyalar.

---

## 6. Derleyici

- **ModuleLoader.cpp:** `std::ui` builtin listesine: `modelCreate, modelSetText, modelGetText, modelSetInt, modelGetInt, modelBindText, modelBindInt`.
- **IRGen.cpp:** extern'ler (Phase 4 bloğundan sonra). Phase 2'de tanımlanan ve aynı fonksiyon gövdesinde kapsamda olan tipler YENİDEN KULLANILIR; yalnız **2 yeni yerel tip** gerekir:
  - `model_create` `()→i32` → `uiRetI32Ty` (Phase 2, kapsamda).
  - `model_set_text` `(i32,ptr,ptr)→void` → **yeni** `uiI32StrStrVoidTy`.
  - `model_get_text` `(i32,ptr)→ptr` → **yeni** `uiI32StrRetStrTy`.
  - `model_set_int` `(i32,ptr,i32)→void` → `uiI32StrI32VoidTy` (Phase 2, kapsamda).
  - `model_get_int` `(i32,ptr)→i32` → `uiI32StrRetI32Ty` (Phase 2, kapsamda).
  - `model_bind_text` / `model_bind_int` `(i32,ptr,i32)→void` → `uiI32StrI32VoidTy` (Phase 2, kapsamda).
- **IRGenCall.cpp:** Phase 5 bloğu (Phase 4 setAnchors'dan sonra) — her FFI için intrinsic eşleme (string-arg'lar i8Ptr; get_text → string döner). Fast-path'e DOKUNULMAZ.
- **Model sınıfı:** `widgets.liva`'da `Menu` gibi; sınıf pre-pass kapsar; yeni dil özelliği yok.

---

## 7. Test & Örnekler

- **Doğrulama (wx-yok / headless):** `livac --emit-ir` (link yok) — `UICodegenExecTest.cpp`'ye:
  - `Model()` + `setText`/`getText` + `setInt`/`getInt` temiz derlenir.
  - `bindText("ad", input)` / `bindInt("yas", spin)` temiz derlenir; çoklu widget tek anahtara bind + Label yansıtma.
  - Her FFI'nin IR'de gerçekten lower olduğunu assert et (Faz 4 `hasRuntimeCall` deseni — boş-geçişi önler; en az `model_create`, `model_bind_text`, `model_set_text` için).
- **Runtime C++ derlemesi:** bu makinede wx kurulu → `cmake --build` `wx_runtime.cpp`'yi derler (model deposu + bağlama + olay-hook C++ hataları yakalanır).
- **Örnek:** `examples/ui_data_binding.liva` — §4 kullanım örneği genişletilmiş (input↔model, label yansıma, SpinCtrl bindInt + int-gösterim Label, reset butonu model→widget).
- **GUI doğrulama (manuel, wx-kurulu makinede):** input'a yaz → label canlı değişir; spin'i oynat → bağlı gösterim güncellenir; reset → model'den widget'lar güncellenir. (headless senkronu test edemez — Faz 3-4 ile aynı kabul.)
- **Regresyon:** mevcut 2310 test yeşil kalır; ~4-5 yeni emit-IR testi.

---

## 8. Kapsam Dışı (Non-Goals)

- `Model.destroy` / tek tek binding kaldırma (model uygulama ömrü boyunca yaşar).
- Türetilmiş/hesaplanan bağlamalar (formül, computed), doğrulama (validation), biçimlendirme (format/mask).
- Koleksiyon bağlama (ListBox/DataGrid ↔ dizi/satır kümesi).
- Sade Liva sınıf-alanına bağlama (dile property-notify makinesi gerektirir).
- Aynı anahtarın hem text hem int kullanılması (ayrı namespace'ler; karıştırma desteklenmez).
- Çok-iş-parçacıklı erişim (wx tek UI thread varsayımı).

---

## 9. Riskler ve Karşı Önlemler

| Risk | Önlem |
|------|-------|
| Geri-besleme döngüsü (setText → wxEVT_TEXT → handler → setText...) | Tek global `g_modelUpdating` bayrağı; handler başında erken dönüş. wx tek-thread UI'da yeterli. |
| Kaynak widget'ta imleç/seçim bozulması | Propagasyonda kaynak handle atlanır (widget→model yönünde diğerlerine yazılır, kaynağa değil). |
| Model handle ↔ widget handle karışması | Model handle `g_nextHandle++` (benzersiz) ama yalnız `g_models`'ta; widget handle yalnız `g_handles`'ta. Çapraz arama yok (`g_treeNodes` deseni). |
| Yok edilmiş widget'a yazma | `liva_ui_set_text`/`set_value` `getHandle` null-kontrollü → no-op. Bağlama kaydı sızar ama güvenli (mevcut desenle tutarlı). |
| Tip uyumsuzluğu (text widget'a bindInt) | Dokümanda belirtilir; `get_value` text widget'ta 0 döner — sessiz yanlış-kullanım, zorlanmaz (kapsam dışı). |
| string ömrü (`model_get_text`) | Mevcut `returnTempStr` deseni — kanıtlanmış. |
| wx-yok ortamda gerçek senkron test edilemez | IR-emit doğrulama (+ `hasRuntimeCall`) + wx-kurulu makinede `.exe` + GUI teyidi (Faz 3-4 ile aynı kabul). |
