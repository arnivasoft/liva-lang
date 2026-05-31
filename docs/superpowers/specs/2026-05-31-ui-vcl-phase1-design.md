# Faz 1 — VCL Benzeri UI Kütüphanesi: Sınıf Tabanlı Temel

**Tarih:** 2026-05-31
**Durum:** Tasarım onaylandı, implementasyon planı bekliyor
**Kapsam:** wxWidgets tabanlı `ui::widgets` kütüphanesinin temel mimarisini, Delphi VCL'in `TControl` modeline yakın, sınıf tabanlı ve tip-güvenli bir API'ye dönüştürmek.

---

## 1. Amaç ve Bağlam

Proje şu an wxWidgets üzerine ince bir Liva sargısı sunuyor: ~70 ham global fonksiyon (`createButton`, `onClick`, `sizerAdd`...) `IRGenCall.cpp`'de isimle özel-işlenip `liva_ui_*` extern "C" fonksiyonlarına çevriliyor; `wx_runtime.cpp` bunları wxWidgets'e bağlıyor. Üst katmanda struct wrapper'lar (`widgets.liva`) var ama tüm örnekler ham API kullanıyor.

Hedef: VCL'in *karakterini* (bileşen hiyerarşisi, ortak davranışın taban sınıfta toplanması, nesne hissi veren inşa/metot sözdizimi) Swift sözdiziminden uzaklaşmadan kazandırmak.

### Mevcut durumun analizinden çıkan kritik bulgular

- **Ortak taban yok:** Her widget bağımsız `struct { var handle: i32 }`. Generic kod ve ortak davranış paylaşımı imkânsız.
- **İki paralel API:** Struct wrapper'lar var ama örnekler ham fonksiyonları kullanıyor; struct metotları `parent: i32` alıyor (tip-güvensiz).
- **Callback dangling riski:** Closure env'i outer fonksiyonun **stack'inde** alloca ediliyor (`IRGenExpr.cpp:833`). Bind edip return eden yardımcı fonksiyonlarda env ölür → dangling. Mevcut örneklerde gerçek bug yok çünkü hepsi `main()` içinde, `appRun()` app bitene kadar bloke ediyor.
- **`onKey` keycode'u atıyor:** `wx_runtime.cpp:564` `evt.GetKeyCode()` okumadan `cb.invoke()` çağırıyor → `focus.liva`'daki tüm `KEY_*` sabitleri pratikte kullanılamaz.

### Liva yetenek doğrulaması (yeni dil özelliği GEREKMİYOR)

- `class` + `init(...)` + `Type(args)` doğrudan inşa (Swift tarzı, `classes.liva:300`).
- Kalıtım: `class Button: Control`, `super.init(...)`, `override`, `is`/`as?`.
- `dyn Protocol` / trait object'ler ve `[dyn Control]` koleksiyonlar.
- Trailing closure (`ParseExpr.cpp:454`): `btn.onClick |_h| { ... }`.
- Closure literali: Rust tarzı pipe `|_h: i32| { ... }`; değişkene atanıp geçilebilir.
- Metot erişimi yalnız `.` (nokta) — `->` operatörü yok, Swift de `.` kullanır.

---

## 2. Alınan Tasarım Kararları

| Karar | Seçim | Gerekçe |
|-------|-------|---------|
| API stratejisi | **Tamamen değiştir** | Ham API kullanıcıya kapatılır, dahili FFI olarak kalır; tek temiz yol. |
| Widget modeli | **Sınıf hiyerarşisi** | `class Control` tabanı + `class Button: Control`. VCL'in `TControl` modeline birebir; `Button(panel,"x")` Swift inşa. |
| Callback ömrü | **Widget'a bağla** | Env widget destroy'da serbest bırakılır. |
| Env kapsamı | **Option A — hedefli inline UI callback** | Dil çekirdeğine dokunmaz; idiyomatik kodu güvenli kılar. |

---

## 3. Mimari

```
Kullanıcı kodu
    │  import ui::widgets
    ▼
ui::widgets  ──  class Control + alt sınıflar          ← TEK genel API
    │  içeride çağırır (dahili)
    ▼
liva_ui_* intrinsic'leri  (IRGenCall.cpp özel-işleme)  ← dahili FFI, kullanıcıya kapalı
    ▼
wx_runtime.cpp / .h  (wxWidgets)                        ← runtime
```

Ham global fonksiyonlar (`createButton`, `onClick`...) genel `std::ui` modül yüzeyinden çıkarılır; yalnız sınıf gövdelerinin içinde çağrılabilir kalır. Intrinsic eşlemeleri `IRGenCall.cpp`'de durur (codegen için gerekli).

---

## 4. Widget Sınıf Hiyerarşisi

```liva
// Taban — ortak davranış tek yerde (VCL TControl)
class Control {
    var handle: i32
    init(h: i32) { self.handle = h }

    func setEnabled(b: bool)            { setEnabledRaw(self.handle, b) }
    func setVisible(v: bool)            { setVisibleRaw(self.handle, v) }
    func setBounds(x: i32, y: i32, w: i32, h: i32) { setBoundsRaw(self.handle, x, y, w, h) }
    func setTooltip(t: string)          { setTooltipRaw(self.handle, t) }
    func setFont(size: i32, bold: bool) { setFontRaw(self.handle, size, bold) }
    func setColor(fg: Color, bg: Color) { ... }
}

class Button: Control {
    init(parent: Control, label: string) {
        super.init(createButton(parent.handle, label))
    }
    // onClick: intrinsic event metodu (bkz. §5)
    func setText(t: string) { setTextRaw(self.handle, t) }
}

class Label: Control     { init(parent: Control, text: string) { super.init(createLabel(parent.handle, text)) } ... }
class TextInput: Control { init(parent: Control, value: string) { super.init(createTextInput(parent.handle, value)) } ... }
// Checkbox, Slider, ProgressBar, RadioGroup, Dropdown, TextArea, ListBox,
// TabView, ScrollView, ImageView, Divider, Canvas, Window, Panel — hepsi Control'den türer.
```

**Window** özel: tepe seviye `wxFrame`, parent'sız inşa: `Window(800, 600, "Başlık")`.

### Kullanım (hedeflenen ergonomi)
```liva
import ui::widgets

func main() {
    appInit()
    let win = Window(500, 400, "Kayıt Formu")
    let panel = Panel(win)

    let btn = Button(panel, "Gönder")          // Swift init, .new yok
    btn.setEnabled(true)                        // Control'den miras
    btn.onClick(|_h| { messageBox("OK", "Gönderildi", 1) })

    // Heterojen koleksiyon
    let kids: [dyn Control] = [btn, Label(panel, "Merhaba")]

    win.show()
    appRun()
}
```

---

## 5. Event Binding ve Callback Ömrü

### Intrinsic event metotları
Event-binding metotları (`onClick`, `onChange`, `onSelect`, `onKey`, `onClose`, `onPaint`) normal Liva metot gövdesi DEĞİL, **derleyici-intrinsic'leridir**. `IRGenCall.cpp`, alıcısı bir `Control` alt-tipi olan bu metot çağrılarını tanır.

**Neden intrinsic?** Sınıf seçimiyle `btn.onClick(cb)` normal bir metot olsaydı, closure literali metot gövdesine girer, FFI'ye değişken olarak ulaşır ve "literali bind yerinde tespit et" stratejisi (Option A) çalışmazdı. Intrinsic olarak derleyici literali doğrudan çağrı yerinde görür.

### Heap-env mekanizması (Option A)
`recv.onClick(<closure literal>)` için derleyici:
1. Closure env struct tipini (`__env_N`) ve **boyutunu** bilir.
2. Env'i stack yerine `malloc(size)` ile heap'e kopyalar.
3. FFI'yi yeni imzayla çağırır: `liva_ui_on_click(handle, func, env, size)`.
4. Runtime, env pointer'ını widget kaydında saklar; widget destroy edilince `free(env)`.

Non-literal arg (`let cb = |..|{}; btn.onClick(cb)`) → eski stack-env yolu + **derleme uyarısı** (`callback may dangle if bound outside a long-lived scope`).

UI-dışı closure'lar (`arr.map(|x| ...)`) hiç etkilenmez — yalnız Control-alıcılı event metotları bu yolu tetikler.

### onKey keycode düzeltmesi
Yeni FFI callback imzası: `func(env, handle, keycode)`. `liva_ui_on_key`, `evt.GetKeyCode()`'u callback'e geçirir:
```liva
btn.onKey(|_h: i32, key: i32| {
    if key == KEY_ENTER() { submit() }
})
```

### Faz 1 kısıtı (belgelenir)
Widget callback'leri **değer tipi** yakalamalı (i32 / bool / f64 / handle). Heap-sahipli nesne (ör. `string`) yakalanırsa env sığ (shallow memcpy) kopyalanır — derin kopya/drop-thunk Faz 3'e bırakıldı.

---

## 6. Runtime Değişiklikleri (`wx_runtime.cpp` / `.h`)

- Callback bind fonksiyonları (`liva_ui_on_click/change/select/close/paint`) `void *env` + `int32_t size` parametreleri alır; env'i heap'te tutmaz, **çağıran derleyici kodu** zaten heap'e kopyalamış olarak verir; runtime pointer'ı widget kaydına ekler.
- Her widget handle kaydına bağlı `std::vector<void*> ownedEnvs` tutulur; `liva_ui_destroy_widget` (ve wx parent-child yıkımı) sırasında `free` edilir.
- `liva_ui_on_key` imzası `func(env, handle, keycode)` olur; `evt.GetKeyCode()` aktarılır.
- Geri kalan widget yıkımı wxWidgets'in parent-child sahipliğine güvenir; handle tablosu girişleri destroy'da temizlenir.

---

## 7. Migrasyon

- `ui::widgets` tüm widget'ları `Control` alt sınıfı olarak yeniden tanımlar.
- `composite.liva`, `layout.liva`, `theme.liva`, `router.liva`, `listview.liva`, `tooltip.liva` sınıf API'sine geçer (`i32 handle` yerine `Control` / alt tip alır).
- `ModuleLoader.cpp`: `std::ui` ihracı sınıf-merkezli; ham adlar dahili kalır.
- `IRGenCall.cpp`: Control-alıcılı event metotları için intrinsic tanıma eklenir; create/property intrinsic'leri korunur.
- Örnekler sınıf API'sine taşınır: `ui_form.liva`, `ui_hello_wx.liva`, `ui_showcase_demo.liva`, `ui_counter.liva`, `ui_callback_demo.liva`, `ui_validation_demo.liva`, `ui_composite_demo.liva`, `ui_form_themed.liva`, `ui_widgets_demo.liva`, `ui_paint.liva`, `ui_hello.liva`.
- Yeni örnek: `buildCounter`-tarzı yardımcı-fonksiyon kalıbının artık güvenli çalıştığını gösteren demo.

---

## 8. Test Stratejisi

- **Derleme/IR testleri** (`UIModuleTest.cpp` genişler): her widget sınıfı, `Control` kalıtımı, `Button(panel,"x")` inşa, `dyn Control` koleksiyon, event metotları, trailing closure → derlenir, IR doğrulanır (GUI headless çalışamaz).
- **Callback ömrü IR testi:** `buildCounter` kalıbı derlenip event metodunun heap-env + boyutlu FFI imzasını (`liva_ui_on_click(handle, func, env, size)`) ürettiği doğrulanır.
- **Saf-fonksiyon birim testleri:** `types.liva` (`Color`, `Rect.contains`), `animation.liva` easing, layout flag sabitleri — headless çalıştırılır.
- **onKey testi:** keycode'un callback'e ulaştığı IR seviyesinde doğrulanır.
- **Regresyon:** mevcut 2064 test geçmeye devam eder; UI örnekleri sınıf API'sine taşınınca düzelir.

---

## 9. Kapsam Dışı (Non-Goals)

**Faz 2:** Menü / Toolbar / StatusBar, `Align` / `Anchors`, yeni widget'lar (TreeView, Grid, SpinCtrl), event payload genişlemesi (mouse/focus konumu), `onRightClick` / context menu.

**Faz 3:** Form DSL / görsel designer, data binding, `animation.liva` → timer köprüsü, drag&drop tamamlanması.

**Faz 1'de kesinlikle YOK:** genel escaping-closure heap (Option C), drop-thunk'lı derin env kopyası, ownership-checker entegrasyonu, global closure ABI değişikliği. Non-literal callback yalnız uyarı alır.

---

## 10. Riskler ve Karşı Önlemler

| Risk | Önlem |
|------|-------|
| Intrinsic event-metot tanıma, normal metot çözümlemesiyle çakışabilir | Yalnız Control-alıcı + bilinen event-metot adı + closure argümanı kombinasyonunda tetikle; aksi halde normal yola düş. |
| Heap-env sızıntısı (destroy hiç çağrılmazsa) | Env'i widget kaydına bağla; wx parent-child yıkımında ve `destroyWidget`'te free et. Window kapanınca tüm ağaç yıkılır. |
| `string` yakalama sığ kopya sorunu | Faz 1 kısıtı olarak belgele; değer-tipi yakalama öner. |
| 2064 testte regresyon | UI örneklerini aşamalı taşı; her adımda `ctest` çalıştır. |
