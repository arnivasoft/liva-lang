# Liva-Lang Derleyici - Proje Plani

## Genel Bakis

**Liva**, Swift benzeri sozdizimi ve Rust tarzi ownership/borrowing semantigine sahip bir programlama dili. C++20 ile yazilmis derleyici LLVM 21 backend kullaniyor.

- **Platform:** Windows, llvm-mingw Clang 21.1.8 (MSVC ABI), MinGW GCC 15.2.0 (testler)
- **Build:** CMake, GoogleTest
- **Test:** 191/191 gecen test (lexer:22, parser:48, sema:103, type:12, ownership:6)

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
[Sema] --> Type checking + Ownership checking
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
- `liva_sema` - Sema, TypeChecker, OwnershipChecker, Scope (depends: liva_ast, liva_common)
- `liva_irgen` - IRGen (depends: liva_ast, liva_sema, LLVM)
- `liva_codegen` - CodeGen, TargetInfo (depends: liva_irgen, LLVM)

### livac Komut Satiri
- `livac --dump-tokens <file>` - Token listesini goster
- `livac --dump-ast <file>` - AST agacini goster
- `livac --check-only <file>` - Sadece sema analizi
- `livac --emit-ir <file>` - LLVM IR ciktisi
- `livac -o <output> <file>` - Derle

---

## Tamamlanan Milestone'lar

### M0: Proje Iskeleti [TAMAMLANDI]
- CMake build sistemi kurulumu
- MinGW + MSVC + Clang triple-build destegi
- GoogleTest entegrasyonu
- Dizin yapisi: include/, src/, tests/, examples/

### M1: Lexer [TAMAMLANDI] - 22 test
- 77 token turu (anahtar kelimeler, operatorler, literaller, noktalamalar)
- Tam token listesi:
  - **Ozel:** `eof`, `identifier`, `integer_literal`, `float_literal`, `string_literal`, `char_literal`, `bool_literal`, `newline`, `string_interp_begin`, `string_interp_mid`, `string_interp_end`
  - **Anahtar kelimeler (27):** `func`, `struct`, `enum`, `impl`, `protocol`, `import`, `case`, `let`, `var`, `if`, `else`, `while`, `for`, `in`, `break`, `continue`, `return`, `match`, `as`, `pub`, `self`, `ref`, `mut`, `true`, `false`, `nil`, `where`, `async`, `await`, `try`
  - **Tip anahtar kelimeleri (13):** `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64`, `bool`, `string`, `void`
  - **Operatorler & Noktalamalar (29):** `(){}[],;:.::->=>#@_+-*/%==!=<<=>>=&&||!&|^~<<>>=+=-=*=/=%=`
- Hex (0x), binary (0b), octal (0o) sayi literal destegi
- Satir ve blok yorum destegi
- String interpolasyon tokenleri (`\(expr)`)
- Kaynak konum takibi (satir, sutun)

### M2: Parser + AST [TAMAMLANDI] - 48 test
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
| ProtocolDecl | `protocol Drawable { func draw(self) }` | Tam (static + dynamic dispatch, defaults) |
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
| MemberExpr | `obj.field`, `Color.Red` | Tam |
| IndexExpr | `arr[i]` | Tam |
| AssignExpr | `x = 5`, `x += 1` | Tam |
| StructLiteralExpr | `Point { x: 1.0, y: 2.0 }` | Tam |
| MatchExpr | `match s { ... }` | Tam |
| ArrayLiteralExpr | `[1, 2, 3]` | Tam |
| CastExpr | `expr as Type` | Tam |
| RefExpr | `ref x`, `ref mut x` | Sema, codegen eksik |
| GroupExpr | `(expr)` | Tam |
| RangeExpr | `0..10` | Tam (for-in icinde) |
| UnwrapExpr | `x!` | Tam |
| ClosureExpr | `\|x: i32\| -> i32 { return x * 2 }` | Tam (capture, inference, trailing) |
| TryExpr | `try riskyFunc()` | Tam |

### M3: Tip Kontrolu (Kismi) [TAMAMLANDI] - 12 tip test + 103 sema test
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

### M5: Ownership Kontrolu (Kismi) [TAMAMLANDI] - 6 test
- Sahiplik durumu makinesi: Owned -> Moved/Borrowed -> Dropped
- Move semantigi takibi
- Immutable/mutable borrow kontrolu
- Use-after-move tespiti
- Immutable degiskene atama tespiti
- Borrow catismasi tespiti
- **Sinirlamalar:**
  - OwnershipChecker IRGen pipeline'ina entegre degil
  - Lifetime analizi henuz uygulanmadi (LifetimeAnalysis.h bos)
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

---

## Gelecek Milestone'lar

### M19: Standart Kutuphane [PLANLANMADI]

**M19a: Temel Tipler**
- `Map<K, V>` - hash map
- `Set<T>` - hash set

**M19b: I/O**
- `File` tipi, okuma/yazma
- `stdin`, `stdout`, `stderr`
- Formatlama: `format("x = {}", x)`

**M19c: Matematik**
- `abs`, `min`, `max`, `sqrt`, `pow`
- Trigonometrik fonksiyonlar

### M20: Async/Await [PLANLANMADI]
- `async func fetch() -> String { ... }`
- `let result = await fetch()`
- Coroutine tabanli uygulama

### M21: Derleme Zamani Degerlendirme [PLANLANMADI]
- `const` fonksiyonlar ve ifadeler
- Compile-time array boyutu hesaplama

---

## Diagnostik Envanterleri (54 tanimli)

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

### Sema - Protocol/Trait (3)
- `err_undefined_protocol`, `err_missing_protocol_method`, `err_no_conformance`

### Sema - Error Handling (1)
- `err_try_on_non_result`

### Sema - Module (2)
- `err_module_not_found`, `err_circular_import`

### Sema - Match (1)
- `err_nonexhaustive_match`

### Sema - Optional (1)
- `err_nil_without_optional`

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
| `tests/unit/LexerTest.cpp` | 22 | Token turleri, literaller, yorumlar, konum, string interpolasyon |
| `tests/unit/ParserTest.cpp` | 48 | Bildirimler, ifadeler, generics, optional, closure, protocol, import, trait bounds |
| `tests/unit/SemaTest.cpp` | 103 | Struct, enum, match, string, generics, dyn array, optional, closure, protocol, result, module, trait bounds |
| `tests/unit/TypeTest.cpp` | 12 | Tip uyumlulugu, donusum, bit genisligi |
| `tests/unit/OwnershipTest.cpp` | 6 | Move, borrow, use-after-move |
| **Toplam** | **191** | |

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

1. **OwnershipChecker IRGen'e entegre degil** - Ownership kontrolu yapiliyor ama codegen'e yansimitlmiyor
2. **LifetimeAnalysis.h bos** - Lifetime analizi henuz uygulanmadi
3. **RefExpr codegen eksik** - Parse ve sema var, IR uretimi yok
4. **Optional chaining yok** - `obj?.method()` sozdizimi henuz desteklenmiyor
5. **Closure capture by reference yok** - Sadece capture by value destekleniyor
6. **where clause yok** - Trait bounds sadece `<T: Protocol>` sozdizimi ile
7. **Associated types yok** - Protocol'larda iliskili tip tanimlama yok
8. **Ayri derleme yok** - Modul sistemi inline (tek LLVM Module'e ekleme)

---

## Oncelik Sirasi (Onerilen)

1. **M19: Standart kutuphane** - Map, Set, File I/O, matematik
2. **M20: Async/Await** - Ileri ozellik
3. **M21: Derleme zamani degerlendirme** - Optimizasyon
