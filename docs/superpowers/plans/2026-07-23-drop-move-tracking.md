# Move Takibi + Drop/Optional — Implementasyon Planı (Roadmap #4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drop'lu struct'larda `let b = a` / `b = a` move semantiği (double-drop biter) + `Optional<DropluStruct>` scope-çıkışı koşullu drop + if-let sahiplik devri. Spec: `docs/superpowers/specs/2026-07-23-drop-move-tracking-design.md` (muhafazakâr kapsam ONAYLI).

**Tech Stack:** C++20, LLVM 21, `build_clang.bat`, ctest (SERİ), GoogleTest.

## Global Constraints

- **TDD zorunlu**: her davranış değişikliği önce RED test (drop-sayacı deseni: drop metodu println izi bırakır, test TAM sayıyı assert eder), sonra implementasyon, GREEN. RED/GREEN kanıtı rapora.
- Her task sonunda: `build_clang.bat` + TAM seri suite (`ctest --test-dir build-clang --output-on-failure`, ASLA `-j`, FOREGROUND 600000ms) yeşil; toplam sayı raporlanır (2414 + yeniler).
- Davranış değişikliği KAPSAMI: yalnız Drop'lu struct'lar. Drop'suz tiplerde sıfır fark — bunu koruyan negatif testler yazılır (Drop'suz struct `let b = a` sonrası `a` kullanılabilir kalır, kopya davranır).
- Mevcut test Drop'lu tip kopyalıyorsa: move semantiğine göre güncellenir + raporlanır (onaylı davranış değişikliği).
- Commit formatı: `fix(ownership): ...` / `feat(ownership): ...` + zorunlu trailer'lar:
Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
- Sapma protokolü: Sema/IRGen "Drop'lu tip" tespiti iki katmanda AYNI kümeyi vermiyorsa veya if-let mekanizması beklenmedik çıkarsa BLOCKED raporla.

## Task 0: Mekanizma haritası (salt-okunur, commit yok)

**Rapor** → `.superpowers/sdd/task-0-report.md`. Her iddia file:line ile.

- [x] **(a) Drop-conformance tespiti**: Sema'da bir struct'ın Drop'a uyduğu NEREDE kayıtlı (TypeChecker conformance kaydı? OwnershipChecker erişebiliyor mu?); IRGen'de `<Struct>_drop` fonksiyon üretimi nerede (IRGenDecl?) ve scope temizliği drop çağrısını nasıl seçiyor (`IRGenStmt.cpp:36-136` — varStructTypes + ne kontrolü?). İki katman için önerilen "isDropType(name)" kaynağını yaz.
- [x] **(b) OwnershipChecker veri akışı**: `trackVariable`'a isCopyType'ı kim hesaplayıp veriyor; visitVarDecl/visitAssignExpr mevcut halleri (var mı, ne yapıyor); Drop bilgisi OwnershipChecker'a nasıl taşınır (en az invaziv yol).
- [x] **(c) if-let/while-let codegen haritası**: `IRGenStmt.cpp:506` neyi movedVars'a ekliyor; binding payload'u nasıl materialize ediyor (kopya mı pointer mı); if-let scope'unda binding drop ediliyor mu; Optional kaynağın temizlikle ilişkisi.
- [x] **(d) heapOptionalStringVars şablonu**: tam mekanizma (kayıt nerede, temizlik kodu) — Optional<Drop> genellemesinin şablonu.
- [x] **(e) Korpus taraması**: tests/examples/stdlib'de Drop'lu struct kopyalayan (`let x = y` identifier-init veya atama) kod var mı; if-let ile Optional<Droplu> kullanan var mı (websocket/json stdlib'i Drop kullanıyor — özellikle tara).

## Task 1: `let b = a` + `b = a` move semantiği (double-drop fix)

**Files:** `src/Sema/OwnershipChecker.cpp` (+.h gerekirse), `src/IR/IRGenDecl.cpp` (visitVarDecl init yolu), `src/IR/IRGenExpr.cpp` veya `src/IR/IRGenCall.cpp` (visitAssignExpr — Task 0 (c) bulgusuna göre), testler.

- [x] **TDD RED**: RuntimeExec `DropMoveLetNoDoubleDrop` (Drop'lu struct, drop println izi; `let b = a` sonrası scope biter → iz TAM 1 kez; RED'de 2) + `DropMoveAssignNoDoubleDrop` (atama varyantı) + `NonDropStructCopyUnchanged` (Drop'suz: `let b = a` sonrası `a` kullanılır, davranış değişmez — bu test RED'de de GREEN'de de geçmeli, koruma testi) + SemaTest `UseAfterMoveDropType` (`let b = a` sonrası `a` kullanımı → err_use_after_move; RED'de kabul).
- [x] **Sema**: Task 0 (b) yoluyla Drop bilgisi OwnershipChecker'a; visitVarDecl (identifier init, Drop'lu) + visitAssignExpr (identifier RHS, Drop'lu) → markMoved.
- [x] **IRGen**: aynı iki noktada `vars_.movedVars.insert(kaynakAd)` (yalnız Drop'lu struct tiplerde — Task 0 (a) tespitiyle).
- [x] GREEN + tam suite + commit `fix(ownership): Drop'lu tiplerde let/atama move semantiği — double-drop kapandı`. (2419 tests, 100% pass — see `.superpowers/sdd/task-1-report.md`)

## Task 2: Optional<Droplu> scope drop + if-let sahiplik

**Files:** `src/IR/IRGenStmt.cpp` (scope temizliği + if-let/while-let), gerekirse `include/liva/IR/IRGen.h` (yeni takip seti, heapOptionalStringVars kardeşi), testler.

- [ ] **TDD RED**: RuntimeExec `OptionalDropFiresOnScopeExit` (Optional<Droplu> değerli → iz 1; RED'de 0), `OptionalDropNilNoDrop` (nil → 0), `IfLetBindingSingleDrop` (if-let ile açılan değer: toplam iz TAM 1 — binding drop'u VEYA kaynak drop'u, ikisi değil), `WhileLetSingleDropPerIteration` (mümkünse; değilse raporla).
- [ ] Scope temizliğine Optional<Droplu> koşullu drop (has-value bayrağı + `T_drop`, heapOptionalStringVars şablonu genellemesi; movedVars guard'lı).
- [ ] if-let/while-let: binding sahipliği alır → kaynak Optional moved (veya payload-consumed) işaretlenir; Task 0 (c) haritasına göre en az invaziv biçim.
- [ ] GREEN + tam suite + commit `feat(ownership): Optional<Drop> scope drop'u + if-let sahiplik devri`.

## Task 3: Docs + kapanış

- [ ] `docs/en/LANGUAGE-REFERENCE.md` + `docs/tr/LANGUAGE-REFERENCE.md`: ownership/Drop bölümüne move semantiği (Drop'lu tipler), Optional drop ve if-let sahipliği; örnekler livac ile doğrulanır. Bilinen sınırlar (atamada eski değer drop edilmez; clone yok) dürüstçe yazılır.
- [ ] `roadmap.md` satır 4: `` Atama/if-let move takibi — **tamamlandı (2026-07, muhafazakâr kapsam: Drop'lu tipler)** — kalan: clone(), atamada eski-değer drop'u, koleksiyon/alan drop'ları `` ; ayrıca 2.3 tablosundaki ilgili iki hata satırına "(çözüldü 2026-07)" notu.
- [ ] Tam seri suite; commit `docs(ownership): move semantiği + Optional drop dokümantasyonu — roadmap #4 tamam`.
