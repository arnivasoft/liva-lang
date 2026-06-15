# Faz 6.1 — Model Listesinden Geri Okuma (`listGet`) Tasarımı

**Tarih:** 2026-06-15
**Durum:** Onaylandı
**Önceki:** [Faz 6 — Koleksiyon Bağlama](2026-06-02-ui-vcl-phase6-collection-binding-design.md)

---

## 1. Amaç

Faz 6 koleksiyon bağlamasını, model'in tuttuğu string listesinden **indeksle tek öğe okuyacak** bir erişimci ile genişletmek. Faz 6 yalnız yazma + bağlama sağlıyordu (`bindList`/`listAdd`/`listClear`/`listCount`); öğeleri model'den geri okumak mümkün değildi.

Mevcut `listCount(key) -> i32` ile birlikte `listGet` tam iterasyon sağlar:

```liva
let n = model.listCount("dosyalar")
var i = 0
while i < n {
    println(model.listGet("dosyalar", i))
    i = i + 1
}
```

## 2. Kapsam

**Dahil:**
- `listGet(key: string, index: i32) -> string` — model listesinden indeksle tek öğe.

**Kapsam dışı (Non-Goals):**
- `listItems(key) -> [string]` — tüm listeyi tek çağrıda döndürme. UI runtime'da `[string]`/DynArray **return** köprüsü yok; Faz 6 bu marshalling'i kasıtlı erteledi. Ayrı, daha büyük bir dilim.
- Liste öğesi silme-indeksten / öğe güncelleme / sıralama / filtreleme.
- İki-yönlü öğe düzenleme.

## 3. Mimari

Faz 6 ile **aynı 6-katman native binding deseni**, tek FFI ekler. String döndürme için mevcut `liva_ui_model_get_text` deseni (`returnTempStr`) birebir yeniden kullanılır — ömür güvenli, yeni altyapı yok.

### 3.1 Runtime FFI — `stdlib/ui/wx_runtime.h`

`liva_ui_model_list_count` prototipinden sonra:

```c
const char *liva_ui_model_list_get(int32_t model, const char *key, int32_t index);
```

### 3.2 Runtime implementasyon — `stdlib/ui/wx_runtime.cpp`

`liva_ui_model_list_count` fonksiyonundan sonra:

```cpp
const char *liva_ui_model_list_get(int32_t model, const char *key, int32_t index) {
    auto it = g_models.find(model);
    if (it == g_models.end()) return "";
    auto vit = it->second.listVals.find(key ? key : "");
    if (vit == it->second.listVals.end()) return "";
    if (index < 0 || index >= static_cast<int32_t>(vit->second.size())) return "";
    return returnTempStr(wxString::FromUTF8(vit->second[index]));
}
```

### 3.3 IRGen extern — `src/IR/IRGen.cpp`

(i32, ptr, i32) → ptr tipi mevcut değil; yeni yerel tip tanımlanır. Faz 6 `liva_ui_model_bind_list` extern'inden sonra:

```cpp
auto *uiI32StrI32RetStrTy =
    llvm::FunctionType::get(i8PtrTy, {i32Ty, i8PtrTy, i32Ty}, false);
module_->getOrInsertFunction("liva_ui_model_list_get", uiI32StrI32RetStrTy);
```

> `i8PtrTy` ve `i32Ty` bu kapsamda mevcut (Faz 5/6 blokları). Biri kapsamda değilse yerel eşdeğeri (`llvm::PointerType::getUnqual(*context_)`, `builder_->getInt32Ty()`) ile tanımla ve raporla.

### 3.4 IRGenCall intrinsic — `src/IR/IRGenCall.cpp`

Faz 6 `modelListCount` intrinsic'inden sonra:

```cpp
// modelListGet(model, key, index) -> string
if (funcName == "modelListGet" && node->getArgs().size() >= 3) {
    auto *m = visit(node->getArgs()[0].get());
    auto *k = visit(node->getArgs()[1].get());
    auto *i = visit(node->getArgs()[2].get());
    if (!m || !k || !i) return nullptr;
    return builder_->CreateCall(getOrPanic("liva_ui_model_list_get"),
                                {m, k, i}, "ui.mlget");
}
```

### 3.5 Builtin adı — `src/Sema/ModuleLoader.cpp`

Faz 6 satırına `modelListGet` eklenir:

```cpp
"modelBindList", "modelListAdd", "modelListClear", "modelListCount",
// Phase 6.1: list readback
"modelListGet"
```

### 3.6 Model metodu — `stdlib/ui/widgets.liva`

`Model` sınıfında `listCount` metodundan sonra:

```liva
pub func listGet(key: string, index: i32) -> string { return modelListGet(self.handle, key, index) }
```

## 4. Veri Akışı

```
Liva: model.listGet("k", i)
  -> modelListGet builtin (ModuleLoader tanır)
  -> IRGenCall intrinsic: liva_ui_model_list_get(handle, "k", i) çağrısı
  -> runtime: g_models[handle].listVals["k"][i] -> returnTempStr -> const char*
  -> Liva string
```

Tek yön, salt-okuma. Bağlama (kind 2) durumunu değiştirmez; widget'lara dokunmaz.

## 5. Hata Yönetimi

| Durum | Davranış |
|-------|----------|
| Bilinmeyen model handle | `""` döner |
| Bilinmeyen key | `""` döner |
| `index < 0` veya `index >= size` | `""` döner |
| `key == nullptr` | `""` (boş key olarak ele alınır) |

Crash yok, panic yok — Faz 6'nın no-op güvenli deseniyle tutarlı. Çağıran sınırları `listCount` ile kontrol eder; sınır-dışı erişim sessizce `""` verir.

## 6. Test Stratejisi

- **Codegen (otomatik):** `tests/unit/UICodegenExecTest.cpp` → `ModelListGetCompiles`. `listGet` çağrısının temiz IR ürettiğini (`emitsClean`) ve `liva_ui_model_list_get`'e lower olduğunu (`hasRuntimeCall`) assert eder.
- **Runtime C++ (otomatik):** Tam `cmake --build build-clang` `wx_runtime.cpp`'yi derler — imza/tip hataları yakalanır.
- **GUI (manuel, opsiyonel):** `examples/ui_collection_binding.liva`'ya bir geri-okuma satırı eklenebilir (ör. seçili indeksin metnini `listGet` ile okuyup Label'a yazma). wx-kurulu makinede `.exe` ile teyit.

## 7. Riskler

| Risk | Önlem |
|------|-------|
| `returnTempStr` ömrü (sonraki çağrıda üzerine yazılır) | Faz 5 `model_get_text` ile aynı kabul; dönen string anında kullanılır/kopyalanır. |
| Yeni tip (`uiI32StrI32RetStrTy`) ad çakışması | Yerel `auto*`, dosya kapsamında benzersiz. |
| (i32,ptr,i32)→ptr tipinin yokluğu | Yeni yerel tip tanımı (§3.3). |

## 8. Tamamlama

Tek FFI + 4 ufak bağlama noktası + 1 test. Tamamlanınca regresyon (`ctest`) çalıştırılır ve `master`'a commit'lenir (Faz 6 doğrudan master'da geliştirildi; aynı akış).
