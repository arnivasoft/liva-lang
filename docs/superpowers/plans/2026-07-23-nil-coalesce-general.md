# `??` Genel LHS — Implementasyon Planı (Roadmap #5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `emitNilCoalesce`'in derleme-zamanı-ternary fallback'i, mevcut doğru genel lowering'in helper'a çıkarılıp tüm LHS şekillerine uygulanmasıyla değiştirilir. Spec: `docs/superpowers/specs/2026-07-23-nil-coalesce-general-design.md`.

## Global Constraints

- TDD zorunlu (RED kanıtı: mevcut fallback'in yanlış çıktısı; RHS-lazy yan-etki testi şart).
- Her task sonunda `build_clang.bat` + TAM seri ctest (ASLA `-j`, FOREGROUND 600000ms) yeşil; toplam raporlanır (2436 + yeniler).
- Mevcut iki doğru yol (identifier, member-chain) davranış DEĞİŞTİRMEZ — regresyon testleri korur.
- Commit: `fix(irgen): ...` + zorunlu trailer'lar:
Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01AZdcE7dS5uf54j3t6Cw216
- Sapma protokolü: Optional temsili yapısal tespite izin vermiyorsa (ör. string-optional'lar farklı temsilde) BLOCKED raporla.

## Task 0: Keşif haritası (salt-okunur, commit yok)

Rapor → `.superpowers/sdd/task-0-report.md`, her iddia file:line:
- [ ] (a) `emitNilCoalesce` TAM gövdesi (302'den sonuna): identifier yolunun kalanı, string-optional özel durumları var mı.
- [ ] (b) Optional sarmalayıcı yapısal tespiti: `getOptionalType` named mi literal struct mı üretiyor; `lhsVal->getType()`'ın Optional olduğunu güvenle anlamanın ölçütü (tip eşitliği? isStructTy+2-eleman+i1-ilk-alan?). Optional<string>/heap-string temsili ve `heapOptionalStringVars` etkileşimi (`?? `bu değişkenlerle kullanılınca bugün ne oluyor?).
- [ ] (c) Sema `NilCoalesce` kuralı (`TypeChecker.cpp:2127` civarı): optional-olmayan LHS'e diagnostik var mı; sonuç tipi nasıl çözülüyor.
- [ ] (d) `a ?? b ?? c` parse assoc'u ve mevcut codegen davranışı (derle-çalıştır probe).
- [ ] (e) Korpus: tests/examples/stdlib'de `??` kullanan yerler — fallback'e düşen şekil var mı (davranışı fix'le DEĞİŞECEK kullanım).

## Task 1: Genel lowering (TDD) — Task 0 bulgularıyla genişletilmiş kapsam

**Files:** `src/IR/IRGenExpr.cpp` (+`include/liva/IR/IRGen.h` helper bildirimi), `src/Parser/ParseExpr.cpp` (`??` sağ-assoc), `src/Sema/TypeChecker.cpp` (NilCoalesce sonuç tipi cloneTypeRepr), testler.

**Task 0 kapsam ekleri (onaylı sapma — controller):**
1. `??` SAĞ-assoc yapılır (`ParseExpr.cpp:378` — Swift uyumlu; sol-assoc zincirde `(a??b)` T üretip `T ?? c`'yi anlamsız kılıyor; sağ-assoc `a ?? (b ?? c)` ile her aşamanın LHS'i Optional kalır ve bugünkü PHI-mismatch zinciri doğal çözülür). Yapısal tespit ölçütü: `optionalTypes_` map değerlerine pointer-identity ters arama (isim/şekil sezgisi YASAK — kullanıcı struct'ıyla çakışır).
2. Sema `NilCoalesce` sonuç tipi `makePrimitiveType` yerine `cloneTypeRepr` (TypeChecker.cpp:2127-2132; visitGroupExpr'ın :2602-2613'teki aynı-sınıf düzeltmesi şablon) — named/karmaşık RHS tiplerinde slicing'i önler.
3. RHS'i Optional olan `lhs ?? rhs` (zincir dışında) bugün PHI-crash: v1 kuralı = RHS düz T olmalı; zincir sağ-assoc'la kapsanır. RHS Optional gelirse davranışı raporla (temiz Sema diagnostiği eklemek UCUZSA ekle ve raporla; değilse dokümante et).

- [ ] TDD RED: RuntimeExec `NilCoalesceCallLHS` (`f() ?? d`: f değerli→f sonucu, f nil→d; RED'de yanlış), `NilCoalesceRHSLazy` (RHS'te println izi; dolu yolda iz YOK — RED'de mevcut fallback RHS'i hiç üretmediğinden dolu yol tesadüfen geçebilir, nil yolda yanlış döner; RED durumunu ölçüp raporla), `NilCoalesceChained` (`a ?? b ?? c` — Task 0 (d) bulgusuna göre), `NilCoalesceSubscriptLHS` (ifade edilebilirse; değilse raporla), regresyon: `NilCoalesceIdentifierRegression`, `NilCoalesceMemberChainRegression`.
- [ ] `emitOptionalCoalesce(lhsVal, rhsExpr)` helper'ı (member-chain kodu verbatim); fallback → LHS değerlendir + yapısal Optional testi (Task 0 (b) ölçütü) → helper; Optional değilse mevcut davranış + yorum.
- [ ] GREEN + tam suite + commit `fix(irgen): ?? genel LHS desteği — derleme-zamanı ternary fallback'i kaldırıldı`.

## Task 2: Docs + kapanış

- [ ] `docs/en/LANGUAGE-REFERENCE.md` + TR: `??` bölümü varsa güncelle/yoksa kısa bölüm (genel LHS, lazy RHS; örnekler livac-doğrulamalı).
- [ ] `roadmap.md`: öncelik satır 5 → `` `??` operatörünü genel LHS'lerde doğru üret — **tamamlandı (2026-07)** ``; 2.3 tablosundaki `??` satırına "(çözüldü 2026-07)".
- [ ] Tam suite; commit `docs(irgen): ?? genel LHS dokümantasyonu — roadmap #5 tamam`.
