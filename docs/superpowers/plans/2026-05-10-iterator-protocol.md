# Iterator / AsyncIterator Protocol Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `protocol Iterator` and `protocol AsyncIterator` to stdlib and rewire `for x in expr` and `for await x in expr` to dispatch through them, while keeping built-in fast paths (DynArray, Map, Set, Range, Generator) unchanged.

**Architecture:** Hybrid dispatch. Sema seeds an implicit conformance for built-in iterables (so generic constraints accept them) but IRGen continues using hardcoded paths for those types. Custom user types conform to `Iterator` / `AsyncIterator` and are dispatched via protocol method call (devirtualized when the concrete type is known, vtable otherwise). The string `"Iter"` used today as a hardcoded protocol name in Sema is migrated to `"Iterator"`.

**Tech Stack:** C++20 (compiler), Liva (stdlib + tests), LLVM 21 backend, GoogleTest, ctest, Ninja+Clang build (`build_clang.bat` / `cmake --build build-clang`).

**Spec:** `docs/superpowers/specs/2026-05-10-iterator-protocol-design.md` (commit `3c09bf8`).

**Resolved open questions** (from spec):
- **Associated-type equality syntax**: Liva already accepts `where T.Item == X` (ParseDecl.cpp:202-228, `WhereConstraint::AssociatedTypeEqual`). Use this — spec's `I::Item == i32` becomes `I.Item == i32`.
- **Built-in receivers in generic bodies**: chosen option (i) from the spec — generic body uses `for x in it` for built-in receivers; a direct `it.next()` against a built-in receiver inside a generic body is an error. The IRGen `for-in` built-in path is intrinsic-emitted whenever the monomorphized type matches a known built-in.

---

## File Structure

**Created:**
- `stdlib/core/iterator.liva` — `protocol Iterator` and `protocol AsyncIterator` (~30 lines)
- `tests/unit/IteratorProtocolTest.cpp` — Sema + IRGen + small runtime cases for the new protocols (~400 lines)

**Modified:**
- `include/liva/Common/DiagnosticKinds.def` — three new diagnostics
- `include/liva/Sema/TypeChecker.h` — rename `iteratorElementTypes_` → `iteratorItemTypes_`; add `asyncIteratorItemTypes_`
- `src/Sema/TypeChecker.cpp` — for-in / for-await resolution; built-in implicit conformance seed; `"Iter"` → `"Iterator"` migration
- `src/Sema/Sema.cpp` — bootstrap call to seed built-in conformances (if init lives elsewhere, follow that pattern instead)
- `src/IR/IRGenStmt.cpp` — refactor custom-iterator branch to protocol-method dispatch; add for-await loop pattern
- `tests/unit/SemaTest.cpp` — adjust any test that referenced the old `"Iter"` literal
- `tests/unit/SelfHostTest.cpp` — runtime end-to-end cases
- `tests/CMakeLists.txt` — wire `IteratorProtocolTest.cpp` into the test target
- `docs/COOKBOOK.md`, `docs/tr/COOKBOOK.md` — replace the "custom Iter" example with `impl Iterator for ...`
- `docs/LANGUAGE-REFERENCE.md` — append a short Iterator / AsyncIterator section

Each file has one clear responsibility. The new test file groups all Iterator-protocol-specific cases so they can be located, reviewed, and extended in one place. Sema for-in / for-await type-checking changes stay inside the existing `TypeChecker` body that already owns that logic.

---

## Task 1: Add new diagnostics

**Files:**
- Modify: `include/liva/Common/DiagnosticKinds.def`

- [ ] **Step 1: Append three diagnostics**

Open `include/liva/Common/DiagnosticKinds.def` and add (preserving the file's existing macro style — match the surrounding entries):

```
DIAG(err_for_in_not_iterable, Error,
     "type '%0' is not iterable; conform to 'Iterator' or use a built-in iterable")
DIAG(err_for_await_requires_async_iterator, Error,
     "type '%0' is not an async iterable; conform to 'AsyncIterator'")
DIAG(err_iterator_next_signature, Error,
     "'Iterator.next' must be 'mut func next() -> Item?'; got '%0'")
```

Place them next to the existing for-in / async diagnostics (search for `err_for_in` or `err_for_await_requires_async_context` and insert nearby).

- [ ] **Step 2: Build to confirm the macro file compiles**

Run: `cmake --build build-clang --parallel 2>&1 | grep -E "error:" | head`
Expected: no output.

- [ ] **Step 3: Commit**

```bash
git add include/liva/Common/DiagnosticKinds.def
git commit -m "diag: add iterator-protocol diagnostics"
```

---

## Task 2: Create stdlib/core/iterator.liva

**Files:**
- Create: `stdlib/core/iterator.liva`

- [ ] **Step 1: Write the stdlib protocol file**

Create `stdlib/core/iterator.liva` with this exact content:

```liva
/// Iterator protocol — produces a sequence of values, one at a time.
///
/// Conformers implement `next()`, which returns the next element or `nil`
/// when the iterator is exhausted. After `nil` is returned, subsequent
/// calls must continue to return `nil` (the iterator is "fused"). The
/// fused contract is documented but not enforced by the compiler.
pub protocol Iterator {
    type Item

    /// Advance the iterator and return the next element, or `nil` if done.
    mut func next() -> Item?
}

/// AsyncIterator protocol — produces a sequence of values asynchronously.
///
/// Used with `for await x in source` inside `async` functions. Returns
/// `nil` to signal end of stream.
pub protocol AsyncIterator {
    type Item

    /// Asynchronously advance and return the next element, or `nil`.
    mut func next() async -> Item?
}
```

- [ ] **Step 2: Confirm the file parses**

Build the compiler and run a quick parse check by adding a throw-away test or running an existing parser test that loads stdlib.

Run: `cmake --build build-clang --parallel 2>&1 | grep -E "error:" | head`
Expected: no output.

If your build pipeline does not auto-include this file, locate the stdlib include manifest (search `stdlib/core/` references in CMakeLists or `Driver/CompilerInstance.cpp`) and add `iterator.liva` to it. Don't guess — `grep -rn "core/option" stdlib/ src/` shows how the existing `core/option.liva` is wired.

- [ ] **Step 3: Commit**

```bash
git add stdlib/core/iterator.liva
# add CMake/manifest changes if any:
# git add <wherever-stdlib-files-are-listed>
git commit -m "stdlib: add core/iterator.liva with Iterator + AsyncIterator protocols"
```

---

## Task 3: Add `asyncIteratorItemTypes_` field; rename `iteratorElementTypes_` → `iteratorItemTypes_`

**Files:**
- Modify: `include/liva/Sema/TypeChecker.h`
- Modify: `src/Sema/TypeChecker.cpp` (rename usages)

- [ ] **Step 1: Rename the field in the header**

Open `include/liva/Sema/TypeChecker.h`, find the `iteratorElementTypes_` declaration (around line 153 per the spec), and rename it:

```cpp
// Before:
//   std::unordered_map<std::string, const TypeRepr *> iteratorElementTypes_;
// After:
std::unordered_map<std::string, const TypeRepr *> iteratorItemTypes_;
std::unordered_map<std::string, const TypeRepr *> asyncIteratorItemTypes_;
```

- [ ] **Step 2: Rename all usages in the .cpp file**

Search-and-replace `iteratorElementTypes_` → `iteratorItemTypes_` across `src/Sema/TypeChecker.cpp`.

```powershell
$f = 'src\Sema\TypeChecker.cpp'
$c = [io.file]::ReadAllText($f)
$c = $c.Replace('iteratorElementTypes_', 'iteratorItemTypes_')
[io.file]::WriteAllText($f, $c, [System.Text.UTF8Encoding]::new($false))
```

Verify no other source file references it:

```bash
grep -rn "iteratorElementTypes_" F:/Cpp_Projects/liva-lang/src F:/Cpp_Projects/liva-lang/include
```

Expected: empty.

- [ ] **Step 3: Build**

Run: `cmake --build build-clang --parallel 2>&1 | grep -E "error:" | head`
Expected: no output.

- [ ] **Step 4: Run all tests (regression)**

Run: `ctest --test-dir build-clang -j 1 2>&1 | tail -3`
Expected: `100% tests passed, 0 tests failed out of 2251`.

- [ ] **Step 5: Commit**

```bash
git add include/liva/Sema/TypeChecker.h src/Sema/TypeChecker.cpp
git commit -m "sema: rename iteratorElementTypes_ to iteratorItemTypes_; add asyncIteratorItemTypes_ field"
```

---

## Task 4: Migrate hardcoded `"Iter"` string to `"Iterator"`

**Files:**
- Modify: `src/Sema/TypeChecker.cpp` (around line 1690)

- [ ] **Step 1: Locate every `"Iter"` literal**

```bash
grep -n '"Iter"' F:/Cpp_Projects/liva-lang/src/Sema/TypeChecker.cpp
```

Expected: at least one hit at the line referenced in the spec (~1690).

- [ ] **Step 2: Replace `"Iter"` with `"Iterator"`**

For each hit confirmed in Step 1, change the literal. Use Edit per occurrence with enough context to be unique. Do not blanket-replace — `"Iter"` may appear inside identifiers (e.g. `IterType`) where it must NOT change.

For example, the line that reads:

```cpp
auto convIt = protocolConformances_.find("Iter");
```

becomes:

```cpp
auto convIt = protocolConformances_.find("Iterator");
```

- [ ] **Step 3: Build and run all tests**

Run: `cmake --build build-clang --parallel && ctest --test-dir build-clang -j 1 2>&1 | tail -3`
Expected: 2251/2251 pass. (At this point the `"Iterator"` lookup will return empty for every type — Task 5 seeds it. That is fine: any user code that previously relied on the `"Iter"` string convention now fails type-check, which is acceptable because no stdlib actually used it; and the built-in iteration paths don't go through this lookup.)

If a test fails because it references a custom `Iter`-named protocol, update that test to use `Iterator` (the test was relying on the old string convention; the new spec-correct name is `Iterator`).

- [ ] **Step 4: Commit**

```bash
git add src/Sema/TypeChecker.cpp tests/unit
git commit -m "sema: migrate hardcoded \"Iter\" protocol name to \"Iterator\""
```

---

## Task 5: Seed built-in implicit conformance for `Iterator` and `AsyncIterator`

**Files:**
- Modify: `src/Sema/TypeChecker.cpp` (or wherever `Sema::init` / TypeChecker constructor lives)

- [ ] **Step 1: Find the bootstrap point**

```bash
grep -n "protocolConformances_\[" F:/Cpp_Projects/liva-lang/src/Sema
grep -n "TypeChecker::TypeChecker\|Sema::init\|registerBuiltins" F:/Cpp_Projects/liva-lang/src/Sema
```

Pick the function where `protocolConformances_` is first populated, or where built-ins are registered. If neither exists, the right place is the top of `TypeChecker::check` (or whatever public entry point runs once per translation unit) — guard with a `static bool seeded = false;` so it runs once per TypeChecker instance.

- [ ] **Step 2: Add the seeding logic**

Insert the following block at the bootstrap point. The exact placement depends on Step 1's findings — match the surrounding code style:

```cpp
// Built-in iterables conform to Iterator implicitly. IRGen continues to
// use hardcoded fast paths for these types; the conformance entry exists
// so that `where T: Iterator` accepts them in generic constraints.
for (const char *name : {"Range", "Array", "DynArray", "Map", "Set", "Generator"}) {
    protocolConformances_["Iterator"].push_back(name);
}
protocolConformances_["AsyncIterator"].push_back("Generator");
```

For each built-in, also seed the `Item` type. Range and Generator's element types are known at use-site (they carry a generic argument), so for those, leave `iteratorItemTypes_` unset and let the for-in resolution path (Task 6) extract the element type from the receiver's resolved type. For `Array`, `DynArray`, `Map`, `Set`, the element type is also receiver-dependent — same approach.

In other words: the conformance set is seeded statically; the `iteratorItemTypes_` lookup remains the *fallback* used only for custom-type conformers (where the impl block declares `type Item = X`).

- [ ] **Step 3: Build**

Run: `cmake --build build-clang --parallel 2>&1 | grep -E "error:" | head`
Expected: no output.

- [ ] **Step 4: Run all tests**

Run: `ctest --test-dir build-clang -j 1 2>&1 | tail -3`
Expected: 2251/2251 pass (no behavioral change yet — the conformance entries are not consulted until Task 6 enables generic-constraint dispatch).

- [ ] **Step 5: Commit**

```bash
git add src/Sema/TypeChecker.cpp
git commit -m "sema: seed built-in implicit conformance for Iterator/AsyncIterator"
```

---

## Task 6: Sema for-in resolution — accept custom Iterator conformers

**Files:**
- Modify: `src/Sema/TypeChecker.cpp:visitForStmt` (lines ~1585-1713)
- Test: `tests/unit/IteratorProtocolTest.cpp` (new file)

- [ ] **Step 1: Wire the new test file into CMake**

Open `tests/CMakeLists.txt`, find the `add_executable` (or `target_sources`) line that lists `SemaTest.cpp` and other unit tests, and add:

```cmake
tests/unit/IteratorProtocolTest.cpp
```

(match the existing list style — search for `SemaTest.cpp` to find it).

Re-run cmake configure:
```bash
cmake -B build-clang
```

- [ ] **Step 2: Write the failing test**

Create `tests/unit/IteratorProtocolTest.cpp`:

```cpp
#include "liva/Common/Diagnostics.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/TypeChecker.h"
#include <gtest/gtest.h>

namespace liva {

namespace {

// Helper: parse + sema-check a snippet, return diagnostic count
struct CheckResult {
    bool ok;
    std::vector<DiagID> diagIds;
};

CheckResult checkSnippet(const std::string &source) {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    DiagnosticsEngine diag;
    Parser parser(tokens, diag);
    auto tu = parser.parseTranslationUnit();
    TypeChecker tc(diag);
    bool ok = tc.check(*tu);
    CheckResult r{ok, {}};
    for (auto &d : diag.diagnostics()) r.diagIds.push_back(d.id);
    return r;
}

} // namespace

TEST(IteratorProtocolTest, CustomIteratorConformsAndForInResolves) {
    auto r = checkSnippet(R"(
        import core::iterator
        struct Counter { var n: i32 }
        impl Iterator for Counter {
            type Item = i32
            mut func next() -> i32? {
                if self.n <= 0 { return nil }
                self.n -= 1
                return self.n + 1
            }
        }
        func main() {
            for x in Counter(n: 3) { }
        }
    )");
    EXPECT_TRUE(r.ok);
}

} // namespace liva
```

- [ ] **Step 3: Build the test target and run it; expect FAIL**

Run:
```bash
cmake --build build-clang --target unit_tests --parallel
ctest --test-dir build-clang -R "IteratorProtocolTest.CustomIteratorConformsAndForInResolves" --output-on-failure
```

Expected: FAIL — Sema does not yet recognise custom Iterator conformers in for-in (the Item type isn't extracted, the loop variable is unbound or wrongly typed).

- [ ] **Step 4: Implement the Sema change**

In `src/Sema/TypeChecker.cpp:visitForStmt`, after the existing built-in iterable resolution branches and before the existing custom-Iter branch (line ~1690), restructure as follows:

```cpp
// Existing: built-in iterable extraction (Array/DynArray/Map/Set/Range/Generator)
// — keep unchanged.

// Replace the existing "Iter" custom-protocol lookup with this:
const std::string typeName = resolvedType ? resolvedType->getName() : "";
auto convIt = protocolConformances_.find("Iterator");
if (!typeName.empty() && convIt != protocolConformances_.end()) {
    auto &list = convIt->second;
    if (std::find(list.begin(), list.end(), typeName) != list.end()) {
        auto itemIt = iteratorItemTypes_.find(typeName);
        if (itemIt != iteratorItemTypes_.end()) {
            elementType = itemIt->second;
            iterationKind = IterationKind::CustomIterator;
            return;
        }
    }
}

// Failure (none of the above matched):
diag_.report(node->getStartLoc(), DiagID::err_for_in_not_iterable, typeName);
```

Where `IterationKind::CustomIterator` is a new enum entry — add it to whatever enum/flag `visitForStmt` already uses to discriminate built-in vs custom iteration (search for `Custom` near the existing for-in code; if no enum exists, introduce a small one local to `visitForStmt`).

When a struct's `impl Iterator for T { type Item = ... }` is processed (in the `visitImplDecl` path that handles associated types), populate `iteratorItemTypes_[T]` with the `Item` TypeRepr.

- [ ] **Step 5: Run the test; expect PASS**

Run: `ctest --test-dir build-clang -R "IteratorProtocolTest.CustomIteratorConformsAndForInResolves" --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Run all tests (regression)**

Run: `ctest --test-dir build-clang -j 1 2>&1 | tail -3`
Expected: 2251/2251 + 1 new = 2252 pass.

- [ ] **Step 7: Commit**

```bash
git add tests/CMakeLists.txt tests/unit/IteratorProtocolTest.cpp src/Sema/TypeChecker.cpp
git commit -m "sema: resolve for-in over custom Iterator conformers"
```

---

## Task 7: Sema — emit `err_for_in_not_iterable` for non-iterable types

**Files:**
- Test: `tests/unit/IteratorProtocolTest.cpp` (extend)

- [ ] **Step 1: Add the failing test**

Append to `tests/unit/IteratorProtocolTest.cpp`:

```cpp
TEST(IteratorProtocolTest, NonIterableEmitsDiagnostic) {
    auto r = checkSnippet(R"(
        struct Plain { var x: i32 }
        func main() {
            for x in Plain(x: 1) { }
        }
    )");
    EXPECT_FALSE(r.ok);
    bool found = false;
    for (auto id : r.diagIds) {
        if (id == DiagID::err_for_in_not_iterable) { found = true; break; }
    }
    EXPECT_TRUE(found);
}
```

- [ ] **Step 2: Build and run; expect PASS**

The Task 6 implementation already emits this diagnostic in the failure branch.

Run: `ctest --test-dir build-clang -R "IteratorProtocolTest.NonIterableEmitsDiagnostic" --output-on-failure`
Expected: PASS.

If it fails because the diagnostic isn't emitted for plain structs, double-check the failure branch in Task 6 Step 4 — the `diag_.report(...)` line must run when both built-in and custom-conformer checks fall through.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/IteratorProtocolTest.cpp
git commit -m "test: assert err_for_in_not_iterable for non-iterable receivers"
```

---

## Task 8: Sema — generic constraint `where I: Iterator`, `where I.Item == X`

**Files:**
- Test: `tests/unit/IteratorProtocolTest.cpp` (extend)
- Modify (only if the test fails): `src/Sema/TypeChecker.cpp`

- [ ] **Step 1: Add the failing test**

Append to `tests/unit/IteratorProtocolTest.cpp`:

```cpp
TEST(IteratorProtocolTest, GenericIteratorConstraintWithItemEquality) {
    auto r = checkSnippet(R"(
        import core::iterator
        struct Counter { var n: i32 }
        impl Iterator for Counter {
            type Item = i32
            mut func next() -> i32? {
                if self.n <= 0 { return nil }
                self.n -= 1
                return self.n + 1
            }
        }
        func sum<I>(iter: I) -> i32 where I: Iterator, I.Item == i32 {
            var total = 0
            var it = iter
            while let x = it.next() { total += x }
            return total
        }
        func main() {
            let s = sum(Counter(n: 3))
        }
    )");
    EXPECT_TRUE(r.ok);
}
```

- [ ] **Step 2: Build and run; expect PASS or FAIL**

Run: `ctest --test-dir build-clang -R "IteratorProtocolTest.GenericIteratorConstraintWithItemEquality" --output-on-failure`

- If PASS: Liva's existing `where T: Protocol` and `where T.Item == X` paths handle the new protocol with no changes. Skip to Step 4.
- If FAIL: Liva's generic constraint solver isn't applying `Iterator` correctly. Inspect the failure (likely an unresolved `it.next()` call inside the generic body) and patch `TypeChecker` so that when a generic param `I` has a `WhereConstraint::TypeBound` to `Iterator`, calls to `next()` on values of type `I` resolve to the protocol method with return type `Item?` — and `Item` is taken from the matching `WhereConstraint::AssociatedTypeEqual` when present.

- [ ] **Step 3: (Conditional) Implement the constraint solver patch**

If Step 2 failed: in `TypeChecker`, when resolving a method call whose receiver type is a generic param `I`, look at `I`'s where-constraints; if any `WhereConstraint::TypeBound` names `Iterator`, look up `Iterator`'s `next` method and use its associated-type-resolved return type. The associated type `Item` resolves to `i32` (or whatever the user wrote) via the matching `AssociatedTypeEqual` constraint, or stays abstract otherwise.

(Engineer note: the existing protocol-method dispatch on concrete types already does this lookup — find that code path and reuse it for generic-param receivers.)

- [ ] **Step 4: Run all tests (regression)**

Run: `ctest --test-dir build-clang -j 1 2>&1 | tail -3`
Expected: 2253/2253 (or +1 from the previous count).

- [ ] **Step 5: Commit**

```bash
git add tests/unit/IteratorProtocolTest.cpp src/Sema/TypeChecker.cpp
git commit -m "sema: support where I: Iterator, I.Item == T constraint"
```

---

## Task 9: Sema for-await — accept custom AsyncIterator conformers

**Files:**
- Test: `tests/unit/IteratorProtocolTest.cpp` (extend)
- Modify: `src/Sema/TypeChecker.cpp:visitForStmt`

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/IteratorProtocolTest.cpp`:

```cpp
TEST(IteratorProtocolTest, ForAwaitOverAsyncIterator) {
    auto r = checkSnippet(R"(
        import core::iterator
        struct Pings { var left: i32 }
        impl AsyncIterator for Pings {
            type Item = i32
            mut func next() async -> i32? {
                if self.left <= 0 { return nil }
                self.left -= 1
                return self.left
            }
        }
        async func main() {
            for await x in Pings(left: 2) { }
        }
    )");
    EXPECT_TRUE(r.ok);
}

TEST(IteratorProtocolTest, ForAwaitNonAsyncIterableEmitsDiagnostic) {
    auto r = checkSnippet(R"(
        struct Plain { var x: i32 }
        async func main() {
            for await x in Plain(x: 1) { }
        }
    )");
    EXPECT_FALSE(r.ok);
    bool found = false;
    for (auto id : r.diagIds) {
        if (id == DiagID::err_for_await_requires_async_iterator) { found = true; break; }
    }
    EXPECT_TRUE(found);
}
```

- [ ] **Step 2: Build and run; expect FAIL**

Run: `ctest --test-dir build-clang -R "IteratorProtocolTest.ForAwait" --output-on-failure`
Expected: both tests fail (current Sema doesn't recognise custom AsyncIterator conformers).

- [ ] **Step 3: Implement the Sema change**

In `src/Sema/TypeChecker.cpp:visitForStmt`, locate the `node->isAwait()` branch (existing async-context check around line 1587). After confirming async context, add the AsyncIterator resolution path:

```cpp
if (node->isAwait()) {
    if (!currentIsAsync_) {
        diag_.report(node->getStartLoc(), DiagID::err_for_await_requires_async_context);
        return;
    }
    const std::string typeName = resolvedType ? resolvedType->getName() : "";

    // Generator<T> — already async-capable
    if (resolvedType && resolvedType->getKind() == TypeRepr::Kind::Generator) {
        elementType = /* extract Generator element type as today */;
        iterationKind = IterationKind::Generator;
        return;
    }

    // Custom AsyncIterator
    auto convIt = protocolConformances_.find("AsyncIterator");
    if (!typeName.empty() && convIt != protocolConformances_.end()) {
        auto &list = convIt->second;
        if (std::find(list.begin(), list.end(), typeName) != list.end()) {
            auto itemIt = asyncIteratorItemTypes_.find(typeName);
            if (itemIt != asyncIteratorItemTypes_.end()) {
                elementType = itemIt->second;
                iterationKind = IterationKind::CustomAsyncIterator;
                return;
            }
        }
    }

    diag_.report(node->getStartLoc(), DiagID::err_for_await_requires_async_iterator, typeName);
    return;
}
```

When `visitImplDecl` processes an `impl AsyncIterator for T { type Item = ... }`, populate `asyncIteratorItemTypes_[T]` analogously to Task 6's Iterator handling.

- [ ] **Step 4: Run; expect PASS**

Run: `ctest --test-dir build-clang -R "IteratorProtocolTest.ForAwait" --output-on-failure`
Expected: both PASS.

- [ ] **Step 5: Run all tests (regression)**

Run: `ctest --test-dir build-clang -j 1 2>&1 | tail -3`
Expected: 2255/2255.

- [ ] **Step 6: Commit**

```bash
git add tests/unit/IteratorProtocolTest.cpp src/Sema/TypeChecker.cpp
git commit -m "sema: resolve for-await over custom AsyncIterator conformers"
```

---

## Task 10: IRGen — refactor custom for-in branch to protocol-method dispatch

**Files:**
- Modify: `src/IR/IRGenStmt.cpp` (custom-iterator branch, lines ~987-1029)
- Test: `tests/unit/SelfHostTest.cpp` (new runtime case)

- [ ] **Step 1: Write the failing runtime test**

Append to `tests/unit/SelfHostTest.cpp`:

```cpp
TEST_F(SelfHostTest, CustomIteratorCountdown) {
    expectOutput(R"--(
import core::iterator
struct Counter { var n: i32 }
impl Iterator for Counter {
    type Item = i32
    mut func next() -> i32? {
        if self.n <= 0 { return nil }
        self.n -= 1
        return self.n + 1
    }
}
func main() {
    for x in Counter(n: 3) { println(x) }
}
)--", "3\n2\n1\n");
}
```

- [ ] **Step 2: Build and run; expect FAIL**

Run: `ctest --test-dir build-clang -R "SelfHostTest.CustomIteratorCountdown" --output-on-failure`
Expected: FAIL — IRGen still calls a mangled `Counter_next` function that won't match the new protocol-method dispatch path.

- [ ] **Step 3: Refactor the IRGen custom branch**

In `src/IR/IRGenStmt.cpp:visitForStmt`, replace the body of the existing custom-iterator branch (the block that currently emits a call to `<StructName>_next`) with this shape:

```cpp
case IterationKind::CustomIterator: {
    // var it = expr; (alloca + store)
    auto *iterVal = visit(node->getIterable());
    auto *iterTy = iterVal->getType();
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *itAlloca = createEntryBlockAlloca(func, "it", iterTy);
    builder_->CreateStore(iterVal, itAlloca);

    auto *condBB = llvm::BasicBlock::Create(*context_, "for.cond", func);
    auto *bodyBB = llvm::BasicBlock::Create(*context_, "for.body", func);
    auto *exitBB = llvm::BasicBlock::Create(*context_, "for.exit", func);

    builder_->CreateBr(condBB);
    builder_->SetInsertPoint(condBB);

    // Dispatch to Iterator::next. Devirtualize when concrete type known via
    // vars_.varConcreteProtocolTypes (populated by P1-7); else use vtable.
    llvm::Value *optVal = emitProtocolMethodCall(
        /*proto=*/"Iterator",
        /*method=*/"next",
        /*receiverPtr=*/itAlloca,
        /*receiverConcreteType=*/typeName,
        /*args=*/{});
    // optVal is { i1 hasVal, T item } — split:
    auto *hasVal = builder_->CreateExtractValue(optVal, {0}, "has");
    auto *payload = builder_->CreateExtractValue(optVal, {1}, "item");

    builder_->CreateCondBr(hasVal, bodyBB, exitBB);

    // body
    builder_->SetInsertPoint(bodyBB);
    auto *loopVarAlloca = createEntryBlockAlloca(func, node->getLoopVar(), payload->getType());
    builder_->CreateStore(payload, loopVarAlloca);
    vars_.namedValues[node->getLoopVar()] = loopVarAlloca;

    loopStack_.push_back({exitBB, condBB});
    visit(node->getBody());
    loopStack_.pop_back();

    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(condBB);

    builder_->SetInsertPoint(exitBB);
    break;
}
```

If `emitProtocolMethodCall` doesn't already exist as a helper, search for the existing protocol-method dispatch path (e.g. for trait-object method calls on `dyn Protocol`) and extract a helper:

```bash
grep -n "vtable" F:/Cpp_Projects/liva-lang/src/IR/IRGenCall.cpp | head
grep -n "varConcreteProtocolTypes" F:/Cpp_Projects/liva-lang/src/IR/IRGenCall.cpp | head
```

The helper takes (protocol name, method name, receiver pointer, optional concrete type for devirt, args) and returns the call's LLVM Value. Reuse the existing devirt+vtable logic — do NOT duplicate it.

- [ ] **Step 4: Run; expect PASS**

Run: `ctest --test-dir build-clang -R "SelfHostTest.CustomIteratorCountdown" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Run all tests (regression)**

Run: `ctest --test-dir build-clang -j 1 2>&1 | tail -3`
Expected: 2256/2256.

- [ ] **Step 6: Commit**

```bash
git add src/IR/IRGenStmt.cpp tests/unit/SelfHostTest.cpp
git commit -m "irgen: dispatch for-in over custom Iterator via protocol method"
```

---

## Task 11: Runtime — empty iterator, exhaustion fused contract

**Files:**
- Test: `tests/unit/SelfHostTest.cpp` (extend)

- [ ] **Step 1: Write the test**

Append to `tests/unit/SelfHostTest.cpp`:

```cpp
TEST_F(SelfHostTest, CustomIteratorEmpty) {
    expectOutput(R"--(
import core::iterator
struct Empty { }
impl Iterator for Empty {
    type Item = i32
    mut func next() -> i32? { return nil }
}
func main() {
    for x in Empty() { println("never") }
    println("done")
}
)--", "done\n");
}
```

- [ ] **Step 2: Build and run; expect PASS**

Task 10's loop pattern correctly skips the body when `next()` returns nil on the first call.

Run: `ctest --test-dir build-clang -R "SelfHostTest.CustomIteratorEmpty" --output-on-failure`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/SelfHostTest.cpp
git commit -m "test: empty custom Iterator runs zero iterations"
```

---

## Task 12: Runtime — generic `sum<I: Iterator>` over multiple types

**Files:**
- Test: `tests/unit/SelfHostTest.cpp` (extend)

- [ ] **Step 1: Write the test**

```cpp
TEST_F(SelfHostTest, GenericSumOverIterator) {
    expectOutput(R"--(
import core::iterator
struct Counter { var n: i32 }
impl Iterator for Counter {
    type Item = i32
    mut func next() -> i32? {
        if self.n <= 0 { return nil }
        self.n -= 1
        return self.n + 1
    }
}
func sum<I>(iter: I) -> i32 where I: Iterator, I.Item == i32 {
    var total = 0
    var it = iter
    while let x = it.next() { total += x }
    return total
}
func main() {
    println(sum(Counter(n: 4)))
}
)--", "10\n");
}
```

- [ ] **Step 2: Build and run; expect PASS**

Run: `ctest --test-dir build-clang -R "SelfHostTest.GenericSumOverIterator" --output-on-failure`

- If PASS: monomorphization correctly instantiates the body's `it.next()` against `Counter::next`. Continue.
- If FAIL with an unresolved-symbol error: the constraint-solver patch from Task 8 needs to be applied (or extended) in IRGen's monomorphization path, not just Sema. Inspect the IR for the monomorphized `sum_Counter` function and ensure the `it.next()` call lowers to `Counter_next` (mangled) or to a vtable call. If neither is happening, search `IRGenMono.cpp` for where method calls on generic params are resolved and patch it analogously to Task 8's Sema work.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/SelfHostTest.cpp src/Sema/TypeChecker.cpp src/IR
git commit -m "test+impl: generic sum<I: Iterator> with Item == i32"
```

---

## Task 13: IRGen — for-await loop over custom AsyncIterator

**Files:**
- Modify: `src/IR/IRGenStmt.cpp:visitForStmt` (add new branch)
- Test: `tests/unit/SelfHostTest.cpp` (extend)

- [ ] **Step 1: Write the failing runtime test**

```cpp
TEST_F(SelfHostTest, ForAwaitOverAsyncIterator) {
    expectOutput(R"--(
import core::iterator
import async::async
struct Pings { var left: i32 }
impl AsyncIterator for Pings {
    type Item = i32
    mut func next() async -> i32? {
        if self.left <= 0 { return nil }
        self.left -= 1
        return self.left
    }
}
async func run() {
    for await x in Pings(left: 3) { println(x) }
}
func main() {
    runBlocking(run())
}
)--", "2\n1\n0\n");
}
```

(Adjust `runBlocking` / async entrypoint to whatever the existing async tests use — check `SelfHostTest` for the async test setup style; do not invent a new pattern.)

- [ ] **Step 2: Build and run; expect FAIL**

Run: `ctest --test-dir build-clang -R "SelfHostTest.ForAwaitOverAsyncIterator" --output-on-failure`
Expected: FAIL — IRGen has no for-await branch for custom AsyncIterator yet.

- [ ] **Step 3: Implement the for-await branch**

In `src/IR/IRGenStmt.cpp:visitForStmt`, add:

```cpp
case IterationKind::CustomAsyncIterator: {
    auto *iterVal = visit(node->getIterable());
    auto *iterTy = iterVal->getType();
    auto *func = builder_->GetInsertBlock()->getParent();
    auto *itAlloca = createEntryBlockAlloca(func, "it", iterTy);
    builder_->CreateStore(iterVal, itAlloca);

    auto *condBB = llvm::BasicBlock::Create(*context_, "forawait.cond", func);
    auto *bodyBB = llvm::BasicBlock::Create(*context_, "forawait.body", func);
    auto *exitBB = llvm::BasicBlock::Create(*context_, "forawait.exit", func);

    builder_->CreateBr(condBB);
    builder_->SetInsertPoint(condBB);

    // Dispatch to AsyncIterator::next, which returns LivaTask*.
    llvm::Value *taskHandle = emitProtocolMethodCall(
        /*proto=*/"AsyncIterator",
        /*method=*/"next",
        /*receiverPtr=*/itAlloca,
        /*receiverConcreteType=*/typeName,
        /*args=*/{});

    // Suspend until the task completes; reuse visitAwaitExpr's IR pattern.
    llvm::Value *optVal = emitAwait(taskHandle);  // returns Optional<Item>

    auto *hasVal = builder_->CreateExtractValue(optVal, {0}, "has");
    auto *payload = builder_->CreateExtractValue(optVal, {1}, "item");

    builder_->CreateCondBr(hasVal, bodyBB, exitBB);

    // body
    builder_->SetInsertPoint(bodyBB);
    auto *loopVarAlloca = createEntryBlockAlloca(func, node->getLoopVar(), payload->getType());
    builder_->CreateStore(payload, loopVarAlloca);
    vars_.namedValues[node->getLoopVar()] = loopVarAlloca;

    loopStack_.push_back({exitBB, condBB});
    visit(node->getBody());
    loopStack_.pop_back();

    if (!builder_->GetInsertBlock()->getTerminator())
        builder_->CreateBr(condBB);

    builder_->SetInsertPoint(exitBB);
    break;
}
```

If `emitAwait` is not already a helper, extract it from the existing `visitAwaitExpr` body — do NOT duplicate `coro.suspend` + resume IR; reuse it. Search:

```bash
grep -n "visitAwaitExpr\|coro.suspend" F:/Cpp_Projects/liva-lang/src/IR/IRGenExpr.cpp
```

- [ ] **Step 4: Run; expect PASS**

Run: `ctest --test-dir build-clang -R "SelfHostTest.ForAwaitOverAsyncIterator" --output-on-failure`
Expected: PASS, output `2\n1\n0\n`.

- [ ] **Step 5: Run all tests (regression)**

Run: `ctest --test-dir build-clang -j 1 2>&1 | tail -3`
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add src/IR/IRGenStmt.cpp src/IR/IRGenExpr.cpp tests/unit/SelfHostTest.cpp
git commit -m "irgen: for-await loop over custom AsyncIterator via protocol dispatch"
```

---

## Task 14: Runtime — nested iteration, built-in receivers in generic body

**Files:**
- Test: `tests/unit/SelfHostTest.cpp` (extend)

- [ ] **Step 1: Add tests**

```cpp
TEST_F(SelfHostTest, NestedCustomIteration) {
    expectOutput(R"--(
import core::iterator
struct Counter { var n: i32 }
impl Iterator for Counter {
    type Item = i32
    mut func next() -> i32? {
        if self.n <= 0 { return nil }
        self.n -= 1
        return self.n + 1
    }
}
func main() {
    for x in Counter(n: 2) {
        for y in Counter(n: 2) {
            println(x * 10 + y)
        }
    }
}
)--", "21\n22\n11\n12\n");
    // Note: Counter counts down — outer x = 2, then inner produces 2,1; outer x = 1, then inner 2,1.
}

TEST_F(SelfHostTest, GenericSumWithBuiltinDynArray) {
    expectOutput(R"--(
import core::iterator
func sum<I>(iter: I) -> i32 where I: Iterator, I.Item == i32 {
    var total = 0
    for x in iter { total += x }
    return total
}
func main() {
    let arr: [i32] = [1, 2, 3, 4]
    println(sum(arr))
}
)--", "10\n");
}
```

The second test exercises the "built-in receiver in generic body uses for-in" branch from the resolved open question. The body uses `for x in iter`, so monomorphization recognizes `iter`'s type as `[i32]` (DynArray) and emits the built-in iteration path inline rather than calling `<DynArray>_next`.

- [ ] **Step 2: Build and run; expect PASS or FAIL**

Run: `ctest --test-dir build-clang -R "SelfHostTest.NestedCustomIteration|SelfHostTest.GenericSumWithBuiltinDynArray" --output-on-failure`

- If both PASS: monomorphization already recognises built-in types. Continue.
- If `GenericSumWithBuiltinDynArray` fails: in `IRGenMono.cpp` (or wherever for-in is emitted in monomorphized bodies), branch on the monomorphized receiver type — if it matches a built-in iterable name, emit the built-in iteration path; otherwise emit the protocol-method dispatch path. This is the same logic as the top-level `visitForStmt`, just in the monomorphized context.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/SelfHostTest.cpp src/IR/IRGenMono.cpp src/IR/IRGenStmt.cpp
git commit -m "test+impl: nested custom iteration + generic sum over DynArray"
```

---

## Task 15: Doc updates

**Files:**
- Modify: `docs/COOKBOOK.md`
- Modify: `docs/tr/COOKBOOK.md`
- Modify: `docs/LANGUAGE-REFERENCE.md`

- [ ] **Step 1: Find the existing custom-Iter example in COOKBOOK**

```bash
grep -n -i "iter" F:/Cpp_Projects/liva-lang/docs/COOKBOOK.md
```

- [ ] **Step 2: Replace it with an `Iterator` impl**

Open `docs/COOKBOOK.md`, find the section that currently shows the manual `<StructName>_next()` pattern, and rewrite it as:

```markdown
### Custom Iterator

Conform to `core::iterator::Iterator` to make a type usable with `for-in`:

\```liva
import core::iterator

struct Counter { var n: i32 }

impl Iterator for Counter {
    type Item = i32
    mut func next() -> i32? {
        if self.n <= 0 { return nil }
        self.n -= 1
        return self.n + 1
    }
}

func main() {
    for x in Counter(n: 3) { println(x) }   // 3, 2, 1
}
\```
```

(Use real triple-backticks in the doc — escaped above.)

Mirror the same change to `docs/tr/COOKBOOK.md` (translate the prose; keep the code identical).

- [ ] **Step 3: Append a section to LANGUAGE-REFERENCE.md**

Find the protocols section (`grep -n "## Protocol" docs/LANGUAGE-REFERENCE.md`) and add a subsection describing the two new stdlib protocols, their signatures, the for-in / for-await desugar, and the built-in implicit-conformance note.

- [ ] **Step 4: Commit**

```bash
git add docs/COOKBOOK.md docs/tr/COOKBOOK.md docs/LANGUAGE-REFERENCE.md
git commit -m "docs: document Iterator + AsyncIterator protocols"
```

---

## Task 16: Final regression + status update

**Files:**
- Modify: `status.md` (or whatever the project status doc is — check repo root)

- [ ] **Step 1: Full test run**

Run: `ctest --test-dir build-clang -j 1 2>&1 | tail -5`
Expected: 100% pass; total = 2251 (baseline) + new tests added across Tasks 6–14. Confirm the count.

- [ ] **Step 2: Update status doc**

If `status.md` (or similar) lists completed milestones, add an entry for P1-9. Match the existing format.

- [ ] **Step 3: Commit**

```bash
git add status.md
git commit -m "docs(status): mark P1-9 (Iterator/AsyncIterator protocol) complete"
```

---

## Self-Review

**Spec coverage:**
- Goals (Iterator + AsyncIterator stdlib protocols, hybrid dispatch, for-in / for-await routing, built-in implicit conformance, foundation for P1-8): all covered (Tasks 2–14).
- Non-goals (Stream lazy refactor, IntoIterator, HKTB, Channel-as-AsyncIterator, specialization): explicitly skipped — no task addresses them.
- Diagnostics (`err_for_in_not_iterable`, `err_for_await_requires_async_iterator`, `err_iterator_next_signature`): added in Task 1, exercised in Tasks 7 and 9. `err_iterator_next_signature` is registered but no task adds a negative test for it — added at Task 7's expansion if Sema's impl-block validation needs to emit it; otherwise it's a defensive diagnostic that may sit unused.
- Migration of `"Iter"` → `"Iterator"`: Task 4.
- Renaming `iteratorElementTypes_` → `iteratorItemTypes_`: Task 3.
- Test categories (Sema parse/conformance, for-in resolution, for-await, generic constraint, IRGen golden, runtime end-to-end, regression): each represented.
- Coverage target ≥ 80 % new branches: not directly enforced by a task, but Tasks 7, 9, 11, 12, 14 collectively cover positive + negative + edge paths.

**Placeholder scan:** None of the steps say "TBD", "implement later", or describe behavior without showing the code shape. Conditional steps (Task 8 Step 3, Task 12 Step 3, Task 14 Step 2) make the conditional explicit — "if the previous step failed, do this concrete patch in this concrete file" — rather than waving at it.

**Type/identifier consistency:**
- `iteratorItemTypes_` and `asyncIteratorItemTypes_` are introduced in Task 3 and used in Tasks 6 and 9 — same names.
- `IterationKind::CustomIterator` introduced in Task 6, `CustomAsyncIterator` in Task 9.
- `emitProtocolMethodCall` and `emitAwait` referenced in Tasks 10 and 13 are framed as "find or extract" — engineer-instructed to reuse existing logic rather than inventing new helpers, so the lack of an exact pre-existing definition is intentional.
- `protocolConformances_["Iterator"]` and `["AsyncIterator"]` consistent across Tasks 4, 5, 6, 9.

No fixes needed.

---

## Verification (whole feature, end of all tasks)

- `cmake --build build-clang --parallel` → no errors
- `ctest --test-dir build-clang -j 1` → 100 % pass; new tests visible in `IteratorProtocolTest` and `SelfHostTest` runs
- Smoke test: a hand-written program using `CountdownIter`, `for x in [1,2,3]`, and a small `for await` loop produces correct output when compiled with `livac` and executed
- `git log --oneline` shows ~16 small commits, each with passing tests, in the order above
