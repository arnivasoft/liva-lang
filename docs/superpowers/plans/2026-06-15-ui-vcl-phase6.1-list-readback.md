# Faz 6.1 — Model Listesinden Geri Okuma (`listGet`) Implementasyon Planı

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Faz 6 koleksiyon bağlamasına, model'in string listesinden indeksle tek öğe okuyan `listGet(key, index) -> string` erişimcisini eklemek.

**Architecture:** Faz 6 ile aynı 6-katman native binding deseni, tek FFI. String döndürme için mevcut `liva_ui_model_get_text` deseni (`returnTempStr`) yeniden kullanılır — ömür güvenli, yeni altyapı yok. Salt-okuma; bağlama durumunu/widget'ları değiştirmez. Sınır-dışı/bilinmeyen → `""`.

**Tech Stack:** C++20, LLVM 21 IRGen, wxWidgets 3.3 (vcpkg), CMake/Ninja (`build-clang/`), GoogleTest.

**Spec:** `docs/superpowers/specs/2026-06-15-ui-vcl-phase6.1-list-readback-design.md`

---

## Dokunulacak Dosyalar

- **`tests/unit/UICodegenExecTest.cpp`** — `ModelListGetCompiles` emit-IR testi (Task 1, Step 1).
- **`stdlib/ui/wx_runtime.h`** — 1 FFI prototipi.
- **`stdlib/ui/wx_runtime.cpp`** — `liva_ui_model_list_get` implementasyonu.
- **`src/IR/IRGen.cpp`** — yeni yerel tip + extern.
- **`src/IR/IRGenCall.cpp`** — `modelListGet` intrinsic.
- **`src/Sema/ModuleLoader.cpp`** — `modelListGet` builtin adı.
- **`stdlib/ui/widgets.liva`** — `Model.listGet` metodu.

### Ortak komutlar
- Tam derleme: `cmake --build build-clang`
- Tek test: `ctest --test-dir build-clang -R "<TestAdı>" --output-on-failure`
- Tam regresyon (SERİ — `-j` kullanma): `ctest --test-dir build-clang --output-on-failure`

---

## Task 1: `listGet` geri-okuma erişimcisi

`liva_ui_model_list_get` FFI + derleyici bağlama (extern/intrinsic/builtin) + `Model.listGet` metodu.

**Files:**
- Test: `tests/unit/UICodegenExecTest.cpp`
- Modify: `stdlib/ui/wx_runtime.h`, `stdlib/ui/wx_runtime.cpp`, `src/IR/IRGen.cpp`, `src/IR/IRGenCall.cpp`, `src/Sema/ModuleLoader.cpp`, `stdlib/ui/widgets.liva`

- [ ] **Step 1: Write the failing test**

`tests/unit/UICodegenExecTest.cpp` içinde, son `TEST`'ten sonra (`#endif // LIVA_HAS_LLVM`'den önce — Faz 6 `ModelListBindCompiles` testinin hemen ardına) ekle:

```cpp
// ── Phase 6.1: list readback ───────────────────────────────────────────
TEST(UICodegenExec, ModelListGetCompiles) {
    auto ir = emitIR(
        "import ui::widgets\n"
        "func main() {\n"
        "  appInit()\n"
        "  let model = Model()\n"
        "  model.listAdd(\"items\", \"a\")\n"
        "  model.listAdd(\"items\", \"b\")\n"
        "  let n = model.listCount(\"items\")\n"
        "  var i = 0\n"
        "  while i < n {\n"
        "    println(model.listGet(\"items\", i))\n"
        "    i = i + 1\n"
        "  }\n"
        "}\n",
        "model_list_get");
    ASSERT_TRUE(emitsClean(ir));
    EXPECT_TRUE(hasRuntimeCall(ir, "liva_ui_model_list_get"))
        << "listGet must lower to liva_ui_model_list_get";
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.ModelListGetCompiles" --output-on-failure`
Expected: FAIL — `no IR emitted` (`modelListGet` builtin'i tanınmıyor; `listGet` metodu yok).

- [ ] **Step 3a: Add runtime FFI prototype**

`stdlib/ui/wx_runtime.h` içinde, `liva_ui_model_list_count` prototipinden (satır ~144) hemen sonra ekle:

```c
const char *liva_ui_model_list_get(int32_t model, const char *key, int32_t index);
```

- [ ] **Step 3b: Add runtime implementation**

`stdlib/ui/wx_runtime.cpp` içinde, `liva_ui_model_list_count` fonksiyonundan (satır ~1544) sonra ekle:

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

> `returnTempStr` mevcut (Faz 5 `liva_ui_model_get_text`, satır ~1401 aynısını kullanır). Bulunamazsa raporla.

- [ ] **Step 3c: Add IRGen extern**

`src/IR/IRGen.cpp` içinde, Faz 6 `liva_ui_model_bind_list` extern'inden (satır ~1709) hemen sonra ekle:

```cpp
    // ── Phase 6.1: list readback ─────────────────────────────────────
    auto *uiI32StrI32RetStrTy =
        llvm::FunctionType::get(i8PtrTy, {i32Ty, i8PtrTy, i32Ty}, false);
    module_->getOrInsertFunction("liva_ui_model_list_get", uiI32StrI32RetStrTy);
```

> `i8PtrTy` (satır ~394) ve `i32Ty` bu fonksiyon kapsamında tanımlı (Faz 5/6 externleri aynısını kullanır). `i32Ty` kapsamda değilse `auto *i32Ty = builder_->getInt32Ty();` yerel olarak ekle ve raporla. `(i32,ptr,i32)→ptr` tipi mevcut değil — bu yüzden yeni `uiI32StrI32RetStrTy` tanımlanıyor (Faz 6 `uiI32StrRetStrTy` (i32,ptr)→ptr idi, üçüncü i32 yok).

- [ ] **Step 3d: Add IRGenCall intrinsic**

`src/IR/IRGenCall.cpp` içinde, Faz 6 `modelListCount` intrinsic'inden sonra (`// Closure-taking free-function forms` yorumundan ÖNCE) ekle:

```cpp
    // ── Phase 6.1: list readback ─────────────────────────────────────
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

- [ ] **Step 3e: Register builtin name**

`src/Sema/ModuleLoader.cpp` içinde, Faz 6 satırını bul:

```cpp
         "modelBindList", "modelListAdd", "modelListClear", "modelListCount"});
```

ve şuna değiştir:

```cpp
         "modelBindList", "modelListAdd", "modelListClear", "modelListCount",
         // Phase 6.1: list readback
         "modelListGet"});
```

- [ ] **Step 3f: Add Model.listGet method**

`stdlib/ui/widgets.liva` içinde, `Model` sınıfındaki `listCount` metodundan (satır ~348) sonra ekle:

```liva
    pub func listGet(key: string, index: i32) -> string { return modelListGet(self.handle, key, index) }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-clang && ctest --test-dir build-clang -R "UICodegenExec.ModelListGetCompiles" --output-on-failure`
Expected: PASS. Tam `cmake --build build-clang` `wx_runtime.cpp`'yi de derler; `liva_ui_model_list_get` imza/tip hatası vermemeli. Bir wx/tip hatası çıkarsa doğrusunu bul ve sapmayı raporla.

- [ ] **Step 5: Run the full regression suite**

Run: `cmake --build build-clang && ctest --test-dir build-clang --output-on-failure`
Expected: TÜM testler PASS (mevcut + yeni `ModelListGetCompiles`). Seri çalıştır (`-j` kullanma — bilinen paralel yarışlar var). Tam pass/fail sayısını raporla.

- [ ] **Step 6: Commit**

```bash
git add tests/unit/UICodegenExecTest.cpp stdlib/ui/wx_runtime.h stdlib/ui/wx_runtime.cpp src/IR/IRGen.cpp src/IR/IRGenCall.cpp src/Sema/ModuleLoader.cpp stdlib/ui/widgets.liva
git commit -m "ui-phase6.1: listGet — model listesinden indeksle geri okuma

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Tamamlama

Task 1 tamamlanınca özellik biter (tek FFI). `superpowers:finishing-a-development-branch` gerekmez — Faz 6 gibi doğrudan `master`'da geliştirilir, regresyon yeşil olunca commit yeterli. İsteğe bağlı: `examples/ui_collection_binding.liva`'ya seçili öğeyi `listGet` ile okuyup Label'a yazan bir satır eklenip GUI'de teyit edilebilir (ayrı, opsiyonel commit).
