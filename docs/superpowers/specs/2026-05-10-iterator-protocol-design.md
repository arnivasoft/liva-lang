# Iterator / AsyncIterator Protocol — Design Spec

**Status:** Draft for review
**Date:** 2026-05-10
**Roadmap item:** P1-9 (see `plans/projeyi-detayl-incele-ve-iterative-tome.md`)
**Scope:** Sync `Iterator` + async `AsyncIterator` protocols, hybrid for-in / for-await dispatch.

## Context

Liva-Lang currently handles `for x in expr` via hardcoded type switches in
`IRGenStmt::visitForStmt` (DynArray, Map, Set, Range, Generator) plus a
weakly-defined custom-type fallback that calls a mangled `<TypeName>_next()`
function. Sema records this fallback under the hardcoded protocol name
`"Iter"` (`TypeChecker.cpp:1690`) but no `Iter` protocol is actually defined
in stdlib — it is a string convention, not a type.

This blocks P1-8 (HashMap/BTreeMap/HashSet/BTreeSet stdlib collections):
those collections need a uniform iteration story, including chainable
operations (`map`, `filter`) and async streaming. It also leaves `for await`
(parsed and async-context-validated, but not implemented for any type other
than `Generator`) effectively dead.

This spec defines two stdlib protocols — `Iterator` and `AsyncIterator` —
and refactors for-in / for-await to dispatch through them while keeping the
existing built-in fast paths.

## Goals

- Define `protocol Iterator` and `protocol AsyncIterator` in `stdlib/core/iterator.liva`.
- Refactor the custom-type for-in fallback to dispatch through the new
  `Iterator` protocol (replaces the `"Iter"` string convention).
- Implement `for await x in expr` end-to-end against `AsyncIterator`.
- Register built-in iterables (DynArray, Map, Set, Range, Generator) as
  implicit conformers of `Iterator` so that `where T: Iterator` accepts them
  in generic constraints — without changing the generated IR for built-in
  iteration.
- Provide a foundation for P1-8 (generic collections) and a future lazy
  refactor of `Stream<T>` (out of scope here).

## Non-Goals

- `Stream<T>` lazy refactor (eager `filter` / `map` / `collect` stay).
- `IntoIterator` protocol — `for x in expr` requires `expr` itself to be an
  `Iterator` (or a built-in iterable). No `intoIter()` chain.
- Higher-ranked / lifetime-polymorphic iterator bounds.
- Channel as an implicit `AsyncIterator` (out of scope; users wrap manually).
- Specialization or coherence rules for overlapping `Iterator` impls.

## Architecture Overview

| Layer       | Change                                                            |
|-------------|-------------------------------------------------------------------|
| Stdlib      | New file `stdlib/core/iterator.liva` with two protocols.          |
| AST         | None. `ProtocolDecl` + `AssociatedTypeDecl` already sufficient.   |
| Sema        | Built-in implicit conformance registration; for-in / for-await type checking routes through `protocolConformances_["Iterator"]` and `["AsyncIterator"]`. The hardcoded `"Iter"` string and `iteratorElementTypes_` map are renamed/redirected. |
| IRGen       | Built-in switches in `visitForStmt` retained. Custom-type branch refactored to protocol-method dispatch. New for-await branch emits `await it.next()` using the existing coroutine pattern. |
| Diagnostics | New: `err_for_in_not_iterable`, `err_for_await_requires_async_iterator`, `err_iterator_next_signature`. Renamed: `err_for_in_not_iterable` replaces the old hardcoded-Iter error path. |

## Protocol API

`stdlib/core/iterator.liva` (new):

```liva
/// A type that produces a sequence of values, one at a time.
///
/// Conformers implement `next()`, which returns the next element or `nil`
/// when exhausted. After `nil` is returned, subsequent calls must continue
/// to return `nil` (the iterator is "fused"). The fused contract is
/// documented but not enforced by the compiler.
pub protocol Iterator {
    type Item

    /// Advance the iterator and return the next element, or `nil` if done.
    mut func next() -> Item?
}

/// A type that produces a sequence of values asynchronously.
///
/// Used with `for await x in source` inside `async` functions. Returns
/// `nil` to signal end of stream.
pub protocol AsyncIterator {
    type Item

    /// Asynchronously advance and return the next element, or `nil`.
    mut func next() async -> Item?
}
```

### Design rationale

1. **`mut func next()`** — iteration mutates the iterator's internal cursor.
   Liva's existing `mut` annotation extends to protocol method signatures
   (Sema already parses `mut` on parameters; this spec lifts it to
   `mut func` on protocol methods, which only affects `self`).

2. **`type Item`** — uses the existing `AssociatedTypeDecl` (`Decl.h:376-380`)
   with empty `lifetimeParams_` and `typeParams_`. This is a plain
   associated type, not a GAT, so no new AST.

3. **`Item?` return** — Optional<T> is the existing end-of-stream signal.
   The desugar pattern `while let x = it.next() { body }` already works in
   Liva, so IRGen reuses the optional-unwrapping IR path.

4. **Fused contract** — documented in the protocol docstring; no Sema or
   runtime check. Conformers that violate it produce undefined iteration
   length, but no UB.

5. **`async func next()`** — reuses the existing async/coroutine
   infrastructure (`currentCoroTask_`, `coro.suspend`).

### Usage example

```liva
struct CountdownIter {
    var n: i32
}

impl Iterator for CountdownIter {
    type Item = i32
    mut func next() -> i32? {
        if self.n <= 0 { return nil }
        self.n -= 1
        return self.n + 1
    }
}

// Direct for-in
for x in CountdownIter(n: 3) { println(x) }   // 3, 2, 1

// Generic constraint
func sum<I: Iterator>(iter: I) -> i32 where I::Item == i32 {
    var total = 0
    var it = iter
    while let x = it.next() { total += x }
    return total
}
```

## Hybrid Dispatch

### Sema (`src/Sema/TypeChecker.cpp`)

#### `for x in expr`

Refactor of the resolution loop in `visitForStmt` (lines 1585–1713):

1. **Built-in iterable check** (existing paths preserved):
   - `Array` / `DynArray` → element type from `ArrayTypeRepr::getElement()`.
   - `Map<K,V>` → tuple element `(K, V)`.
   - `Set<T>` → element `T`.
   - `Range` → `i32` (or matched range integer type).
   - `Generator<T>` → element `T`.
   - In each case, also synthesize an `Iterator` conformance entry (so
     generics see them as iterable; see "Built-in implicit conformance"
     below).

2. **Custom type check:**
   - If the iterable's resolved type is a struct/class `T` and
     `protocolConformances_["Iterator"]` contains `T`, look up the element
     type in `iteratorItemTypes_[T]` (renamed from `iteratorElementTypes_`)
     and use it as the loop variable's type.

3. **Failure:** emit `err_for_in_not_iterable` (new diagnostic; the existing
   error becomes this one).

#### `for await x in expr`

1. **Async context check** (existing, line 1587): unchanged.
2. **Type check:**
   - If `T` is `Generator<U>` → element `U` (already async-capable).
   - Else if `protocolConformances_["AsyncIterator"]` contains `T` → element
     from `asyncIteratorItemTypes_[T]`.
   - Else → emit `err_for_await_requires_async_iterator`.

#### Built-in implicit conformance

In `Sema::init()` (or equivalent bootstrap), seed:

```cpp
protocolConformances_["Iterator"]      += {"Range", "Array", "DynArray", "Map", "Set", "Generator"};
protocolConformances_["AsyncIterator"] += {"Generator"};
```

These are synthetic — IRGen still uses the hardcoded built-in path; the
entries exist purely so that `where T: Iterator` accepts these types and
generic monomorphization can call `T::next` against them via the synthetic
Item entries in `iteratorItemTypes_` / `asyncIteratorItemTypes_`.

For built-in types in generic positions, IRGen recognizes the receiver type
and dispatches to the built-in iteration shape rather than synthesising a
`<BuiltIn>_next` function.

#### Migration of `"Iter"` → `"Iterator"`

- `TypeChecker.cpp:1690` and any other `"Iter"` literal references the new
  `"Iterator"` name.
- `iteratorElementTypes_` is renamed `iteratorItemTypes_` (cosmetic, but
  keeps the field name consistent with the protocol's `Item`).
- The docs/cookbook example for "Custom Iter" is rewritten as
  `impl Iterator for ...`.

### IRGen (`src/IR/IRGenStmt.cpp:visitForStmt`)

#### Built-in path (lines 712–985)

Unchanged. Range, DynArray, Map, Set, and Generator emission paths produce
identical LLVM IR. This is the performance-critical path and must remain
hardcoded; goldens cover regression.

#### Custom Iterator path (replaces lines 987–1029)

Old: direct call to `<StructName>_next()`.

New: protocol-method dispatch.

```text
%it.alloca = alloca <IterType>
store <expr-result>, %it.alloca

br label %loop.cond

loop.cond:
    ; protocol-method dispatch — devirtualized when concrete type known
    %opt = call <Item?> @<Iterator::next mangled or via vtable>(%it.alloca)
    %has = extract-hasVal %opt
    br i1 %has, label %loop.body, label %loop.exit

loop.body:
    %x = extract-payload %opt
    ; bind %x to the loop variable, visit body
    br label %loop.cond

loop.exit:
```

Devirtualization reuses the existing `vars_.varConcreteProtocolTypes` path
(populated by P1-7 / earlier work) when the concrete type is known at the
binding site. Otherwise, vtable dispatch via the `Iterator` protocol's
vtable global (mangled `vtable_Iterator_<TypeName>`).

#### Async iterator path (new)

```text
%it.alloca = alloca <IterType>
store <expr-result>, %it.alloca

br label %loop.cond

loop.cond:
    %task = call ptr @<AsyncIterator::next>(%it.alloca)   ; returns LivaTask*
    %fut.opt = await %task                                ; existing visitAwaitExpr IR
    %has = extract-hasVal %fut.opt
    br i1 %has, label %loop.body, label %loop.exit
...
```

`await` insertion reuses `visitAwaitExpr`'s `coro.suspend` + resume pattern.
`for await` outside an async context is a parse/sema error (already
enforced).

## Test Strategy

### Sema tests (`tests/unit/SemaTest.cpp` + new `IteratorProtocolTest.cpp`)

1. Protocol parse + conformance:
   - Custom struct conforms to `Iterator` (positive).
   - Wrong `next()` signature → `err_iterator_next_signature`.
   - `mut` missing on `next()` in conforming impl → diagnostic.

2. for-in resolution:
   - Built-in iterables resolve element type as before (regression).
   - Custom `Iterator` conformer resolves via `iteratorItemTypes_`.
   - Non-iterable → `err_for_in_not_iterable`.

3. for-await resolution:
   - Sync context → existing `err_for_await_requires_async_context`.
   - Non-`AsyncIterator` in async context → `err_for_await_requires_async_iterator`.
   - `Generator` and custom `AsyncIterator` resolve correctly.

4. Generic constraints:
   - `func sum<I: Iterator>(...)` parses and type-checks.
   - `where I::Item == i32` is honored.
   - Built-in implicit conformance: `sum([1,2,3])` (DynArray) compiles.

### IRGen tests (`tests/unit/IRGenTest.cpp`)

5. Built-in regression (golden IR comparison): no diff for existing for-in
   over DynArray / Map / Set / Range / Generator.
6. Custom `Iterator`: `next()` dispatch IR matches the documented shape;
   devirtualizes when concrete type is bound.
7. Custom `AsyncIterator`: `await it.next()` lowers to `coro.suspend` /
   resume in the loop header.

### Runtime tests (`tests/unit/SelfHostTest.cpp`)

8. `CountdownIter` — full sequence + termination.
9. Empty iterator — `next()` returns nil on first call, body never runs.
10. Iterator after exhaustion — `next()` keeps returning nil.
11. `sum<I: Iterator>` invoked with `Range(0..10)`, `[1,2,3]`, and
    `CountdownIter` — all produce the expected sums.
12. Nested iteration: `for x in iter1 { for y in iter2 { } }`.
13. Custom `AsyncIterator` (channel-style) drained with `for await`,
    producing the expected sequence with at least one suspend point.

### Regression

14. All 2251 existing tests continue to pass; the rename from `"Iter"` to
    `"Iterator"` is reflected in any test that referenced the literal
    string.

### Coverage targets

- New Sema branches ≥ 80 % covered.
- One negative test per new diagnostic.

## Migration Path

1. Add `stdlib/core/iterator.liva` with the two protocols.
2. Rename `iteratorElementTypes_` → `iteratorItemTypes_` (all sites).
3. Replace `"Iter"` literal references with `"Iterator"`.
4. Add built-in implicit conformance seeding in `Sema::init()`.
5. Refactor IRGen custom-Iter branch to protocol-method dispatch.
6. Implement IRGen for-await loop pattern.
7. Add new diagnostics + tests.
8. Update cookbook / language reference snippets that mentioned `Iter`.

Each step is independent enough to land in its own commit; the new
diagnostics and protocol file should land first so subsequent steps can be
tested incrementally.

## Open Questions / Future Work

- **Associated-type equality constraints (`where I::Item == i32`)**: the
  spec's `sum<I: Iterator>` example assumes Liva already accepts this
  syntax. If it doesn't, the implementation plan must either (a) add it as
  a small Sema extension, or (b) fall back to the equivalent
  `where I: Iterator<Item = i32>` form (if generic-arg-on-protocol is
  supported), or (c) leave constraint-by-equality out of P1-9 and document
  it as a Sema follow-up. The plan must verify this against the current
  parser/Sema before relying on the syntax in tests.

- **Built-in types inside generic bodies**: a generic function
  `func f<I: Iterator>(it: I)` whose body calls `it.next()` works
  trivially for custom conformers (real `next()` dispatch). For a built-in
  receiver (e.g. `f([1,2,3])` with `I = DynArray<i32>`), there is no
  physical `<DynArray>_next` function. Two options for the plan to choose:
  - **(i) For-in only for built-ins:** generic body must use `for x in it`
    (built-in iteration path is intrinsic-emitted at the call site); a
    direct `it.next()` call against a built-in receiver is an error.
  - **(ii) Synthesised next shim:** IRGen emits a small wrapper function
    on demand for built-in receivers used in generic bodies.
  Option (i) is simpler and does not regress non-generic built-in
  iteration; option (ii) is more uniform but adds IRGen complexity. The
  plan should pick one based on how P1-8 collections are intended to be
  used (collections will most often appear in for-in, not direct .next()).

- **`IntoIterator`**: deferred. If P1-8 collections consistently expose an
  `iter()` method that returns an Iterator type, `IntoIterator` may not be
  needed. Revisit after P1-8.
- **Generator ↔ Iterator unification**: today `Generator<T>` is an opaque
  coroutine type with built-in for-in support. Making it a regular
  `Iterator` conformer is appealing but interacts with P1-13 (async
  runtime) — defer.
- **Channel as AsyncIterator**: `stdlib/sync` has `Channel<T>`; future work
  may add `impl AsyncIterator for Channel<T>` so `for await x in channel`
  works without a manual wrapper.
- **Lazy `Stream<T>`**: rebuild `stdlib/stream/stream.liva` on top of
  `Iterator` to make `filter` / `map` chains lazy. Significant refactor;
  separate spec.

## File Inventory (delta)

**New:**
- `stdlib/core/iterator.liva`
- `tests/unit/IteratorProtocolTest.cpp` (or section in `SemaTest.cpp`)

**Modified:**
- `src/Sema/TypeChecker.cpp` — for-in / for-await resolution; rename map.
- `src/Sema/Sema.cpp` (or wherever `init()` lives) — built-in conformance.
- `include/liva/Sema/TypeChecker.h` — field rename.
- `src/IR/IRGenStmt.cpp` — custom-Iter dispatch refactor; for-await loop.
- `include/liva/Common/DiagnosticKinds.def` — new diagnostics.
- `tests/unit/SemaTest.cpp`, `IRGenTest.cpp`, `SelfHostTest.cpp` — added cases.
- `docs/COOKBOOK.md` (and `docs/tr/COOKBOOK.md`) — replace `Iter` example.
- `docs/LANGUAGE-REFERENCE.md` — protocol section addition.

## Verification

- `cmake --build build-clang --parallel` → no errors.
- `ctest --test-dir build-clang -j 1` → 100 % pass; new test count
  ≥ 2251 + (new tests).
- Manual smoke test: build a small program using each of `CountdownIter`,
  `for x in [1,2,3]` (regression), and a small async iterator drained with
  `for await`.
