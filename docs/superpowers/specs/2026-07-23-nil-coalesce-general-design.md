# `??` Genel LHS Desteği — Tasarım (Roadmap #5)

Tarih: 2026-07-23
Kapsam: `IRGen::emitNilCoalesce` (`src/IR/IRGenExpr.cpp:302+`) — bilinen sessiz-yanlış-davranış hatasının kapatılması. Dar kapsam: tek fonksiyon + testler; Sema değişikliği beklenmiyor (gerekirse sapma raporu).

## Tespit

`emitNilCoalesce` üç yol içeriyor:
1. Identifier yolu (`varOptionalTypes` + alloca) — doğru.
2. MemberExpr optional-chain yolu — **tam genel lowering** (LHS değeri → temp alloca → has-flag testi → CondBr → PHI; RHS yalnızca nil dalında = lazy) — doğru.
3. Fallback (`:350-351`): `return lhs ? lhs : visit(rhs)` — **derleme-zamanı C++ ternary'si**; LHS codegen'i değer ürettiyse RHS hiç üretilmeden LHS koşulsuz döner. `foo() ?? default`, subscript, çağrı zinciri vb. → sessizce yanlış (nil testi yok, RHS ölü).

## Normatif davranış

`lhs ?? rhs` her LHS şekli için: LHS bir kez değerlendirilir; LHS'in ürettiği değer Optional sarmalayıcıysa (`{i1, T}` — `getOptionalType` düzeni) has-flag test edilir: dolu → payload; nil → RHS değerlendirilir (RHS YALNIZCA nil yolunda çalışır — yan-etkiyle test edilir). Sonuç tipi `T`.

## Değişiklik

1. MemberExpr dalındaki genel lowering `emitOptionalCoalesce(llvm::Value *lhsVal, Expr *rhsExpr)` helper'ına verbatim çıkarılır; mevcut iki doğru yol davranış değiştirmez.
2. Fallback yeniden yazılır: LHS değerlendirilir; değer tipi yapısal olarak Optional sarmalayıcıysa → helper. Değilse mevcut davranış korunur ve Task 0 bulgusuna göre raporlanır (Sema'nın optional-olmayan LHS'e `??` kuralı ne diyorsa o geçerli).
3. Task 0 keşif kalemleri (implementasyon öncesi haritalanır): Optional<string>/heap-string temsili ve `heapOptionalStringVars` etkileşimi; Sema `NilCoalesce` kuralı (`TypeChecker.cpp:2127`); `a ?? b ?? c` assoc/zincirleme; identifier yolunun tam gövdesi (356+); Optional sarmalayıcının yapısal tespiti için güvenilir ölçüt (LLVM tip karşılaştırması — Optional struct'ları named mi literal mi).

## Doğrulama

TDD. RuntimeExec: `foo() ?? d` değerli/nil; **RHS-lazy kanıtı** (RHS'te println izi — dolu yolda iz YOK); subscript/zincir şekilleri; `a ?? b ?? c`; mevcut identifier/member şekilleri regresyon. Tam seri suite yeşil (2436 + yeniler).

## Kapsam dışı

Sema tip kuralı değişiklikleri; `??=` gibi yeni operatörler; Optional dışı türlere `??` genişletmesi.
