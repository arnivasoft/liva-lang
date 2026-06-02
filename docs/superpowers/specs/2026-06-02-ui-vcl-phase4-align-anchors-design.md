# Faz 4 — Align/Anchors Yerleşim

**Tarih:** 2026-06-02
**Durum:** Tasarım onaylandı, implementasyon planı bekliyor
**Branch:** `feat/ui-vcl-phase4-align-anchors` (master üzerinde — Faz 1-3 merge edilmiş)
**Kapsam:** wxWidgets tabanlı `ui::widgets` kütüphanesine Delphi VCL'in imza yerleşim modelini eklemek: **Align** (kenar-dock) + **Anchors** (orantılı sabitleme). Sizer'lara alternatif, saf VCL yerleşim.

---

## 1. Amaç ve Bağlam

Faz 1-3 sınıf-tabanlı widget temelini, uygulama çerçevesini ve altı yeni widget'ı kurdu. Yerleşim şu ana dek yalnız wxSizer ile yapılıyordu — Faz 3 örneğinde manuel sizer yönetiminin kırılganlığı görüldü (frame'in çoklu çocuğu sizer olmadan yerleşmiyor). Delphi VCL'de yerleşimin temeli sizer değil, **Align** ve **Anchors** özellikleridir; bu Faz, o modeli getirir.

- **Align** — bir widget'ı ebeveynin bir kenarına dock eder (`top/bottom/left/right`) veya kalan alanı doldurur (`client`). Sıralı kenar-tüketimi: her dock'lanan, kalan istemci dikdörtgeninden bir şerit alır.
- **Anchors** — bir widget'ın kenarlarını ebeveynin kenarlarına sabitler `[left, top, right, bottom]`. Ebeveyn yeniden boyutlanınca widget orantılı olarak taşınır/esner.

### Faz 1-3'ten devralınan altyapı
- **3 katman:** Liva `Control` sınıfı → `liva_ui_*` intrinsic (IRGenCall.cpp) → `wx_runtime.cpp` (wxWidgets).
- **Handle deseni:** her widget i32 handle; `getHandle<wxWindow>(h)`.
- **Yan tablo deseni:** wx-olmayan veri için handle→kayıt map (örn. `g_treeNodes`); Align/Anchors için `g_layouts` aynı deseni kullanır.
- **Bool→i32 köprüsü:** mevcut `setEnabled(b: bool)` deseni (Control içinde `if b { v = 1 }`).
- **wx link:** `liva_ui.lib` + `wx_libs.cfg`; `ui::` import'u runtime'ı linkler.

### Teknik doğrulama (yapıldı)
- **Basit (payload'sız) enum'lar IR'de düz `i32` discriminant** olarak temsil edilir (`IRGenDecl.cpp:1292`, `IRGenCall.cpp:7502` — "just an i32 tag value"). Yani `Align.top` zaten bir i32 değeridir → setAlign FFI'sine doğrudan geçirilir; enum→i32 indirme için özel iş gerekmez.
- Enum sözdizimi: `enum X { case A = 0 }`, erişim `X.A` (bkz. `examples/enum_match.liva`, discriminant'lar memory'de kanıtlı).
- `wxEVT_SIZE` / `wxSizeEvent` ve `wxWindow::GetClientSize/SetSize/GetRect/GetChildren` standart, kurulu wx 3.3'te mevcut.

---

## 2. Alınan Tasarım Kararları

| Karar | Seçim |
|-------|-------|
| Faz 4 dilimi | Align/Anchors yerleşim (Form DSL/data binding/dnd/animation ayrı spec'ler) |
| Kapsam | **Align + Anchors ikisi birden** (tam VCL modeli) |
| API şekli | `setAlign(a: Align)` enum + `setAnchors(left, top, right, bottom)` 4 bool |
| Uygulama | Özel yerleşim yöneticisi: ebeveynin `wxEVT_SIZE`'ına bağlı VCL `AlignControls` algoritması (sizer'a çevirme DEĞİL — Anchors orantılı boyut gerektirir, sizer'la ifade edilemez) |
| Sizer ile birlikte | Ebeveyn başına **biri**: sizer ya da align/anchors; son ayarlanan kazanır (align modu sizer'ı kaldırır) |
| Varsayılan | `align = none`, `anchors = [left, top]` (sol-üst sabit, sabit boyut) — VCL ile aynı |
| Branch | Yeni `feat/ui-vcl-phase4-align-anchors` (master üzerinde) |

---

## 3. Mimari ve Yerleşim Algoritması

Faz 1-3'ün 3 katmanı korunur. Yeni: runtime'da ebeveyn-başına bir yerleşim yöneticisi.

```
Kullanıcı kodu ── widget.setAlign(Align.top) / widget.setAnchors(...)
    ▼
Control.setAlign / setAnchors → setAlign / setAnchors intrinsic'leri
    ▼
liva_ui_set_align / liva_ui_set_anchors (wx_runtime.cpp)
    ▼  (g_layouts kaydı + ebeveyne wxEVT_SIZE bağla + relayoutParent)
relayoutParent(parent): VCL AlignControls — kenar-dock + anchor matematiği
```

### Çalışma zamanı durumu

```cpp
struct LivaLayout {
    int  align = 0;             // 0=none,1=top,2=bottom,3=left,4=right,5=client
    bool aLeft = true, aTop = true, aRight = false, aBottom = false;  // VCL default
    wxRect refBounds;           // anchor referans dikdörtgeni (set anında yakalanır)
    wxSize refParentClient;     // anchor referans ebeveyn istemci boyutu
    bool   hasRef = false;      // ilk gerçek boyutta lazy doldurma
};
// Anahtar wxWindow* (handle değil): relayout doğrudan parent->GetChildren()
// pointer'larıyla bakar; set_align/set_anchors getHandle ile pointer'ı çözer.
static std::unordered_map<wxWindow *, LivaLayout> g_layouts;
// wxEVT_SIZE işleyicisi bağlanmış ebeveynler (çift-bağlamayı önler)
static std::unordered_set<wxWindow *> g_layoutParents;
```

### `relayoutParent(wxWindow *parent)` — VCL `AlignControls`

```
clientW, clientH = parent->GetClientSize()
R = wxRect(0, 0, clientW, clientH)

// 1) Kenar-dock (z-sırasında parent->GetChildren()):
//    sıra: önce top/bottom/left/right (her biri R'den şerit tüketir)
for child in children with align in {top,bottom,left,right}:
    top:    child.SetSize(R.x, R.y, R.width, ch);            R.y += ch; R.height -= ch
    bottom: child.SetSize(R.x, R.y+R.height-ch, R.width, ch); R.height -= ch
    left:   child.SetSize(R.x, R.y, cw, R.height);           R.x += cw; R.width -= cw
    right:  child.SetSize(R.x+R.width-cw, R.y, cw, R.height); R.width -= cw
    // (ch/cw = çocuğun mevcut yükseklik/genişliği — dock yönündeki boyut korunur)

// 2) client: kalan R'yi doldur (yalnız ilki; sonrakiler sıfır alan)
for child in children with align == client:
    child.SetSize(R.x, R.y, R.width, R.height); R = empty

// 3) none: anchors uygula (refBounds + ebeveyn boyut deltası)
dw = clientW - lay.refParentClient.w;  dh = clientH - lay.refParentClient.h
for child in children with align == none:
    nx = refBounds.x; nw = refBounds.w
    if  aLeft &&  aRight: nw = refBounds.w + dw            // yatay esner
    elif !aLeft && aRight: nx = refBounds.x + dw           // sağ kenarla taşınır
    elif !aLeft && !aRight: nx = refBounds.x + dw/2        // orantılı kayar
    // (aLeft && !aRight: konum+boyut sabit — hiçbir şey değişmez)
    // ny/nh için aTop/aBottom ile simetrik
    child.SetSize(nx, ny, nw, nh)
```

Bu Delphi VCL `TWinControl.AlignControls` davranışının birebir karşılığıdır: `alClient` kalanı doldurur, çoklu `alTop` üst üste yığılır, anchors orantılı boyutlanır.

---

## 4. API (widgets.liva)

### Align enum

```liva
pub enum Align {
    case none = 0
    case top = 1
    case bottom = 2
    case left = 3
    case right = 4
    case client = 5
}
```

### Control taban sınıfına eklenen metotlar

```liva
// Control { ... mevcut metotlar ... }
pub func setAlign(a: Align) { setAlign(self.handle, a) }
pub func setAnchors(left: bool, top: bool, right: bool, bottom: bool) {
    var l = 0; if left { l = 1 }
    var t = 0; if top { t = 1 }
    var r = 0; if right { r = 1 }
    var b = 0; if bottom { b = 1 }
    setAnchors(self.handle, l, t, r, b)
}
```

> `setAlign` argümanı `Align` enum'u; basit enum IR'de i32 discriminant olduğundan FFI'ye doğrudan i32 olarak iner. `setAnchors` 4 bool'u i32'ye mevcut `setEnabled` desenindeki gibi çevirir.

### Kullanım örneği

```liva
import ui::widgets

func main() {
    appInit()
    let win = Window(700, 500, "Align/Anchors")
    let root = Panel(win)             // frame'i dolduran tek içerik (frame tek çocuğu doldurur)

    let toolbar = Panel(root); toolbar.setSize(700, 36); toolbar.setAlign(Align.top)
    let status  = Label(root, "Hazir"); status.setAlign(Align.bottom)
    let side    = Panel(root); side.setSize(180, 0); side.setAlign(Align.left)
    let editor  = TextArea(root, "");  editor.setAlign(Align.client)   // kalanı doldurur

    let ok = Button(root, "Tamam")
    ok.setBounds(600, 430, 80, 28)
    ok.setAnchors(false, false, true, true)    // sağ-alt köşeye sabit

    win.show()
    appRun()
}
```

---

## 5. Runtime (wx_runtime.cpp / .h)

```c
/* ── Phase 4: Align/Anchors layout ──────────────────────────────────── */
void liva_ui_set_align(int32_t handle, int32_t align);
void liva_ui_set_anchors(int32_t handle, int32_t left, int32_t top, int32_t right, int32_t bottom);
```

### Implementasyon notları
- **`g_layouts` / `g_layoutParents`:** yukarıdaki yapı; `<unordered_set>` include eklenir.
- **`liva_ui_set_align(handle, align)`:** `getHandle<wxWindow>(handle)`; `g_layouts[handle].align = align`; `captureRef(handle)` (refBounds = `GetRect()`, refParentClient = `parent->GetClientSize()`); `ensureParentBound(parent)`; `relayoutParent(parent)`.
- **`liva_ui_set_anchors(handle, l,t,r,b)`:** benzer; `aLeft=l!=0` vb.; refBounds/refParentClient yakala; `ensureParentBound`; `relayoutParent`.
- **`ensureParentBound(wxWindow *parent)`:** `parent` null değil ve `g_layoutParents`'ta yoksa: eğer parent'ın sizer'ı varsa `parent->SetSizer(nullptr, false)` (ebeveyn başına biri kuralı); `parent->Bind(wxEVT_SIZE, [parent](wxSizeEvent &e){ relayoutParent(parent); e.Skip(); })`; set'e ekle.
- **`relayoutParent`:** §3 algoritması; `parent->GetChildren()` z-sırası; her çocuğun handle'ını bulmak için ters-arama gerekmez — `g_layouts`'ı çocuğun wxWindow* üzerinden değil handle üzerinden tutuyoruz, ama relayout child→handle eşlemesi ister. **Çözüm:** `g_layouts`'ı `unordered_map<wxWindow*, LivaLayout>` yap (handle yerine pointer anahtar) — set_align/set_anchors `getHandle` ile pointer'ı bulup pointer-anahtarla saklar; relayout doğrudan `parent->GetChildren()` pointer'larıyla bakar. Dock'suz/dokunulmamış çocuk için varsayılan `LivaLayout{}` (none + [L,T], refBounds = mevcut GetRect).
- **string yok:** bu FFI'ler string döndürmez/almaz.
- **Yok etme temizliği:** widget destroy yolunda (`liva_ui_destroy_widget` ve frame-destroy süpürmesi) `g_layouts.erase(w)`; parent yok edilince `g_layoutParents.erase(parent)`.
- **Kenar durumları (§3 + Bölüm 3 kararları):** parent null → no-op; çoklu client → yalnız ilki; lazy ref (`hasRef`) ilk gerçek boyutta güncellenir; menubar/toolbar/statusbar frame tarafından dock edilir, client rect onları dışlar.

---

## 6. Derleyici

- **ModuleLoader.cpp:** `std::ui` builtin listesine `setAlign`, `setAnchors`.
- **IRGen.cpp:** extern'ler — `liva_ui_set_align` `(i32,i32)→void` (`uiSetValTy` yeniden kullanım); `liva_ui_set_anchors` `(i32,i32,i32,i32,i32)→void` (yeni yerel `uiI32x5VoidTy` = `(i32,i32,i32,i32,i32)→void`).
- **IRGenCall.cpp:** Faz 3 Phase-3 bloğunun yanında yeni Phase-4 bloğu:
  - `setAlign(handle, align)` → `liva_ui_set_align` (align argümanı basit enum → i32, doğrudan `visit`).
  - `setAnchors(handle, l, t, r, b)` → `liva_ui_set_anchors`.
  - **Fast-path'e DOKUNULMAZ** (callback değil bunlar).
- **Align enum:** `widgets.liva`'da `pub enum Align`; Parser/Sema/IRGen mevcut basit-enum yolunu kullanır (yeni dil özelliği yok).

---

## 7. Test & Örnekler

- **Doğrulama (wx-yok / headless):** `livac --emit-ir` (link yok) — `UICodegenExecTest.cpp`'ye:
  - `setAlign(Align.top)` / `Align.client` temiz derlenir (enum→i32 indirme).
  - `setAnchors(true, true, true, false)` temiz derlenir (4 bool→i32).
  - Align + anchors birlikte, çeşitli widget'larda (Panel/Button/TreeView) — Control alt sınıfı olarak otomatik kapsanır.
  - `Align` enum tanımı + `Align.top` discriminant erişimi derlenir.
- **Runtime C++ derlemesi:** bu makinede wx kurulu → `cmake --build` `wx_runtime.cpp`'yi derler, `AlignControls`/anchor matematiği C++ hatalarını yakalar.
- **Örnek:** `examples/ui_layout_align.liva` — §4 kullanım örneği (top/bottom/left/client dock + sağ-alt anchored buton); pencere yeniden boyutlanınca davranışı gösterir.
- **GUI doğrulama (manuel, wx-kurulu makinede):** gerçek `.exe` linklenir + pencere yeniden boyutlandırılarak align/anchors davranışı teyit edilir (headless yerleşimi test edemez — Faz 3 ile aynı kabul).
- **Regresyon:** mevcut 2306 test yeşil kalır; ~5-6 yeni emit-IR testi eklenir.

---

## 8. Kapsam Dışı (Non-Goals)

- **Dock paneli sürükleme** (kullanıcının dock'u runtime'da taşıması), `alCustom`.
- **Constraint-tabanlı yerleşim** (min/max boyut, oran kilidi).
- **Align değişiminde animasyon** (anlık yerleşim).
- **Split-with-align**, iç içe otomatik dock kombinasyonları (basit kenar-tüketimi + anchors yeterli).
- **Form DSL/designer, data binding, drag&drop, animation→timer** — ayrı Faz 4b+ spec'leri.

---

## 9. Riskler ve Karşı Önlemler

| Risk | Önlem |
|------|-------|
| Enum→i32 FFI indirme | Doğrulandı: basit enum IR'de i32 discriminant (`IRGenDecl.cpp:1292`). `setAlign(a: Align)` argümanı doğrudan i32 olarak `visit` edilir. Sorun çıkarsa fallback: `Align*` modül sabitleri (`WX_ALL()` gibi helper fonksiyonlar). |
| `g_layouts` anahtarı: handle mı wxWindow* mi | `wxWindow*` anahtar kullan — relayout `parent->GetChildren()` pointer'larıyla doğrudan bakar; set_align/set_anchors `getHandle` ile pointer'ı çözer. Handle→pointer ters-arama gerekmez. |
| Sizer ile çakışma | Ebeveyn başına biri; align modu `SetSizer(nullptr)` ile sizer'ı kaldırır. Dokümanda belirtilir. |
| `wxEVT_SIZE` çift-bağlama / sonsuz döngü | `g_layoutParents` set'i çift-bağlamayı önler; `SetSize` yeni `wxEVT_SIZE` üretmez (çocuk boyutu, ebeveyn değil) → döngü yok. `e.Skip()` çağrılır. |
| Anchor referansı boyut öncesi yakalanırsa | `hasRef` + lazy ilk-boyut güncellemesi; ilk gerçek `wxEVT_SIZE`'da refParentClient = mevcut client. |
| Yok etme sızıntısı | destroy yolunda `g_layouts.erase(w)` + `g_layoutParents.erase(parent)`; Faz 2/3 temizlik deseniyle uyumlu. |
| wx-yok ortamda gerçek yerleşim test edilemez | IR-emit doğrulama + wx-kurulu makinede `.exe` + GUI teyidi (Faz 3 ile aynı kabul). |
