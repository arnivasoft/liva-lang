# Argüman ve Atama Tiplemesi — Tasarım

**Tarih:** 2026-07-25
**Durum:** Onaylandı (kapsam ve IRGen boğaz-noktası tasarımı kullanıcı onaylı)

## Problem

Fonksiyon argümanları ve atamalar Sema'da **hiç tip denetimi görmüyor**;
IRGen de değeri hedefin tipine dönüştürmüyor. Tek kök, iki belirti.

Probe ile doğrulanan mevcut davranış (2026-07-25, `livac` main @ 624fd42):

| Kaynak | Bugünkü davranış |
|---|---|
| `func f(n: i64)` + `f(5)` | **LLVM verifier hatası** — "Call parameter type does not match function signature! i32 5" |
| `func f(x: f64)` + `f(3)` | Aynı verifier hatası |
| `func f(b: u8)` + `f(200)` | Aynı verifier hatası |
| `func take(xs: [i32])` + `take(mkStr())` (`mkStr() -> [string]`) | **Sessizce derleniyor ve çalışıyor** |
| `var a: [i32] = [1,2,3]; a = mkStr()` | Derleniyor; `a[0]` → `-1335196736` (pointer'ın i32 okunması) |
| Eşleşen tipler (`i32`, `string`, `[i32]`) | Çalışıyor |

İki gözlem:

1. **Skaler uyumsuzlukta** LLVM verifier arka-durdurucu görevi görüyor —
   sessiz değil ama mesaj okunaksız ve konum bilgisi yok. Daha kötüsü,
   dilin tamsayı literalleri katı `i32` olduğundan `i64`/`f64`/`u8`
   parametreli **hiçbir fonksiyon literalle çağrılamıyor**.
2. **Bileşik tiplerde** (dizi/optional) arka-durdurucu YOK: iki DynArray
   aynı LLVM tipine (`{ptr,i64,i64}`) sahip olduğundan verifier susuyor ve
   program sessizce yanlış veri okuyor.

`typesCompatible` 2026-07-25'te derinleştirildi ama yalnız **VarDecl
anotasyonu** ve **return** yollarından çağrılıyor; argüman ve atama
yolları ona hiç uğramıyor.

## Kapsam

**Dahil:**
- Kullanıcı tanımlı serbest fonksiyon çağrıları
- Struct/impl metod çağrıları, sınıf metodları ve init'leri
- Monomorfize edilmiş generik fonksiyon/metod çağrıları
- Bir **identifier** hedefe atama (`a = expr`)

**Hariç (ayrı izleme kalemleri):**
- Native builtin çağrıları — kendi elle yazılmış dönüşümleri var
  (`toI64`, `CreateSIToFP` vb.); dokunulmaz.
- Variadic paketleme — ayrı kod yolu, roadmap 2.3'te izlemede.
- `push`/eleman ataması — dizi eleman tiplemesi turunda ele alındı.
- Üye alan ataması (`o.f = x`) ve çok-seviyeli hedefler.

## Mimari

Dizi eleman tiplemesinde kurulan modelin aynısı, iki yeni yüzeye taşınıyor.

### Katman 1 — Sema

#### 1.1 Uyum kuralını genelleştir

`TypeChecker::checkArrayElement(target, elem)` bugün dizi elemanına özgü
adlandırılmış ama gövdesi tamamen genel: değer-koruyan sayısal dönüşüm her
ifade için uygun, kayıplı dönüşüm yalnız literalde ve değeri sığıyorsa,
`bool` sayısal değil, yargılanamayan hedeflerde (`Named`/`Generic`/
`Inferred`/`AssociatedType`/`DynProtocol`) ve `resolvedType`'ı olmayan
ifadelerde sessiz.

Bu gövde `checkAssignable(const TypeRepr *target, const Expr *value)` adıyla
yeniden adlandırılır; `checkArrayElement` kaldırılır ve çağrıları
`checkAssignable`'a yönlendirilir. **Davranış değişmez** — yalnız ad ve
kullanım alanı genişler. Dönüş tipi `ElemAssign` de `Assignability` olarak
yeniden adlandırılır (`Ok`, `Mismatch`, `LiteralOutOfRange`).

#### 1.2 Argüman denetimi

`TypeChecker::checkCallArgTypes(CallExpr *node)` eklenir ve
`checkCallArgCount`'un hemen ardından, aynı çağrı sitelerinden çağrılır.

Yalnız çağrılan **kullanıcı fonksiyonunun bildirimi çözülebildiğinde**
çalışır (`Symbol::Kind::Function` + `funcDecl`, ya da sınıf init'i).
Çözülemezse (builtin, closure değişkeni, dinamik dispatch) **sessiz kalır**.

Her argüman için `checkAssignable(param.type, arg)`:
- `Ok` → devam
- `Mismatch` → `err_arg_type_mismatch` (argümanın kendi konumunda)
- `LiteralOutOfRange` → `err_arg_literal_range`

`self` parametresi atlanır. Varsayılan değerli eksik argümanlar atlanır.
Variadic parametreli fonksiyonlarda **variadic parametreye denk gelen ve
sonrasındaki argümanlar denetlenmez** (paketleme ayrı kod yolu).

#### 1.3 Atama denetimi

`visitAssignExpr` bugün yalnız mutability denetliyor. Hedef bir
`IdentifierExpr` ve sembolün bildirilmiş bir tipi varsa
`checkAssignable(sym->type, node->getValue())` çalıştırılır:
- `Mismatch` → `err_assign_type_mismatch`
- `LiteralOutOfRange` → `err_assign_literal_range`

Hedef identifier değilse veya sembolün tipi yoksa sessiz kalınır.

#### 1.4 Yeni diagnostikler

```
DIAG(err_arg_type_mismatch, error, "argument of type '%0' cannot be passed to parameter of type '%1'")
DIAG(err_arg_literal_range, error, "literal %0 does not fit in parameter type '%1'")
DIAG(err_assign_type_mismatch, error, "cannot assign a value of type '%0' to a variable of type '%1'")
DIAG(err_assign_literal_range, error, "literal %0 does not fit in type '%1'")
```

Konumlar: argüman denetiminde **argümanın kendi** `getStartLoc()`'u;
atamada **değerin** `getStartLoc()`'u.

### Katman 2 — IRGen

#### 2.1 Argüman boğaz noktası

Argüman geçişi 20'den fazla ayrı döngüde yapılıyor. Site sayımına güvenmek
yerine dönüşüm **çağrının kendisinde**, callee'nin `llvm::FunctionType`'ına
göre yapılır — verifier'ın şikâyet ettiği otorite zaten odur.

```cpp
/// Coerce each argument to the callee's declared parameter type. The
/// function's own signature is the authority — it is what the verifier
/// checks — so this works regardless of which of the many argument-
/// collection loops produced the vector. Extra arguments beyond the
/// declared parameters (variadic packing) are left untouched.
/// `argTypes` carries each argument's static Liva type where the caller
/// could supply it, for signedness; a short or null-filled vector simply
/// means "assume signed", which is today's behaviour.
void coerceCallArgs(llvm::FunctionType *fnTy,
                    std::vector<llvm::Value *> &args,
                    const std::vector<const TypeRepr *> &argTypes = {});
```

Gövde: `i` için `i < fnTy->getNumParams()` ise
`args[i] = coerceToElemType(args[i], fnTy->getParamType(i), isUnsignedTypeRepr(argTypes[i]))`;
`coerceToElemType` `nullptr` dönerse argüman **dokunulmadan bırakılır**
(Sema zaten reddetmiş olmalı; verifier bugünkü gibi yakalar).

Uygulama siteleri — kullanıcı fonksiyonu/metodu çağıran `CreateCall`'lar:
`IRGenCall.cpp:151`, `:258`, `:334`, `:410` ve `IRGenCallMethod.cpp`'deki
metod-dispatch `CreateCall`'ları. Her birinde `CreateCall`'dan hemen önce
`coerceCallArgs(callee->getFunctionType(), args, argTypes)`.

`argTypes` yalnız AST'nin elde olduğu sitelerde doldurulur; diğerlerinde
boş geçilir.

#### 2.2 Atama dönüşümü

`IRGen::visitAssignExpr`'in **identifier hedefli skaler** dalında, store
öncesi `coerceToElemType(val, alloca->getAllocatedType(), srcUnsigned)`.
Dizi/optional gibi bileşik hedeflerde tipler zaten aynı LLVM tipine
düşer, dolayısıyla `coerceToElemType` erken döner — Sema tarafı bu vakayı
yakalar.

## Riskler

1. **Argüman denetimi dildeki her çağrıyı etkiler.** Bir yanlış pozitif
   stdlib'i veya süiti kırar. Bu yüzden denetim yalnız bildirimi
   çözülebilen çağrılarda çalışır ve yargılanamayan tiplerde susar.
2. **Boğaz noktası işaretlilik bilgisini kaybedebilir.** `argTypes`
   sağlanamayan sitelerde işaretli varsayılır — bugünkü davranış,
   gerileme değil.
3. **`checkArrayElement` → `checkAssignable` yeniden adlandırması** mevcut
   çağrıları kırabilir; mekanik ama dikkat ister.
4. **Verifier arka-durdurucusunun kaybı**: bugün verifier'ın yakaladığı
   skaler uyumsuzluklar artık dönüştürülüp geçirilecek. Sema denetimi bu
   yüzden **aynı turda** girmelidir; yalnız IRGen tarafı girerse gerçek
   uyumsuzluklar sessizce dönüştürülür.

## Test Stratejisi

**SemaTest:**
- `f(mkStr())` — `[i32]` parametreye `[string]` → `err_arg_type_mismatch`
- `f(5)` — `i64` parametre, int literal → hata YOK
- `f(3)` — `f64` parametre, int literal → hata YOK
- `f(200)` — `u8` parametre → hata YOK; `f(300)` → `err_arg_literal_range`
- `f(n)` — `u8` parametre, `n: i32` değişken → `err_arg_type_mismatch`
- `a = mkStr()` — `[i32]` değişken → `err_assign_type_mismatch`
- `a = 5` — `a: i32` → hata YOK
- Builtin çağrısı (`println("x")`, `strLen(s)`) → hata YOK
- Closure değişkeni üzerinden çağrı → hata YOK
- Generic fonksiyon çağrısı (`[T]` parametre) → hata YOK

**RuntimeExecTest:**
- `takeI64(5)`, `takeF64(3)`, `takeU8(200)` artık **derlenip doğru
  değeri** döndürüyor
- `var n: i64 = mkI64(); n = mkI64()` — atama doğru
- Eşleşen tiplerde gerileme yok (i32, string, `[i32]`, struct, metod
  çağrısı, sınıf init'i)

**Gerileme kapısı:** tam seri süit (`ctest --test-dir build-clang`),
2559 testin tamamı yeşil kalmalı. Argüman denetimi dildeki her çağrıyı
gördüğü için asıl kapı budur.
