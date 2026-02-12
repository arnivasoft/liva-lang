# Liva-Lang Derleyici - Proje Plani

## Genel Bakis

**Liva**, Swift benzeri sozdizimi ve Rust tarzi ownership/borrowing semantigine sahip bir programlama dili. C++20 ile yazilmis derleyici LLVM 21 backend kullaniyor.

- **Platform:** Windows, LLVM Clang 21 (C:\LLVM, MSVC ABI), MinGW GCC 15.2.0 (testler)
- **Build:** CMake, GoogleTest
- **Test:** 474/474 gecen test (lexer:34, parser:82, sema:310, type:12, ownership:9, projectconfig:27)

---

## Mimari

```
kaynak kod (.liva)
    |
    v
[Lexer] --> Token stream
    |         liva_lexer
    v
[Parser] --> AST (Abstract Syntax Tree)
    |          liva_parser
    v
[Sema] --> Type checking + Ownership checking + Lifetime analysis
    |        liva_sema
    v
[IRGen] --> LLVM IR
    |         liva_irgen
    v
[CodeGen] --> Native code (.exe)
               liva_codegen
```

### Kutuphane Bagimliliklari
- `liva_common` - SourceLocation, Diagnostics, DiagnosticKinds.def
- `liva_lexer` - Token, TokenKinds.def, Lexer (depends: liva_common)
- `liva_ast` - ASTNode, Expr, Stmt, Decl, Type, ASTVisitor, ASTPrinter (depends: liva_common)
- `liva_parser` - Parser (ParseDecl, ParseStmt, ParseExpr, ParseType) (depends: liva_lexer, liva_ast)
- `liva_sema` - Sema, TypeChecker, OwnershipChecker, LifetimeAnalysis, Scope, ModuleLoader (depends: liva_ast, liva_common)
- `liva_driver` - ProjectConfig, TOML parser, path utilities (depends: liva_common)
- `liva_irgen` - IRGen (6 dosya: IRGen, IRGenDecl, IRGenStmt, IRGenExpr, IRGenCall, IRGenMono) (depends: liva_ast, liva_sema, LLVM)
- `liva_codegen` - CodeGen, TargetInfo (depends: liva_irgen, LLVM)

### livac Komut Satiri
- `livac [options] <file>` - Tek dosya derleme (legacy mod)
- `livac build [--release|--debug] [-o <file>]` - liva.toml'dan proje derleme
- `livac run [--release|--debug]` - Derle + calistir
- `livac init [name]` - Yeni Liva projesi olustur
- `livac --dump-tokens <file>` - Token listesini goster
- `livac --dump-ast <file>` - AST agacini goster
- `livac --check-only <file>` - Sadece sema analizi
- `livac --emit-ir <file>` - LLVM IR ciktisi
- `livac -o <output> <file>` - Derle
- `livac -O0/-O1/-O2/-O3 <file>` - Optimizasyon seviyesi
- `livac -g <file>` - Debug bilgisi uret
- `livac --debug <file>` - Debug build (O0 + debug info)
- `livac --release <file>` - Release build (O2, debug info kapali)

---

## Tamamlanan Milestone'lar

### M0: Proje Iskeleti [TAMAMLANDI]
- CMake build sistemi kurulumu
- MinGW + MSVC + Clang triple-build destegi
- GoogleTest entegrasyonu
- Dizin yapisi: include/, src/, tests/, examples/

### M1: Lexer [TAMAMLANDI] - 34 test
- 103 token turu (anahtar kelimeler, operatorler, literaller, noktalamalar)
- Tam token listesi:
  - **Ozel:** `eof`, `identifier`, `integer_literal`, `float_literal`, `string_literal`, `char_literal`, `bool_literal`, `newline`, `string_interp_begin`, `string_interp_mid`, `string_interp_end`
  - **Anahtar kelimeler (32):** `func`, `struct`, `enum`, `impl`, `protocol`, `import`, `case`, `let`, `var`, `const`, `if`, `else`, `while`, `for`, `in`, `break`, `continue`, `return`, `match`, `as`, `pub`, `self`, `ref`, `mut`, `true`, `false`, `nil`, `where`, `async`, `await`, `try`, `type`
  - **Tip anahtar kelimeleri (13):** `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64`, `bool`, `string`, `void`
  - **Operatorler & Noktalamalar (47):** `( ) { } [ ] , ; : . .. ... -> => :: + - * / % == != < <= > >= && || ! & | ^ ~ << >> = += -= *= /= %= @ # ? ?? ?. _`
- Hex (0x), binary (0b), octal (0o) sayi literal destegi
- Satir ve blok yorum destegi
- String interpolasyon tokenleri (`\(expr)`)
- Kaynak konum takibi (satir, sutun)

### M2: Parser + AST [TAMAMLANDI] - 53 test
- 40+ AST dugum turu (9 Decl, 9 Stmt, 22 Expr)
- Pratt parser (oncelik tirmanmasi) ile ifade ayristirma
- CRTP-tabanli ASTVisitor deseni

**Bildirimler (Decl):**
| Dugum | Sozdizimi | Durum |
|-------|----------|-------|
| FuncDecl | `func name<T>(params) -> Type { body }` | Tam (generic dahil) |
| VarDecl | `let x: i32 = 42`, `var y = 10` | Tam |
| StructDecl | `struct Box<T> { var value: T }` | Tam (generic dahil) |
| FieldDecl | Struct alanlari | Tam |
| EnumDecl | `enum Color { case Red; case Green }` | Tam |
| EnumCaseDecl | `case Circle(f64)` - iliskili tipler | Tam |
| ImplDecl | `impl<T> Box<T> { func get(self) -> T { ... } }` | Tam (generic dahil) |
| ProtocolDecl | `protocol Drawable { type Item; func draw(self) }` | Tam (static + dynamic dispatch, defaults, associated types) |
| ImportDecl | `import foo::bar` | Tam (sema + codegen) |

**Ifadeler (Stmt):**
| Dugum | Sozdizimi | Durum |
|-------|----------|-------|
| ExprStmt | `expr` | Tam |
| ReturnStmt | `return expr` | Tam |
| IfStmt | `if cond { ... } else { ... }` | Tam |
| IfLetStmt | `if let x = optional { ... }` | Tam |
| WhileStmt | `while cond { ... }` | Tam |
| ForStmt | `for i in 0..10 { ... }` | Tam |
| BlockStmt | `{ stmts... }` | Tam |
| BreakStmt | `break` | Tam |
| ContinueStmt | `continue` | Tam |

**Ifadeler (Expr):**
| Dugum | Sozdizimi | Codegen |
|-------|----------|---------|
| IntegerLiteralExpr | `42`, `0xFF` | Tam |
| FloatLiteralExpr | `3.14` | Tam |
| BoolLiteralExpr | `true`, `false` | Tam |
| StringLiteralExpr | `"hello"` | Tam |
| NilLiteralExpr | `nil` | Tam |
| IdentifierExpr | `foo` | Tam |
| BinaryExpr | `a + b`, `x == y`, `&&`, bit ops | Tam |
| UnaryExpr | `-x`, `!b`, `~bits` | Tam |
| CallExpr | `foo(a, b)`, `f(x)` indirect call | Tam |
| MemberExpr | `obj.field`, `obj?.field`, `Color.Red` | Tam |
| IndexExpr | `arr[i]` | Tam |
| AssignExpr | `x = 5`, `x += 1` | Tam |
| StructLiteralExpr | `Point { x: 1.0, y: 2.0 }` | Tam |
| MatchExpr | `match s { ... }` | Tam |
| ArrayLiteralExpr | `[1, 2, 3]` | Tam |
| CastExpr | `expr as Type` | Tam |
| RefExpr | `ref x`, `ref mut x` | Tam |
| GroupExpr | `(expr)` | Tam |
| RangeExpr | `0..10` | Tam (for-in icinde) |
| UnwrapExpr | `x!` | Tam |
| ClosureExpr | `\|x: i32\| -> i32 { return x * 2 }` | Tam (capture, inference, trailing) |
| TryExpr | `try riskyFunc()` | Tam |

### M3: Tip Kontrolu (Kismi) [TAMAMLANDI] - 12 tip test + 107 sema test
- Tip sistemi: 13 TypeRepr cesidi
  - Primitifler: Void, Bool, I8/I16/I32/I64, U8/U16/U32/U64, F32/F64, String
  - Bilesenler: Named, Array, Reference, Optional, Function, Generic, Inferred
- Iki-gecisli tip kontrolu (once bildirimleri kaydet, sonra govdeleri kontrol et)
- Scope yonetimi (ic ice kapsamlar)
- Struct/enum tip kaydi
- Member access tip kontrolu
- Binary/unary operator tip dogrulamasi
- Kontrol akisi tip kontrolu (kosullar bool olmali)
- Built-in fonksiyonlar: `print`, `println`, `len`, `toString`
- Match arm binding'leri scope'a ekleniyor
- Generic fonksiyon/struct tip parametresi cikarimi
- Function-typed variable destegiyle indirect call tip kontrolu

### M4: LLVM IR Uretimi [TAMAMLANDI]
- Tum primitif tipler icin LLVM tip eslestirmesi
- Fonksiyon codegen (parametreler, donus tipleri, C ABI)
- Degisken bildirimleri (alloca, store, load)
- Aritmetik/karsilastirma/mantiksal/bitwise islemler (int + float)
- Kontrol akisi (if/else, while, for-in, break, continue)
- Printf tabanli I/O (`print`/`println`)
- Tip donusumleri (cast: int<->float, int<->int)

### M5: Ownership Kontrolu [TAMAMLANDI] - 9 test
- Sahiplik durumu makinesi: Owned -> Moved/Borrowed -> Dropped
- Move semantigi takibi
- Immutable/mutable borrow kontrolu
- Use-after-move tespiti
- Immutable degiskene atama tespiti
- Borrow catismasi tespiti
- Scope-based lifetime analysis (borrow-outlives checking)
- **Sinirlamalar:**
  - Copy trait tespiti basit (sadece primitifler)

### M6: Enum Codegen + Match [TAMAMLANDI]
- Basit enum'lar i32 tag olarak: `Color.Red -> 0`, `Color.Green -> 1`
- Match expression: LLVM switch instruction
- Wildcard (`_`), enum pattern, integer pattern destegi
- `enumCases_` map ve `varEnumTypes_` degisken takibi

### M6b: Associated Values (Tagged Union) [TAMAMLANDI]
- Payload enum'lar tagged union olarak: `{ i32 tag, [N x i8] payload }`
- `Shape.Circle(3.14)` -> tag=0 + f64 payload store
- `Shape.Rectangle(10.0, 20.0)` -> tag=1 + 2x f64 payload
- `Shape.Empty` -> tag=2, payload bos
- Match binding extraction: `Shape.Circle(r) =>` payload'dan r yukleme
- DataLayout ile payload boyutu hesaplama

### M7a: Array Codegen [TAMAMLANDI]
- Sabit boyutlu dizi: `let arr = [1, 2, 3]` -> LLVM `[3 x i32]`
- Index erisimi: `arr[i]` -> GEP + load
- `varArrayTypes_` ile eleman tipi ve boyut takibi

### M7b: String Islemleri [TAMAMLANDI]
- String birlestirme: `"hello" + " world"` -> `liva_str_concat`
- String karsilastirma: `s1 == s2` -> `liva_str_equal`
- String interpolasyon: `"x = \(value)"` -> parser desugaring ile concat chain
- String uzunluk: `len(s)` -> `liva_str_length`, `s.length` -> member access
- Runtime fonksiyonlari: `liva_i32_to_str`, `liva_f64_to_str`, `liva_bool_to_str`

### M8a: Generic Fonksiyonlar [TAMAMLANDI]
- `func identity<T>(x: T) -> T { return x }`
- Tip parametresi cikarimi (LLVM tip eslemesiyle)
- Monomorfizasyon: her somut tip icin ayri fonksiyon uretimi
- `identity(42)` -> `identity_i32` otomatik uretim
- Coklu tip parametresi destegi: `func pair<T, U>(a: T, b: U)`

### M8b: Generic Struct'lar [TAMAMLANDI]
- `struct Box<T> { var value: T }` -> `Box_i32`, `Box_f64` monomorfizasyonu
- Generic struct literal: `Box { value: 42 }` -> tip cikarimi ile `Box_i32`
- `structTypeArgs_` ile monomorfize edilmis struct tip argumanlari takibi
- Coklu tip parametresi: `struct Pair<T, U> { var first: T; var second: U }`

### M9: Match Exhaustiveness [TAMAMLANDI]
- Enum match ifadelerinde tum durumlarin kapsandigini kontrol
- Eksik case hatasi: `err_nonexhaustive_match`
- Gereksiz arm uyarisi: `warn_unreachable_match_arm`
- Wildcard (`_`) ile eksik durumlarin karsilanmasi

### M10: Dinamik Diziler [TAMAMLANDI]
- `var arr: [i32] = [1, 2, 3]` -> heap-allocated dinamik dizi
- DynArray struct: `{ ptr, i64 len, i64 cap }`
- `arr.push(val)` -> `liva_array_push` (otomatik kapasite artirimi)
- `arr.pop()` -> `liva_array_pop`
- Index erisimi: `arr[i]` -> bounds checking ile
- Runtime: `liva_array_new`, `liva_array_free`, `liva_array_push`, `liva_array_pop`

### M11: Generic Metodlar + Bounds Checking [TAMAMLANDI]
- `impl<T> Box<T> { func get(self) -> T { return self.value } }`
- `monomorphizeMethod()` ile generic impl bloklarindan method uretimi
- Sabit dizi ve dinamik dizi icin runtime bounds checking
- `liva_panic` ile sinir asimi hatasi (noreturn)

### M12: Optional Tipler [TAMAMLANDI]
- `let x: i32? = nil` / `let x: i32? = 42` -> optional degiskenler
- `x!` -> force unwrap (nil ise `liva_panic`)
- LLVM temsili: `{ i1 hasValue, T value }` struct
- `err_nil_without_optional` diagnostigi

### M13: Closures / Lambda Expressions [TAMAMLANDI]
- `|x: i32| -> i32 { return x * 2 }` -> closure literal
- `|| { println("hello") }` -> parametresiz closure
- `let f: (i32) -> i32 = closure` -> function-typed variable
- `func apply(f: (i32) -> i32, x: i32) -> i32` -> higher-order function
- Indirect call: `f(5)` where `f` is a function pointer
- LLVM: closure -> internal function, `varFuncTypes_` ile indirect call
- AST: ClosureExpr dugumu (explicit destructor for unique_ptr<BlockStmt>)

### M14a: Trait/Protocol Sistemi (Static) [TAMAMLANDI]
- `protocol Printable { func toString(self) -> string }` -> protocol tanimlama
- `impl Printable for Point { ... }` -> protocol uygulama
- Static dispatch: metod cagrilari derleme zamaninda cozulur
- `protocolConformances_` map ile conformance takibi
- `err_undefined_protocol`, `err_missing_protocol_method`, `err_no_conformance` diagnostikleri

### M14b: Dynamic Dispatch (vtable) [TAMAMLANDI]
- Trait object: `let p: ref Printable = point` -> fat pointer
- Fat pointer: `{ data_ptr, vtable_ptr }` (`traitObjectTy_`)
- Vtable olusturma: `getOrCreateVtable()` ile global vtable
- Member dispatch: vtable'dan method pointer cekip indirect call
- `protocolMethodNames_`, `protocolMethodIndices_` ile method siralama

### M14c: Protocol Default Implementations [TAMAMLANDI]
- Protocol metotlarinda varsayilan govde: `func shout(self) -> string { ... }`
- Sema: `visitImplDecl` eksik metotlarda default body varsa hata vermez
- IRGen: impl'de override edilmeyen default metotlari otomatik uretir
- `protocolDecls_` map ile default metot govdesi arama

### M15a: Optional Gelistirmeleri [TAMAMLANDI]
- Optional binding: `if let value = x { ... }` -> IfLetStmt
- Nil coalescing: `x ?? defaultValue` -> `emitNilCoalesce()`
- LLVM: hasValue kontrolu ile dallanma, value extraction

### M15b: Result Type ve Error Handling [TAMAMLANDI]
- `Result<i32, string>` tipi -> `{ i8 tag, max(ok,err) payload }` struct
- `try riskyFunc()` -> TryExpr, otomatik error propagation
- `return .Ok(value)` / `return .Err(msg)` -> Result construction
- Match ile Result decomposition: `match result { .Ok(v) => ..., .Err(e) => ... }`
- `err_try_on_non_result` diagnostigi

### M16a: Closure Variable Capture [TAMAMLANDI]
- Closure disindaki degiskenleri yakalama (capture by value)
- Closure object: `{ ptr func_ptr, ptr env_ptr }` (`closureObjTy_`)
- Non-capturing: `env_ptr = null`; Capturing: stack-allocated env struct
- `collectFreeVars()` AST walker ile serbest degiskenleri tespit
- Tum closure fonksiyonlari gizli `ptr %env` parametresi alir

### M16b: Closure Type Inference [TAMAMLANDI]
- `|x| -> i32 { return x * 2 }` -> parametre tipi baglamdan cikarilir
- `cloneTypeRepr()` ile FunctionTypeRepr derin klonu
- `visitVarDecl` ve `visitCallExpr` ile tip yayilimi

### M16c: Trailing Closure Syntax [TAMAMLANDI]
- `apply(5) |x| { return x * 2 }` -> trailing closure sozdizimi
- `parsePostfixExpr` pipe sonrasi closure'u son arguman olarak ekler
- `CallExpr::addArg()` ile trailing closure ekleme

### M17: Modul Sistemi [TAMAMLANDI]
- `import math` -> dosya arama, parse, type-check, export
- `pub` erisim belirleyicisi: sadece pub semboller disari aktarilir
- ModuleLoader: dosya cache, dairesel import tespiti, `registerSource()` (testler icin)
- Sema: ImportDecl -> `loadModule()` -> exported symbol'leri scope'a kaydet
- IRGen: `visitImportDecl` modul TU'sunu ana LLVM Module'e inline eder
- `processedModules_` set ile tekrar isleme onlenir
- `err_module_not_found`, `err_circular_import` diagnostikleri

### M18: Trait Bounds (Sema-Level) [TAMAMLANDI]
- `func print<T: Printable>(x: T)` -> generic kisitlamalari
- `<T: Protocol>` sozdizimi (FuncDecl, StructDecl, ImplDecl)
- `typeParamBounds_` map: paramName -> protocolName
- Sema: bound isim dogrulama, cagri noktalarinda conformance kontrolu
- Codegen: monomorphization zaten calistigindan ek degisiklik gerekmez

### M19: Teknik Borc Temizligi [TAMAMLANDI]
- **Where clause destegi:** `func show<T>(item: T) where T: Printable { ... }`
  - Parser: `parseFuncDecl`, `parseStructDecl`, `parseImplDecl`'e where clause eklendi
  - Inline `<T: P>` ve `where T: P` ayni `typeParamBounds` map'ine yazilir (son yazar kazanir)
  - 5 parser testi, 3 sema testi
- **RefExpr sema tip propagasyonu:** `visitRefExpr` artik `ReferenceTypeRepr` olusturup `setResolvedType` cagiriyor
  - 1 sema testi

### M20: RefExpr Codegen [TAMAMLANDI]
- `ref x` / `ref mut x` icin LLVM IR uretimi
- `toLLVMType`: Reference -> ptr (opaque pointer)
- `varRefTypes_` map: ref degiskenlerin ic LLVM tipini takip eder
- `visitFuncDecl`: ref parametreler ptr olarak gecirilir, `varRefTypes_` kaydi + scope save/restore
- `visitIdentifierExpr`: ref degiskenler icin double-load (ptr yukle, ptr uzerinden deger yukle)
- `visitRefExpr`: normal degiskenin alloca adresini dondurur, ref degiskende ptr'yi yukler (pass-through)
- `visitAssignExpr`: ref mut atamasi pointer uzerinden store (compound assignment dahil)
- 3 yeni sema testi: RefParamFunction, RefMutParamFunction, RefPassThrough

### M21: Optional Chaining [TAMAMLANDI]
- `obj?.field` sozdizimi: Optional nesne nil ise nil doner, deger varsa alana erisir
- Lexer: `question_dot` token (`?.`) — `??` sonrasi, `?` oncesi
- AST: `MemberExpr`'e `isOptionalChain_` flag eklendi
- Parser: `parsePostfixExpr`'de `?.` -> `parseMemberExpr(base, true)`
- Sema: `visitMemberExpr` sonunda optional chain -> resolved type'i Optional'a sar
- IRGen: `emitOptionalChainMember` — nil check -> unwrap struct -> GEP field -> rewrap Optional
- IRGen: VarDecl optional codegen'de `varStructTypes_` kaydi (inner type Named ise)
- IRGen: `emitNilCoalesce` — optional chain LHS destegi (MemberExpr isOptionalChain)
- ASTPrinter: `?.` vs `.` gosterimi
- 2 lexer, 3 parser, 2 sema testi

---

## Tamamlanan Ek Milestone'lar

### M22c: Math Built-ins [TAMAMLANDI]
- `abs`, `min`, `max`: ayni tip donusu (int/float)
- `sqrt`, `pow`, `floor`, `ceil`, `log`, `log10`, `sin`, `cos`, `tan`, `round`: f64 donusu
- Sema: registerBuiltins + visitCallExpr tip cozumlemesi
- IRGen: LLVM Intrinsic kullanimi, otomatik SIToFP donusumu
- `round(x, d)`: cok basamakli yuvarlama destegi

### M20a: Map/Set Collections [TAMAMLANDI]
- `Map<K, V>` — FNV-1a hash, open-addressing linear probing, tombstone deletion
- `Set<T>` — Map ile val_size=0 olarak uygulanir
- Runtime: `liva_map_new/insert/get/contains/remove`, `liva_set_new/insert/contains/remove`
- Entry layout: [1B state][8B hash][key_size key][val_size value]
- Load factor > 0.75 -> 2x rehash
- LLVM struct: `{ ptr entries, i64 size, i64 capacity }`
- Sema: GenericTypeRepr, method type resolution
- IRGen: visitVarDecl alloca + init, visitCallExpr method codegen
- 10 sema testi

### M20b: I/O System [TAMAMLANDI]
- `File.open(path, mode)` -> `File?` (Optional), `file.readLine()` -> `string?`
- `file.readAll()` -> `string`, `file.write(s)`, `file.writeLine(s)`, `file.close()`
- `readLine()` -> stdin'den satir okuma -> `string`
- `format("x = {}, y = {}", x, y)` -> string formatlama ({} placeholders)
- Runtime: `liva_file_open/close/read_line/read_all/write/write_line`, `liva_read_line`, `liva_i64_to_str`
- Sema: `fileVariables_` set, registerBuiltins, visitCallExpr/visitIfLetStmt
- IRGen: `varFileTypes_`, `varFileOptionalTypes_`, File.open -> Optional<ptr>, if-let unwrap
- 10 sema testi

### M23: For-in Koleksiyonlari [TAMAMLANDI]
- `for item in array { ... }` — dinamik dizi iterasyonu (idx loop over data ptr)
- `for key in map { ... }` — Map key iterasyonu (capacity loop, state==1 check)
- `for (key, value) in map { ... }` — Map tuple iterasyonu (tuple destructuring)
- `for item in set { ... }` — Set iterasyonu (Map ile ayni, val_size=0)
- AST: ForStmt varName2_, hasTuplePattern(), yeni constructor
- Parser: `(k, v)` tuple pattern ayristirma
- Sema: visitForStmt tip cikarimi ([T]->T, Map<K,V>->K/K+V, Set<T>->T)
- IRGen: DynArray/Map/Set inline iterasyon codegen (condBB/bodyBB/latchBB/exitBB)
- `err_tuple_for_requires_map` diagnostigi
- 2 parser, 8 sema testi

### M24: String Metodlari [TAMAMLANDI]
- `s.contains("sub")` -> bool, `s.startsWith("pre")` -> bool, `s.endsWith("suf")` -> bool
- `s.indexOf("sub")` -> i64 (-1 if not found)
- `s.substring(start, length)` -> string, `s.trim()` -> string
- `s.toUpper()` -> string, `s.toLower()` -> string
- `s.replace("old", "new")` -> string
- `s.split(",")` -> `[string]` (DynArray<string>) codegen tamamlandi
- Runtime: liva_str_contains, liva_str_starts_with, liva_str_ends_with, liva_str_index_of, liva_str_substring, liva_str_trim, liva_str_to_upper, liva_str_to_lower, liva_str_replace, liva_str_split
- Sema: visitCallExpr MemberExpr string type method resolution
- IRGen: string method codegen (i8 -> i1 trunc for bool, auto i32 -> i64 sext for substring)
- 8 sema testi

### M22d: Type Conversion Built-ins [TAMAMLANDI]
- `parseInt(str)` -> `i32?`, `parseInt64(str)` -> `i64?`, `parseFloat(str)` -> `f64?`
- Runtime: liva_str_parse_i32, liva_str_parse_i64, liva_str_parse_f64 (C strtol/strtoll/strtod)
- Success → Optional(value), failure → nil
- Sema: registerBuiltins, visitCallExpr Optional return type
- IRGen: runtime call → tmp alloca → Optional struct wrap
- 4 sema testi (ParseInt, ParseInt64, ParseFloat, ParseIntIfLet)

### M25: Stdlib Enhancements [TAMAMLANDI]
- **String.split() Codegen**: `s.split(",")` → DynArray struct {ptr, i64, i64}
  - Sema: split() → ArrayTypeRepr(dynamic, string) resolved type
  - IRGen: liva_str_split(str, delim, &count) → DynArray struct wrap
- **DynArray Methods**: `arr.contains(val)`, `arr.indexOf(val)`, `arr.reverse()`
  - Runtime: liva_array_contains (memcmp/strcmp), liva_array_index_of, liva_array_reverse
  - Sema: visitCallExpr DynArray type → contains→bool, indexOf→i64, reverse→void
  - IRGen: load data+len, elem alloca, runtime call, i8→i1 trunc for bool
- **DynArray Properties**: `arr.length` → i64, `arr.isEmpty` → bool
  - Sema: visitMemberExpr Array(dynamic) type detection
  - IRGen: already existed (GEP field 1 / icmp eq 0)
- 6 sema testi (StringSplit, DynArrayContains, DynArrayIndexOf, DynArrayReverse, DynArrayLengthType, DynArrayIsEmptyType)

### M26: Higher-Order Array Methods [TAMAMLANDI]
- **forEach/map/filter**: `arr.forEach(|x| {...})`, `arr.map(|x| -> T {...})`, `arr.filter(|x| -> bool {...})`
  - Sema: closure param type inference from DynArray element type (untyped `|x|` params)
  - Sema: return types — forEach→void, map→ArrayTypeRepr(dynamic), filter→ArrayTypeRepr(dynamic)
  - IRGen: inline loop codegen calling closure via indirect function pointer
  - forEach: void loop, map: new array + store results, filter: new array + conditional copy
  - Fix: save elemType/elemSize/arrAlloca BEFORE visit(closure) — iterator invalidation
  - Fix: visitVarDecl init with dynamic Array resolved type → DynArray alloca + varDynArrayTypes_
  - Chaining: `let d = arr.map(...); d.forEach(...)` works correctly
- 5 sema testi (DynArrayForEach, DynArrayForEachInferred, DynArrayMap, DynArrayFilter, DynArrayFilterInferred)

### M27: Language Enhancements [TAMAMLANDI]
- **reduce()**: `arr.reduce(0, |acc, x| -> i32 { return acc + x })`
  - Sema: closure param 1 (x) type inference from element type, return type from init
  - IRGen: inline loop with accumulator alloca, 2-param closure call
- **Enum Methods**: `impl Color { func describe(self) -> string { ... } }`
  - IRGen: varEnumTypes_ lookup for method dispatch (in addition to varStructTypes_)
  - visitImplDecl: detect enum type → varEnumTypes_["self"] registration
- **while-let**: `while let x = optional { ... }`
  - AST: WhileLetStmt node (Stmt.h), Parser: while + let token detection
  - Sema: scope-based optional unwrap (not resolvedType — avoids cast crash)
  - IRGen: condBB(check hasVal)/bodyBB(unwrap+bind+body)/exitBB + loopStack_
- 7 test (parser:1, sema:6)

### M28: Practical Enhancements [TAMAMLANDI]
- **String Indexing**: `s[i]` → `liva_str_substring(s, i, 1)` ile tek karakter string
  - Sema: visitIndexExpr base string ise resolved type → String
  - IRGen: liva_str_length ile bounds check + liva_str_substring(s, i, 1)
- **Multi-arg println**: `println(a, b, c)` → arglar arasi bosluk, sonda newline
  - IRGen: visitCallExpr println/print loop over all args with space separator
- **Inferred type fix**: `let ch = s[0]` gibi annotation'siz var'larda init value tipi kullanilir
  - IRGen: visitVarDecl fallback init value'nun LLVM tipini kullanir (i32 default yerine)
- 4 test (sema:4)

### M29: Syntax & Convenience [TAMAMLANDI]
- **Multi-line String Literals**: `"""..."""` — triple-quote syntax
  - Lexer: lexString detect `"""` → scan until closing `"""`
  - Token.getStringValue(): strip 3 quotes from each end + optional leading newline
- **Array/String Slicing**: `arr[1..3]` ve `s[0..5]`
  - Parser: zaten `parseExpression()` RangeExpr olarak ayrıstirir
  - Sema: visitIndexExpr range+array → cloneTypeRepr, string slicing → string type
  - IRGen: DynArray → liva_array_new + memcpy, String → liva_str_substring(s, start, len)
- **Default Function Arguments**: `func greet(name: string = "World")`
  - AST: ParamDecl.defaultValue + hasDefault()
  - Parser: parseParamDecl `= expr` desteği
  - IRGen: funcDecls_ map, visitCallExpr missing args → default value codegen
- 7 test (lexer:2, parser:1, sema:4)

### M30: Ternary Expression & Type Aliases [TAMAMLANDI]
- **Ternary Expression**: `x > 5 ? "big" : "small"`
  - Lexer: `?` tokendan sonra `:` bağlamında ternary ayrıştırma
  - AST: TernaryExpr node (condition, thenExpr, elseExpr)
  - Parser: ternary en düşük öncelik, parseExpression() içinde (parsePrecedenceExpr değil)
  - Sema: visitTernaryExpr then-branch tipini propagate eder
  - IRGen: conditional branch + PHI node merge
- **Type Aliases**: `type Int = i32`, `type Pos = Point`
  - Lexer: `kw_type` keyword
  - AST: TypeAliasDecl (name, targetType, isPublic), NodeKind::TypeAliasDecl
  - Parser: parseTypeAliasDecl — `type Name = Type`
  - Scope: Symbol::Kind::TypeAlias + aliasTarget field
  - Sema: typeAliases_ map, resolveAlias() helper, typesCompatible alias resolution
  - Sema: visitStructLiteralExpr alias→struct resolution
  - Sema: visitCallExpr return type alias resolution (ownership checker uyumluluğu)
  - IRGen: typeAliases_ map, toLLVMType alias→target delegation
- 6 test (parser:2, sema:4)

### M31: Tuple Types & Multi-Return [TAMAMLANDI]
- `(i32, string)` tuple tipi → TupleTypeRepr
- `(42, "hello")` tuple literal → TupleLiteralExpr
- `func divmod(a: i32, b: i32) -> (i32, i32)` multi-return
- `pair.0`, `pair.1` tuple element erisimi (MemberExpr integer member)
- `let (x, y) = divmod(10, 3)` tuple destructuring (VarDecl.destructuredNames_)
- IRGen: Tuple = LLVM anonymous StructType, alloca+GEP pattern
- varTupleTypes_ map ile tuple degisken takibi
- 4 parser, 4 sema testi

### M32: Closure Capture by Reference [TAMAMLANDI]
- Mutate edilen dis degiskenler otomatik referansla yakalanir
- CapturedVar struct: {name, byRef} — byRef true if assigned in closure body
- collectFreeVarsImpl: mutatedVars set tracks AssignExpr targets
- By-ref env field = ptr (outer alloca address), by-value = value copy (unchanged)
- Closure body: by-ref → load ptr from env, register in varRefTypes_ → double-load/store-through
- varRefTypes_ save/clear/restore added to visitClosureExpr
- Sema/Parser/Lexer degisikligi yok — mevcut immutability check yeterli
- 4 sema testi

### M33: Ownership + IRGen Integration [TAMAMLANDI]
- Fonksiyon cikisinda heap-tahsisli koleksiyonlar (DynArray, Map, Set) otomatik temizlenir
- `emitScopeCleanup()`: varDynArrayTypes_/varMapTypes_/varSetTypes_ uzerinden free cagrilari
- `movedVars_` set: fonksiyona gecirilen koleksiyon degiskenleri moved olarak isaretlenir (double-free onleme)
- visitReturnStmt: explicit ve implicit return'dan once cleanup
- visitCallExpr: koleksiyon argumanlari moved olarak isaretlenir
- visitAssignExpr: re-assignment moved flag'i siler (un-move)
- visitClosureExpr: movedVars_ save/clear/restore
- Kapsam: sadece fonksiyon seviyesi (block-level yok), File/String temizleme yok
- 4 sema testi

### TD1: IRGen.cpp Bolme [TAMAMLANDI]
- 5,542 satirlik monolitik IRGen.cpp dosyasi 6 dosyaya bolundu
- Tum metotlar ayni `IRGen` sinifinda kalir, sadece .cpp implementasyonlari ayrildi
- `src/IR/IRGen.cpp` — Core: constructor, generate(), createRuntimeDecls(), tip helper'lari (~500 satir)
- `src/IR/IRGenDecl.cpp` — Bildirim visitor'lari (~1600 satir)
- `src/IR/IRGenStmt.cpp` — Ifade visitor'lari + emitScopeCleanup (~570 satir)
- `src/IR/IRGenExpr.cpp` — Expression visitor'lari + CapturedVar/collectFreeVars + closures (~2000 satir)
- `src/IR/IRGenCall.cpp` — Call/member/match/assign visitor'lari (~1600 satir)
- `src/IR/IRGenMono.cpp` — Monomorfizasyon (~350 satir)

### TD2: LifetimeAnalysis Implementasyonu [TAMAMLANDI]
- Scope-tabanli borrow-outlives checking, Sema::analyze() Phase 3 olarak calisir
- Her degiskene tanimlandigi scope derinligini atar (VarInfo{scopeDepth, declLoc, refTarget})
- Referanslarin refer ettikleri degiskeni asip asmadigini kontrol eder
- `visitBlockStmt`: scope push/pop, `visitVarDecl`: ref init kontrolu
- `visitAssignExpr`: ref re-assignment scope kontrolu, `checkScopeExit`: blok cikisinda outlives kontrolu
- `err_borrow_outlives_value` diagnostigi
- 3 yeni ownership testi (BorrowOutlivesValueInnerScope, BorrowSameScope, BorrowOuterToInner)

### TD3: Associated Types [TAMAMLANDI]
- Protokollarda iliskili tip tanimlama: `protocol Container { type Item; func get(self) -> i32 }`
- Impl bloklarinda iliskili tip belirtme: `impl X: Container { type Item = i32; ... }`
- AST: ProtocolDecl.associatedTypes_ (vector<string>), ImplDecl.associatedTypes_ (map<string,string>)
- Parser: parseProtocolDecl'de `kw_type` → associatedTypes listesi, parseImplDecl'de `type Name = Type` → map
- Sema: protocolAssociatedTypes_ map, visitImplDecl'de eksik associated type kontrolu
- `err_missing_associated_type` diagnostigi
- 5 yeni sema testi

### M34: Async/Await Faz 1 [TAMAMLANDI] - 3 test
- `async func fetch() -> i32 { ... }` — async fonksiyon tanimlama
- `let result = await fetch()` — await ifadesi
- Senkron semantik (Task<T> = {i1, T}, gercek coroutine yok)
- Diagnostikler: `err_await_outside_async`, `err_async_main`

### M35: Derleme Zamani Degerlendirme [TAMAMLANDI] - 13 test
- `const PI: f64 = 3.14159` — derleme zamani sabit (tip anotasyonlu)
- `const MAX = 100` — tip cikarimli sabit
- Desteklenen sabit ifadeler: literaller, aritmetik/mantiksal/bitwise ops, ternary, const referanslari, cast
- Desteklenmeyen: fonksiyon cagrilari, closure, array/struct literal, runtime degisken referansi
- Diagnostikler: `err_const_requires_init`, `err_const_init_not_constant`
- IRGen: const degiskenler icin alloca yok, dogrudan llvm::Constant kullanimi

---

### TD4: Teknik Borc Temizligi [TAMAMLANDI]
- Deprecated LLVM API: 15x `getDeclaration` → `getOrInsertDeclaration` (IRGenCall.cpp)
- Test coverage: AllKeywords lexer testi, 5 local const sema testi
- plan.md sayisal tutarsizliklari duzeltildi (token/keyword/test sayilari)

### F3: Guard Clause (Match Arms) [TAMAMLANDI] - 9 test (1 parser, 8 sema)
- `case X where cond => ...` sozdizimi — match arm'larinda koruma ifadesi
- AST: MatchArm.guard (unique_ptr<Expr>) — optional where kosulu
- Parser: `parseMatchExpr` pattern sonrasi `where` keyword kontrolu → guard expression parse
- Sema: visitMatchExpr guard ifadesini visit eder (tip kontrolu)
- IRGen: guard false ise defaultBB'ye atla (condBr ile)
- Exhaustiveness: guarded arm'lar exhaustive sayilmaz (unguarded wildcard gerekli)

### F4: Drop Trait / Destructor [TAMAMLANDI] - 7 sema test
- `protocol Drop { func drop(mut self) }` — kullanici tanimli destructor
- Scope cikisinda otomatik `TypeName_drop()` cagrisi
- Parser: `mut self` parametre destegi (isSelf=true, isMutRef=true)
- Sema: Drop protocol validasyonu (method signature kontrolu)
- IRGen: emitScopeCleanup'ta Drop conformance kontrolu → destructor cagrisi
- `err_drop_method_signature` diagnostigi

### F5: Operator Overloading [TAMAMLANDI] - 9 sema test
- Protocol-tabanli operator overloading: Add/Sub/Mul/Div/Mod/Eq/Less
- `protocol Add { func add(self, other: Self) -> Self }` → `a + b` struct uzerinde
- Operator → protocol eslesmesi: `+`→Add, `-`→Sub, `*`→Mul, `/`→Div, `%`→Mod, `==`→Eq, `<`→Less
- Turetilmis operatorler: `!=` = NOT eq, `>=` = NOT less, `<=` = less OR eq, `>` = NOT(less OR eq)
- Sema: getOpProto() helper + visitBinaryExpr'de Named type dispatch
- IRGen: getOpMethodName() + struct method call dispatch + derived op codegen
- Primitive tipler etkilenmez (Kind::Named kontrolu ONCE yapilir)
- `err_binary_op_on_struct` diagnostigi (conformance yoksa)

### F6: Custom Iterator Protocol [TAMAMLANDI] - 7 sema test
- `protocol Iter { func next(mut self) -> T? }` — kullanici tanimli iterator
- `for item in obj { ... }` — Iter protokolunu uygulayan tipler icin for-in destegi
- Sema: iteratorElementTypes_ map ile element tipi takibi
- IRGen: Iter.next() cagrisi ile loop codegen (nil check ile cikis)
- Desteklenen tipler: struct'lar Iter protokolunu implemente ederek for-in'de kullanilabilir

### F7: Variadic Fonksiyonlar [TAMAMLANDI] - 10 test (1 lexer, 2 parser, 7 sema)
- `func sum(values: i32...) -> i32` — degisken sayida arguman destegi
- Lexer: `...` (ellipsis) token eklendi
- AST: ParamDecl.isVariadic flag
- Parser: parseParamDecl'de type'dan sonra `...` kontrolu
- Sema: variadic validasyon (son parametre olmali, birden fazla olamaz)
- Sema: variadic parametre scope'da `[T]` (DynArray) olarak kayit edilir
- IRGen: variadic param → getDynArrayStructTy() `{ptr, i64, i64}`
- IRGen: call site'da argUmanlar stack-allocated array'e yazilip DynArray struct olusturulur
- 0 arguman → bos DynArray, N arguman → N elemanli DynArray
- Diagnostikler: `err_variadic_not_last`, `err_multiple_variadic`

### F8: Nested Pattern Matching [TAMAMLANDI] - 6 test (1 parser, 5 sema)
- `case Outer.Some(Inner.Val(n)) =>` — ic ice enum pattern esleme
- Parser: degisiklik gereksiz (mevcut token birlestirme nested parenleri otomatik yakalar)
- Sema: extractPatternBindings() — depth-aware paren matching ile recursive leaf binding cikarimi
- IRGen: parseMatchPattern() — depth-aware paren esleme + top-level comma split + recursive sub-pattern
- IRGen: emitNestedPatternMatch() — ic enum tag kontrolu + ic payload binding cikarimi
- Arbitrary depth nesting desteklenir (A(B(C(x))) gibi)
- Payload ve simple (non-payload) enum'lar ic ice kullanilabilir

### P1: Proje Manifest + Paket Yoneticisi Altyapisi [TAMAMLANDI] - 27 test
- **liva.toml** proje konfigurasyonu — minimal TOML parser ile okuma
- TOML destegi: string, integer, boolean, string array, section, yorum, escape
- `livac build` — liva.toml oku, projeyi derle (cikti: `<name>.exe`)
- `livac run` — derle + calistir
- `livac init [name]` — yeni proje iskeleti olustur (liva.toml, src/main.liva, .gitignore)
- Coklu modul arama yollari: `[paths].modules` → ModuleLoader searchPaths_
- CLI override: `--release`/`--debug` flag'leri liva.toml degerlerini ust yazar
- `findProjectFile()` — CWD'den yukari dogru liva.toml arar
- Path utilities: joinPath, getDirectoryOf, getCurrentDirectory, createDirectories, fileExists
- `liva_driver` static kutuphane (bagimsiz test edilebilir)
- Mevcut `livac <dosya>` davranisi degismez (backward compatible)
- Yeni dosyalar: ProjectConfig.h, ProjectConfig.cpp, ProjectConfigTest.cpp
- Degisen dosyalar: Driver.h/cpp, CompilerInstance.h/cpp, ModuleLoader.h/cpp, CMakeLists.txt x2

---

## Diagnostik Envanterleri (69 tanimli)

### Lexer Hatalari (6)
- `err_unexpected_character`, `err_unterminated_string`, `err_unterminated_block_comment`
- `err_invalid_number_literal`, `err_invalid_escape_sequence`, `err_empty_char_literal`

### Parser Hatalari (11)
- `err_expected_token`, `err_expected_expression`, `err_expected_type`
- `err_expected_identifier`, `err_expected_declaration`, `err_expected_statement`
- `err_expected_func_body`, `err_expected_struct_body`, `err_expected_parameter_list`
- `err_unexpected_token`, `err_invalid_assignment_target`

### Sema - Isim Cozumleme (5)
- `err_undeclared_identifier`, `err_redefinition`, `err_undefined_type`
- `err_undefined_member`, `err_wrong_arg_count`

### Sema - Tip Kontrolu (8)
- `err_type_mismatch`, `err_binary_op_type`, `err_unary_op_type`
- `err_return_type_mismatch`, `err_condition_not_bool`, `err_cannot_infer_type`
- `err_void_variable`, `err_no_return`

### Sema - Ownership (9)
- `err_use_after_move`, `err_assign_to_immutable`, `err_mut_borrow_conflict`
- `err_immut_borrow_conflict`, `err_move_while_borrowed`, `err_borrow_outlives_value`
- `err_double_move`, `err_partial_move`, `err_mut_ref_to_immutable`

### Sema - Protocol/Trait (5)
- `err_undefined_protocol`, `err_missing_protocol_method`, `err_no_conformance`
- `err_missing_associated_type`, `err_binary_op_on_struct`

### Sema - Error Handling (1)
- `err_try_on_non_result`

### Sema - Module (2)
- `err_module_not_found`, `err_circular_import`

### Sema - For-in (1)
- `err_tuple_for_requires_map`

### Sema - Match (1)
- `err_nonexhaustive_match`

### Sema - Optional (1)
- `err_nil_without_optional`

### Sema - Async/Await (2)
- `err_await_outside_async`, `err_async_main`

### Sema - Const (2)
- `err_const_requires_init`, `err_const_init_not_constant`

### Sema - Drop (1)
- `err_drop_method_signature`

### Sema - Variadic (2)
- `err_variadic_not_last`, `err_multiple_variadic`

### Sema - Diger (3)
- `err_main_not_found`, `err_break_outside_loop`, `err_continue_outside_loop`

### Uyarilar (4)
- `warn_unused_variable`, `warn_unreachable_code`, `warn_shadowed_variable`
- `warn_unreachable_match_arm`

### Notlar (3)
- `note_previous_declaration`, `note_moved_here`, `note_borrowed_here`

---

## Test Envanteri

| Test Dosyasi | Sayi | Kapsam |
|-------------|------|--------|
| `tests/unit/LexerTest.cpp` | 34 | Token turleri, literaller, yorumlar, konum, string interpolasyon, optional chain, multi-line strings, hata yollari, bitwise tokenlar, ellipsis |
| `tests/unit/ParserTest.cpp` | 82 | Bildirimler, ifadeler, generics, optional, closure, protocol, import, trait bounds, where clause, optional chain, for-in collections, ternary, type aliases, tuples, async/await, const, multi-bound, guard clause, variadic, nested pattern, hata yollari |
| `tests/unit/SemaTest.cpp` | 310 | Struct, enum, match, string, generics, dyn array, optional, closure, protocol, result, module, trait bounds, where clause, ref expr, optional chain, math, map/set, I/O, for-in collections, string methods, type conversions, stdlib, higher-order, reduce/enum methods/while-let, practical, syntax, ternary, type aliases, tuples, capture-by-ref, ownership-cleanup, associated types, async/await, const, multi-bound, stdlib builtins, drop trait, guard clause, operator overloading, custom iterators, variadic functions, nested pattern matching |
| `tests/unit/TypeTest.cpp` | 12 | Tip uyumlulugu, donusum, bit genisligi |
| `tests/unit/OwnershipTest.cpp` | 9 | Move, borrow, use-after-move, lifetime analysis |
| `tests/unit/ProjectConfigTest.cpp` | 27 | TOML parser (basic/edge/error), ProjectConfig loading, path utilities |
| **Toplam** | **474** | |

---

## Build Komutlari

```bash
# MinGW build (testler, LLVM yok)
cmake -G "MinGW Makefiles" -B build
cmake --build build
ctest --test-dir build --output-on-failure

# Clang build (livac + testler, onerilen)
build_clang.bat
ctest --test-dir build-clang --output-on-failure

# MSVC build (legacy, vcvarsall gerekli)
build_msvc.bat

# Ornek calistirma
livac --emit-ir examples/associated_values.liva -o output.ll
clang output.ll -o output.exe
./output.exe
```

---

## Bilinen Sorunlar ve Teknik Borc

1. ~~**OwnershipChecker IRGen'e entegre degil**~~ - COZULDU (M33: DynArray/Map/Set auto-free)
2. ~~**LifetimeAnalysis.h bos**~~ - COZULDU (TD2: scope-based borrow-outlives checking)
3. ~~**RefExpr codegen eksik**~~ - COZULDU (M20)
4. ~~**Optional chaining yok**~~ - COZULDU (M21)
5. ~~**Closure capture by reference yok**~~ - COZULDU (M32)
6. ~~**where clause yok**~~ - COZULDU (M19)
7. ~~**Associated types yok**~~ - COZULDU (TD3: protocol/impl associated types)
8. ~~**IRGen.cpp tek monolitik dosya**~~ - COZULDU (TD1: 6 dosyaya bolundu)
9. **Ayri derleme yok** - Modul sistemi inline (tek LLVM Module'e ekleme)

---

## Oncelik Sirasi (Onerilen)

1. ~~**RefExpr codegen**~~ - TAMAMLANDI (M20)
2. ~~**Optional chaining**~~ - TAMAMLANDI (M21)
3. ~~**Standart kutuphane**~~ - TAMAMLANDI (M22c Math, M20a Map/Set, M20b I/O)
4. ~~**For-in koleksiyonlari**~~ - TAMAMLANDI (M23)
5. ~~**String metodlari**~~ - TAMAMLANDI (M24)
6. ~~**Tip donusumleri**~~ - TAMAMLANDI (M22d parseInt/parseFloat)
7. ~~**Stdlib gelistirmeleri**~~ - TAMAMLANDI (M25 split/DynArray methods)
8. ~~**Higher-order array methods**~~ - TAMAMLANDI (M26 forEach/map/filter)
9. ~~**Dil gelistirmeleri**~~ - TAMAMLANDI (M27 reduce/enum methods/while-let)
10. ~~**Pratik ozellikler**~~ - TAMAMLANDI (M28 string indexing/multi-arg println/inferred type fix)
11. ~~**Syntax kolayliklari**~~ - TAMAMLANDI (M29 multi-line strings/slicing/default args)
12. ~~**Ternary & Type Aliases**~~ - TAMAMLANDI (M30 ternary expression/type aliases)
13. ~~**Tuple Types & Multi-Return**~~ - TAMAMLANDI (M31 tuple types/destructuring/multi-return)
14. ~~**Closure Capture by Reference**~~ - TAMAMLANDI (M32 by-ref capture/varRefTypes reuse)
15. ~~**Ownership + IRGen Integration**~~ - TAMAMLANDI (M33 auto-free DynArray/Map/Set/move tracking)
16. ~~**Teknik Borc Temizligi**~~ - TAMAMLANDI (TD1 IRGen split, TD2 LifetimeAnalysis, TD3 Associated Types)
17. ~~**Async/Await Faz 1**~~ - TAMAMLANDI (M34 senkron semantik Task<T>)
18. ~~**Derleme zamani degerlendirme**~~ - TAMAMLANDI (M35 const keyword, compile-time eval)
19. ~~**Teknik Borc 4**~~ - TAMAMLANDI (TD4 deprecated LLVM API fix, test coverage, plan.md tutarliligi)
20. ~~**Zengin Stdlib (I2)**~~ - TAMAMLANDI (14 yeni builtin: randInt, randFloat, env, exit, args, clock, clockMs, sleep, regexMatch, regexFind, regexFindAll, regexReplace, httpGet, httpPost)
21. ~~**Guard Clause (F3)**~~ - TAMAMLANDI (match arm'larinda where kosulu)
22. ~~**Drop Trait (F4)**~~ - TAMAMLANDI (protocol Drop, otomatik scope cleanup)
23. ~~**Operator Overloading (F5)**~~ - TAMAMLANDI (protocol-tabanli Add/Sub/Mul/Div/Mod/Eq/Less)
24. ~~**Custom Iterator (F6)**~~ - TAMAMLANDI (Iter protocol, next(mut self) -> T? ile for-in)
25. ~~**Variadic Fonksiyonlar (F7)**~~ - TAMAMLANDI (T... sozdizimi, DynArray packing)
26. ~~**Nested Pattern Matching (F8)**~~ - TAMAMLANDI (recursive pattern esleme, emitNestedPatternMatch)
27. ~~**Proje Manifest (P1)**~~ - TAMAMLANDI (liva.toml, livac build/run/init, TOML parser, coklu modul yollari)

---

## Gelecek Ozellikler (Planlanmis)

### Dil Ozellikleri

| # | Ozellik | Aciklama | Karmasiklik |
|---|---------|----------|-------------|
| F1 | **Async/Await Faz 2** | Gercek coroutine destegi (LLVM coroutines), runtime scheduler | Yuksek | [TAMAMLANDI] |
| F2 | **Coklu Trait Bound** | `T: Printable + Hashable` sozdizimi ile coklu kisitlama | Orta | [TAMAMLANDI] |
| F3 | **Guard Clause (Pattern)** | `case .Circle(r) where r > 0 =>` match arm koruma ifadesi | Orta | [TAMAMLANDI] |
| F4 | **Drop Trait / Destructor** | Kullanici tanimli kaynak temizleme, scope cikisinda otomatik cagrilan `drop(self)` | Yuksek | [TAMAMLANDI] |
| F5 | **Operator Overloading** | `+`, `==`, `<` vb. operatorleri struct/enum icin ozellestirme (`protocol Add { ... }`) | Orta | [TAMAMLANDI] |
| F6 | **Custom Iterator** | `Iterator` protokolu, `next() -> T?` ile `for-in` genisletme | Orta | [TAMAMLANDI] |
| F7 | **Variadic Fonksiyonlar** | Degisken sayida arguman destegi (`func sum(values: i32...)`) | Orta | [TAMAMLANDI] |
| F8 | **Nested Pattern Matching** | `match` icinde ic ice pattern esleme (`case .Some(.Circle(r)) =>`) | Orta | [TAMAMLANDI] |

### Altyapi

| # | Ozellik | Aciklama | Karmasiklik |
|---|---------|----------|-------------|
| I1 | **Ayri Derleme** | Modul basina ayri LLVM Module / object file + linking | Yuksek |
| I2 | **Zengin Stdlib** | Daha kapsamli standart kutuphane (date/time, regex, networking) | Yuksek | [TAMAMLANDI] |
| P1 | **Proje Manifest** | liva.toml konfigurasyonu, livac build/run/init alt-komutlari, coklu modul arama yollari | Orta | [TAMAMLANDI] |

### Araclar

| # | Ozellik | Aciklama | Karmasiklik |
|---|---------|----------|-------------|
| T1 | **REPL** | Interaktif komut satiri degerlendirme (JIT ile) | Orta |
| T2 | **LSP Sunucusu** | IDE destegi — otomatik tamamlama, hata gosterimi, go-to-definition | Yuksek |

---

## Production Yol Haritasi

### Faz 1: Stabilite [TAMAMLANDI]

| # | Gorev | Aciklama | Durum |
|---|-------|----------|-------|
| S1 | **Runtime Bellek Sizintilari** | `liva_str_array_free`, `liva_args_free` free fonksiyonlari, async kuyruk tasma kontrolu | TAMAMLANDI |
| S2 | **Parser Hata Kurtarma** | `synchronize()` ile statement/declaration boundary recovery, `maxErrors_=20` limiti, `err_too_many_errors` diagnostigi | TAMAMLANDI |
| S3 | **IRGen Null Guard'lari** | `getOrPanic()` helper ile ~60 runtime getFunction() cagrisi korundu, assert ile hata tespiti | TAMAMLANDI |

### Faz 2: Kullanilabilirlik

| # | Gorev | Aciklama | Durum |
|---|-------|----------|-------|
| S4 | **Hata Mesaji Kaynak Gosterimi** | Diagnostik ciktisinda kaynak satir + caret (`^~~~`) gosterimi | |
| S5 | **Debug Bilgisi (DWARF/CodeView)** | LLVM DIBuilder ile fonksiyon/degisken/satir debug metadata uretimi | |
| S6 | **Unicode Destegi** | UTF-8 identifier, `\u{XXXX}` escape, multibyte sutun takibi veya ASCII-only belgeleme | |

### Faz 3: Platform Genisletme

| # | Gorev | Aciklama | Durum |
|---|-------|----------|-------|
| S7 | **Linux/macOS Destegi** | PATH'den clang arama, platform-agnostic HTTP, POSIX args, dosya yolu normalizasyonu | |
| S8 | **Ayri Derleme** | Modul basina ayri .o dosyasi, incremental build, interface/implementation ayirimi | |

### Faz 4: Ekosistem

| # | Gorev | Aciklama | Durum |
|---|-------|----------|-------|
| S9 | **Liva Stdlib Sarmalayicilari** | Import edilebilir standart moduller (String, Array, Map, IO, Math) | |
| S10 | **Paket Yonetimi** | Dependency resolution, versiyon kontrolu, paket registry | |
| S11 | **LSP Sunucusu** | IDE destegi (otomatik tamamlama, hata gosterimi, go-to-definition) | |
| S12 | **REPL** | Interaktif komut satiri degerlendirme (JIT ile) | |
