# Generator/Yield Runtime Maturation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Take the Generator/yield feature from "sema-only, IRGen wired but dormant" to a full state machine with end-to-end runtime tests, without regressing the async path.

**Architecture:**
- Generator and Async functions today share `Phase 2 Coroutine Ramp` setup (`src/IR/IRGenDecl.cpp:429-497`). Async is *eager* (Task starts running on call); Generator is supposed to be *lazy* (caller drives via `coro.resume`). The shared ramp accidentally produces eager generators — but more importantly, no end-to-end test exists, so neither path is proven at runtime.
- We will: (a) build a minimal e2e test harness that compiles `.liva` → executable → runs → asserts stdout, (b) verify a hand-written reference coroutine passes through Liva's CodeGen pipeline (proves CoroSplit is alive), (c) wire `Generator<T>` dispatch in Sema's `visitForStmt`, (d) split the generator path from async with a `currentIsGenerator_` flag and lazy initial-suspend semantics, (e) run a `for x in countTo(3)` end-to-end test producing `0\n1\n2\n`.

**Tech Stack:** C++20, LLVM 21 coroutine intrinsics (`coro.id/alloc/begin/suspend/promise/resume/done/free/end`), CoroSplit/CoroEarly/CoroElide LLVM passes, GoogleTest, CMake/Ninja, Clang.

**Critical context discovered during exploration:**
- `src/IR/IRGenDecl.cpp:258, 270-273, 381, 430` — current shared ramp uses `node->isAsync() || node->isGenerator()` everywhere; the only generator-specific branch is `generatorFuncs_` map registration and default `i32` promise type.
- `src/IR/IRGenStmt.cpp:625-707` — for-in's generator detection looks up the *callee identifier name* in `generatorFuncs_`, which means generator-typed variables (`let g = countTo(3); for x in g`) are unsupported — must dispatch via `Generator<T>` Sema type instead.
- `src/Sema/TypeChecker.cpp:1585-1705` — `visitForStmt` knows about Array/Map/Set/Iter-protocol but **not** `Generator<T>`. Loop variable type for `for x in countTo(3)` is currently undefined.
- `src/Sema/TypeChecker.cpp:2669-2691` — Sema wraps generator call return type in `Generator<T>`, defaulting `T` to `i32` when the function has no declared return type.
- `src/CodeGen/CodeGen.cpp:167-222` — uses `PassBuilder::buildPerModuleDefaultPipeline(level)`. CoroSplit/CoroEarly are added by default at `O0` and above per LLVM 16+, but we will explicitly verify by inspecting the post-optimization IR.
- No existing test compiles `.liva` to a binary and executes it. CodeGenTest stops at IR; IntegrationTest stops at sema.
- Current commit: `8387ee0 Generator/yield: wire up Generator<T> sema type and for-in IRGen path`. Branch: `master`. Build dir: `build-clang/`. Baseline: 2064 tests passing (Clang build).

---

## File Structure

| File | Responsibility | Action |
|------|----------------|--------|
| `tests/unit/RuntimeExecTest.cpp` | New test fixture: compile `.liva` → executable → run → capture stdout/exit | Create |
| `tests/CMakeLists.txt` | Register new test target | Modify |
| `tests/data/runtime/hello_async.liva` | Minimal async reference fixture | Create |
| `tests/data/runtime/count_to_three.liva` | Generator e2e fixture | Create |
| `include/liva/IR/IRGen.h` | Add `currentIsGenerator_` flag and `currentCoroInitialSuspendBB_` | Modify |
| `src/IR/IRGenDecl.cpp` | Split generator/async ramp paths; add lazy initial suspend for generators | Modify |
| `src/IR/IRGenStmt.cpp` | Replace name-based generator detection with `Generator<T>` type dispatch | Modify |
| `src/Sema/TypeChecker.cpp` | Add `Generator<T>` case to `visitForStmt` element-type resolution | Modify |
| `src/CodeGen/CodeGen.cpp` | Verify (and if needed, explicitly add) CoroEarly/CoroSplit/CoroElide passes | Modify |

---

## Task 1: Create runtime e2e test harness

**Goal:** Build a fixture that takes a `.liva` source string, compiles it to an executable using the in-tree `livac` driver (or direct CodeGen→link), runs it, and asserts on stdout and exit code. We need this *before* any generator change so we can prove behavior, not infer it.

**Files:**
- Create: `tests/unit/RuntimeExecTest.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Inspect how `livac` (or the test pipeline) currently goes from `.liva` to native binary**

Run:
```
grep -rn "emitObjectFile\|llvm-link\|linkObjectsToExe\|Linker::" src/ tools/ | head
```
Expected: locate the existing object→executable wiring. If none exists in tests, look for it in the `livac` driver under `tools/`. We will *call into* it from the test, not duplicate its logic.

- [ ] **Step 2: Read the driver's compile-to-exe entry point**

Read `tools/livac/main.cpp` (or equivalent). Identify the smallest API surface that takes (source path, output exe path) and returns success/failure plus the linker command actually used. Write down the function signature.

- [ ] **Step 3: Write the failing test**

Create `tests/unit/RuntimeExecTest.cpp`:

```cpp
#include "liva/Common/Diagnostics.h"
#include "liva/CodeGen/CodeGen.h"
#include "liva/IR/IRGen.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#define POPEN  _popen
#define PCLOSE _pclose
#define EXE_SUFFIX ".exe"
#else
#define POPEN  popen
#define PCLOSE pclose
#define EXE_SUFFIX ""
#endif

namespace {
struct RunResult {
    int exit_code;
    std::string stdout_output;
};

// Compile a Liva source string to an executable, run it, capture stdout and exit code.
// On any compilation failure, exit_code is set to -1 and stdout_output contains diagnostics.
RunResult compileAndRun(const std::string &source, const std::string &test_name);
}  // namespace

TEST(RuntimeExecTest, HelloWorld_PrintsAndExitsZero) {
    auto r = compileAndRun(
        "func main() { println(\"hello\") }\n",
        "hello_world");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("hello"), std::string::npos)
        << "stdout: " << r.stdout_output;
}
```

The body of `compileAndRun` is the harness — it must mirror exactly what `livac` does: `Lexer → Parser → Sema → IRGen → CodeGen.emitObjectFile → link with runtime.lib → execute via popen`. Use the function signature you wrote down in Step 2; if `livac`'s flow is wrapped in a single callable, call it directly. If it isn't, factor out a `Driver::compileToExe(source, outPath, diag)` helper in `src/Driver/` (small refactor, ~40 lines) and call that. Do **not** reimplement linking inside the test.

- [ ] **Step 4: Add to `tests/CMakeLists.txt`**

```cmake
add_executable(RuntimeExecTest unit/RuntimeExecTest.cpp)
target_link_libraries(RuntimeExecTest PRIVATE liva_core liva_codegen GTest::gtest_main)
target_compile_definitions(RuntimeExecTest PRIVATE
    LIVA_RUNTIME_LIB="${CMAKE_BINARY_DIR}/stdlib/runtime/liva_runtime.lib")
gtest_discover_tests(RuntimeExecTest)
```

(Adjust target/lib names to whatever the project actually uses — copy from `IntegrationTest`'s registration as a model.)

- [ ] **Step 5: Build and run, expect FAIL**

```
cmake --build build-clang --target RuntimeExecTest
ctest --test-dir build-clang -R RuntimeExecTest --output-on-failure
```

Expected: FAIL because `compileAndRun` is declared but not defined.

- [ ] **Step 6: Implement `compileAndRun`**

```cpp
namespace {
RunResult compileAndRun(const std::string &source, const std::string &test_name) {
    using namespace liva;
    DiagnosticsEngine diag;
    Lexer lex(source, "<test>", diag);
    auto tokens = lex.tokenize();
    Parser parser(tokens, diag);
    auto program = parser.parseProgram();
    if (diag.hasErrors()) return {-1, diag.formatAll()};

    Sema sema(diag);
    sema.analyze(*program);
    if (diag.hasErrors()) return {-1, diag.formatAll()};

    IRGen irgen(diag);
    auto module = irgen.generate(*program);
    if (!module || diag.hasErrors()) return {-1, diag.formatAll()};

    TargetInfo ti = TargetInfo::getHostTarget();
    CodeGen cg(diag, ti);
    std::string obj_path  = test_name + ".o";
    std::string exe_path  = test_name + EXE_SUFFIX;
    if (!cg.emitObjectFile(*module, obj_path)) return {-1, "emit failed"};

    // Link against runtime
    std::string link_cmd = std::string("clang ") + obj_path
        + " " + LIVA_RUNTIME_LIB + " -o " + exe_path;
    if (std::system(link_cmd.c_str()) != 0) {
        std::remove(obj_path.c_str());
        return {-1, "link failed: " + link_cmd};
    }

    // Run
    std::string run_cmd = std::string("./") + exe_path;
    FILE *pipe = POPEN(run_cmd.c_str(), "r");
    if (!pipe) return {-1, "popen failed"};
    std::stringstream out;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) out << buf;
    int status = PCLOSE(pipe);
    std::remove(obj_path.c_str());
    std::remove(exe_path.c_str());
    return {WEXITSTATUS(status), out.str()};
}
}  // namespace
```

(Use the `Driver::compileToExe` helper if you created it in Step 3; this code shows the shape but the exact call should match the project's existing flow. The point is: real lex→parse→sema→irgen→codegen→link, no shortcuts.)

- [ ] **Step 7: Run, expect PASS**

```
cmake --build build-clang --target RuntimeExecTest
ctest --test-dir build-clang -R RuntimeExecTest --output-on-failure
```

Expected: PASS, stdout contains "hello".

- [ ] **Step 8: Commit**

```bash
git add tests/unit/RuntimeExecTest.cpp tests/CMakeLists.txt
git commit -m "test: add e2e runtime harness (compile→link→run→assert stdout)"
```

---

## Task 2: Establish async-runtime baseline

**Goal:** Before touching the generator path, prove the existing **async** path works end-to-end. If async is broken at runtime, we cannot blame our generator changes for any failure.

**Files:**
- Modify: `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Add a failing async test**

Append to `RuntimeExecTest.cpp`:

```cpp
TEST(RuntimeExecTest, AsyncSimple_ReturnsAndPrints) {
    auto r = compileAndRun(R"(
        async func answer() -> i32 { return 42 }
        async func main() {
            let a: i32 = await answer()
            println("got:")
            println(a)
        }
    )", "async_simple");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_NE(r.stdout_output.find("42"), std::string::npos)
        << "stdout: " << r.stdout_output;
}
```

- [ ] **Step 2: Build and run**

```
cmake --build build-clang --target RuntimeExecTest
ctest --test-dir build-clang -R RuntimeExecTest.AsyncSimple --output-on-failure
```

**Two outcomes:**
- **PASS** → async runtime is healthy; CoroSplit pipeline is fine; jump to Task 3.
- **FAIL** → record the failure mode (compile error, link error, segfault, wrong stdout). This becomes the *baseline* for separating "always-broken" from "generator-broken." Do **not** fix async in this task — capture the failure into a `// XFAIL: async runtime` comment with the exact symptom and continue.

- [ ] **Step 3: Commit (regardless of pass/fail)**

```bash
git add tests/unit/RuntimeExecTest.cpp
git commit -m "test: add async runtime baseline (XFAIL noted if failing)"
```

---

## Task 3: Reproduce the generator crash with a minimal test

**Goal:** Capture the "ramp patlıyor" failure as a real, reproducible test case — not a memory of a past crash. We need to know exactly *what* fails (compile? link? validate IR? run?) before we can fix it.

**Files:**
- Modify: `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Add the failing generator test**

```cpp
TEST(RuntimeExecTest, Generator_CountToThree_Yields012) {
    auto r = compileAndRun(R"(
        func countTo(n: i32) {
            var i: i32 = 0
            while i < n {
                yield i
                i = i + 1
            }
        }
        func main() {
            for x in countTo(3) {
                println(x)
            }
        }
    )", "gen_count_three");
    EXPECT_EQ(r.exit_code, 0) << "stdout: " << r.stdout_output;
    EXPECT_EQ(r.stdout_output, "0\n1\n2\n")
        << "stdout: " << r.stdout_output;
}
```

- [ ] **Step 2: Run and capture failure mode**

```
ctest --test-dir build-clang -R RuntimeExecTest.Generator_CountToThree --output-on-failure 2>&1 | tee /tmp/gen-baseline.txt
```

Read `/tmp/gen-baseline.txt`. Classify the failure:
- **Sema error?** → loop variable typing missing → Task 5 fixes this.
- **IR validation error?** ("instruction does not dominate"/"invalid use of token"/etc.) → coroutine ramp construction issue → Task 6 + Task 7.
- **Linker error?** ("undefined reference to llvm.coro.*") → CoroSplit pass didn't run → Task 6 fixes this.
- **Runtime crash?** → state machine semantic issue (eager-vs-lazy) → Task 7.
- **Wrong output?** → promise-read or resume sequencing → Task 7.

Write the classification into a comment in the test:

```cpp
// XFAIL baseline as of <commit hash>: <one-line failure mode>
//   e.g. "linker error: undefined reference to llvm.coro.suspend"
```

- [ ] **Step 3: Commit the captured failure**

```bash
git add tests/unit/RuntimeExecTest.cpp
git commit -m "test: capture generator runtime baseline failure mode"
```

This commit's CI run will fail — that's intentional. It locks in what we're fixing.

---

## Task 4: Inspect the emitted IR and CoroSplit behavior

**Goal:** Before touching code, read the LLVM IR Liva produces for `countTo` and confirm whether CoroSplit ran. This gates whether Task 6 (explicit pass wiring) is needed.

**Files:** (no edits — investigation only)

- [ ] **Step 1: Add an IR-dump test path to the harness**

Temporarily modify `compileAndRun` to write `module->print(*os, nullptr)` to `<test_name>.ll` *before* `emitObjectFile`. Also write a second `.ll` *after* `emitObjectFile` (which runs the optimization pipeline) by capturing `module` again post-optimize. (You may need to expose the post-opt module via a `CodeGen::optimizeAndEmit` split — small, reversible change.)

- [ ] **Step 2: Run the generator test once to dump IR**

```
ctest --test-dir build-clang -R RuntimeExecTest.Generator_CountToThree
ls gen_count_three.pre.ll gen_count_three.post.ll
```

- [ ] **Step 3: Inspect the IR**

```
grep -n "coro\." gen_count_three.pre.ll | head -20
grep -n "coro\." gen_count_three.post.ll | head -20
```

**What to look for:**
- `pre.ll` should have `call token @llvm.coro.id`, `call i8 @llvm.coro.suspend`, etc.
- `post.ll` should have those intrinsics **gone** (replaced by `countTo.resume`, `countTo.destroy`, `countTo.cleanup` functions). If they are still present in `post.ll`, CoroSplit did **not** run.

- [ ] **Step 4: Record the finding**

Add a one-line note to the plan (this file, just below):
```
> **CoroSplit status (Task 4):** [DID RUN | DID NOT RUN] — see <commit>
```

> **CoroSplit status (Task 4): DID NOT RUN.** Diagnostic dump of `gen_count_three.post.ll` (O0 and O2, both pipelines) shows no `countTo.resume`/`countTo.destroy`/`countTo.cleanup` clones, and `@countTo` still contains live `@llvm.coro.suspend(token, i1)`, `@llvm.coro.end(ptr, i1, token)`, and `@llvm.coro.promise(ptr, i32, i1)` calls after `buildPerModuleDefaultPipeline` runs. Root cause: `src/IR/IRGenDecl.cpp:430-497` builds the coroutine ramp (coro.id/alloc/begin/suspend/end) but never sets the `presplitcoroutine` function attribute on the ramp function, so CoroSplit's identification check (`Function::isPresplitCoroutine()`) skips it. CoroEarlyPass *does* run (O0 post.ll: coro.id/alloc/begin/free are lowered to malloc/free), but its lowering also clobbers the promise alloca: `%coro.promise = alloca i32` (irgen) is mid-flight replaced by `%promise.addr = call ptr @llvm.coro.promise(ptr %coro.mem, i32 4, i1 false)`, and existing `store i32 …, ptr %coro.promise` operands are retargeted to the new SSA name without preserving dominance — the IR verifier flags `Instruction does not dominate all uses!` on `%promise.addr`. Backend codegen runs without `-verify` (`CodeGen::emitObjectFile` uses `addPassesToEmitFile`, no module verifier), so the broken IR slips into X86 DAG ISel where `LLVM ERROR: Do not know how to promote this operator's operand!` aborts inside `@countTo`'s SelectionDAG build. Reproduction: `llc -disable-verify gen_count_three.post.ll -o NUL` aborts identically; `opt -passes=verify` rejects the same module. Fix path: Task 6 must add `func->addFnAttr(llvm::Attribute::PresplitCoroutine)` at the start of the Phase 2 ramp setup in `src/IR/IRGenDecl.cpp:430` (and probably also gate CoroEarly's promise-rewrite by *not* pre-allocaing the promise — let CoroSplit own it). Once `presplitcoroutine` is set, CoroSplit will produce the .resume/.destroy/.cleanup clones and the suspend intrinsics will be lowered out before reaching ISel.

- [ ] **Step 5: Revert the IR-dump instrumentation** (it was a debug aid, not a permanent feature)

```bash
git checkout -- tests/unit/RuntimeExecTest.cpp src/CodeGen/CodeGen.cpp
```

(Do not commit the dump code; it pollutes the test harness. The *finding* is what matters.)

---

## Task 5: Wire `Generator<T>` dispatch in Sema's `visitForStmt`

**Goal:** Today, `for x in countTo(3)` cannot type-check `x` because `visitForStmt` (`src/Sema/TypeChecker.cpp:1585-1705`) doesn't recognize `Generator<T>` as iterable. The IRGen for-in code path (`src/IR/IRGenStmt.cpp:625-707`) detects generators by callee *name*, which is fragile and breaks for `let g = countTo(3); for x in g`. Fix Sema first; the IRGen change in Task 7 will mirror the dispatch.

**Files:**
- Modify: `src/Sema/TypeChecker.cpp` (around line 1585)
- Modify: `tests/unit/SemaTest.cpp`

- [ ] **Step 1: Write the failing Sema test**

Add to `tests/unit/SemaTest.cpp` (next to the existing Generator_* tests at line 9367+):

```cpp
TEST_F(SemaTest, Generator_ForInBindsLoopVarType) {
    std::string source = R"(
        func gen() { yield 42 }
        func main() {
            for x in gen() {
                let y: i32 = x
            }
        }
    )";
    auto result = analyze(source);
    EXPECT_FALSE(result.hasErrors())
        << "Sema rejected for-in over Generator<i32>; first error: "
        << (result.errors.empty() ? "none" : result.errors.front().message);
}
```

- [ ] **Step 2: Run, expect FAIL**

```
ctest --test-dir build-clang -R SemaTest.Generator_ForInBindsLoopVarType --output-on-failure
```

- [ ] **Step 3: Locate the dispatch site**

Read `src/Sema/TypeChecker.cpp:1585-1705`. Find the chain that resolves `iterableType` to `elementType`. After the existing Array/Map/Set/Iter cases, add a Generator case. Use Read to find the exact spot:

```
grep -n "iterableType\|elementType" src/Sema/TypeChecker.cpp | head -30
```

- [ ] **Step 4: Add the Generator<T> case**

In `visitForStmt`, after the Iter-protocol block but before the "unsupported iterable" diagnostic, insert:

```cpp
// Generator<T>: bind loop var to T (yielded value type).
if (auto *named = dynamic_cast<const NamedTypeRepr *>(iterableType);
    named && named->getName() == "Generator" && named->getTypeArgs().size() == 1) {
    elementType = named->getTypeArgs()[0].get();
    // Continue with normal loop-var binding using elementType.
}
```

(Adjust API names to match the project — `getTypeArgs()` may be `getGenericArgs()` or similar. Check `src/Sema/TypeChecker.cpp:2675` for how `Generator<T>` is constructed; mirror that shape.)

- [ ] **Step 5: Run, expect PASS**

```
ctest --test-dir build-clang -R SemaTest.Generator_ForInBindsLoopVarType --output-on-failure
```

- [ ] **Step 6: Run the full Sema suite to catch regressions**

```
ctest --test-dir build-clang -R SemaTest --output-on-failure
```

Expected: all previously passing tests still pass; new test now passes.

- [ ] **Step 7: Commit**

```bash
git add src/Sema/TypeChecker.cpp tests/unit/SemaTest.cpp
git commit -m "sema: dispatch for-in over Generator<T> to bind loop var to yielded type"
```

---

## Task 6: Verify (and explicitly wire if needed) coroutine passes

**Goal:** Based on Task 4's finding, ensure CoroEarly + CoroSplit + CoroElide + CoroCleanup are part of the optimization pipeline. If they're already there via `buildPerModuleDefaultPipeline`, this task is a no-op — verify and move on.

**Files:**
- Possibly modify: `src/CodeGen/CodeGen.cpp:167-222`

- [ ] **Step 1: Skip if Task 4 found CoroSplit ran**

If Task 4's note says "DID RUN," skip directly to Step 5 (commit nothing).

- [ ] **Step 2: Add explicit pass registration**

In `src/CodeGen/CodeGen.cpp`, inside `optimize()` after `pb.buildPerModuleDefaultPipeline(level)`:

```cpp
#include <llvm/Transforms/Coroutines/CoroCleanup.h>
#include <llvm/Transforms/Coroutines/CoroEarly.h>
#include <llvm/Transforms/Coroutines/CoroElide.h>
#include <llvm/Transforms/Coroutines/CoroSplit.h>
// ...
mpm.addPass(llvm::CoroEarlyPass());
llvm::CGSCCPassManager cgpm;
cgpm.addPass(llvm::CoroSplitPass());
mpm.addPass(llvm::createModuleToPostOrderCGSCCPassAdaptor(std::move(cgpm)));
mpm.addPass(llvm::CoroElidePass());
mpm.addPass(llvm::CoroCleanupPass());
```

- [ ] **Step 3: Build**

```
cmake --build build-clang --target liva_codegen
```

If link errors complain about missing LLVM coroutine libs, add to `src/CodeGen/CMakeLists.txt`:

```cmake
target_link_libraries(liva_codegen PUBLIC LLVMCoroutines)
```

- [ ] **Step 4: Re-run async baseline + generator test**

```
ctest --test-dir build-clang -R "RuntimeExecTest" --output-on-failure
```

If the generator test now passes outright, Tasks 7-8 may be reduced — re-evaluate before continuing. If async **regresses**, revert this task's changes (the default pipeline was sufficient and we double-added).

- [ ] **Step 5: Commit (only if changes made)**

```bash
git add src/CodeGen/CodeGen.cpp src/CodeGen/CMakeLists.txt
git commit -m "codegen: explicitly register coroutine passes (CoroEarly/Split/Elide/Cleanup)"
```

---

## Task 7: Split generator from async — lazy initial suspend

**Goal:** Generators must be *lazy*: calling `countTo(3)` should produce a `LivaTask*` whose body has **not yet executed**. The first `coro.resume(handle)` runs the body up to the first `yield`. The async path stays *eager*: calling `answer()` runs the body to its first `await` (or completion). This requires (a) a `currentIsGenerator_` flag distinct from `currentIsAsync_`, and (b) an *initial suspend* point inserted between `coro.begin` and the body — but **only** for generators.

**Files:**
- Modify: `include/liva/IR/IRGen.h`
- Modify: `src/IR/IRGenDecl.cpp:258-497`
- Modify: `src/IR/IRGenStmt.cpp:625-707`

- [ ] **Step 1: Add the flag**

In `include/liva/IR/IRGen.h`, near the other `currentCoro*` fields:

```cpp
bool currentIsGenerator_ = false;
```

- [ ] **Step 2: Save/restore the flag in `visitFuncDecl`**

In `src/IR/IRGenDecl.cpp` near line 370 (where other coro state is saved/restored):

```cpp
bool oldIsGenerator = currentIsGenerator_;
// ... at line 381 area:
currentIsGenerator_ = node->isGenerator();
// ... and at the function epilogue where other state is restored:
currentIsGenerator_ = oldIsGenerator;
```

- [ ] **Step 3: Insert lazy initial suspend for generators**

In `src/IR/IRGenDecl.cpp` around line 488-497 (where `coro.body` is currently created and branched to immediately after `coro.begin`):

```cpp
// Create the coro.final, coro.cleanup, coro.suspend blocks (will be filled later)
currentCoroFinalBB_ = llvm::BasicBlock::Create(*context_, "coro.final", func);
currentCoroCleanupBB_ = llvm::BasicBlock::Create(*context_, "coro.cleanup", func);
currentCoroSuspendBB_ = llvm::BasicBlock::Create(*context_, "coro.suspend", func);

auto *bodyBB = llvm::BasicBlock::Create(*context_, "coro.body", func);

if (node->isGenerator()) {
    // Lazy: insert an initial suspend so the body does not run until the
    // caller's first coro.resume. This is what makes generators iterable.
    auto *suspendFn = llvm::Intrinsic::getOrInsertDeclaration(module_.get(),
        llvm::Intrinsic::coro_suspend);
    auto *initSusp = builder_->CreateCall(suspendFn,
        {llvm::ConstantTokenNone::get(*context_), builder_->getFalse()},
        "init.susp");
    auto *initSw = builder_->CreateSwitch(initSusp, currentCoroSuspendBB_, 2);
    initSw->addCase(builder_->getInt8(0), bodyBB);              // resume → body
    initSw->addCase(builder_->getInt8(1), currentCoroCleanupBB_); // destroy
} else {
    builder_->CreateBr(bodyBB);  // Async: eager — body starts now.
}

builder_->SetInsertPoint(bodyBB);
```

- [ ] **Step 4: Replace name-based generator detection in for-in IRGen**

In `src/IR/IRGenStmt.cpp:625-707`, the current code does:

```cpp
auto *call = dynamic_cast<const CallExpr *>(node->getIterable());
if (call) {
    auto callee = /* extract identifier name */;
    auto it = generatorFuncs_.find(calleeName);
    if (it != generatorFuncs_.end()) { /* generator path */ }
}
```

Replace the detection with a Sema-type check (so `let g = countTo(3); for x in g` works):

```cpp
// Generator dispatch: check the iterable's static type, not the callee name.
const TypeRepr *iterTy = sema_->getTypeOf(node->getIterable());
bool isGenerator = false;
llvm::Type *yieldedTy = nullptr;
if (auto *named = dynamic_cast<const NamedTypeRepr *>(iterTy);
    named && named->getName() == "Generator" && named->getTypeArgs().size() == 1) {
    isGenerator = true;
    yieldedTy = toLLVMType(named->getTypeArgs()[0].get());
}
if (isGenerator) {
    // existing coro.done / coro.promise / coro.resume loop, using yieldedTy
}
```

(API names are illustrative — match what the project actually exposes for "type of expression after sema." If no such accessor exists, add one to the IRGen side that consults the same maps Sema populates.)

- [ ] **Step 5: Build and run the full ctest suite**

```
cmake --build build-clang
ctest --test-dir build-clang --output-on-failure 2>&1 | tail -30
```

Expected:
- Async runtime test still passes (eager path unchanged).
- Generator runtime test passes (`0\n1\n2\n`).
- All 2064 prior tests still pass.

If the generator test still fails, **stop and debug with `systematic-debugging`** — do not pile more changes on. Look at `gen_count_three.post.ll` to see what CoroSplit produced, and compare against a minimal hand-written `countTo` IR (Task 8 reference if needed).

- [ ] **Step 6: Commit**

```bash
git add include/liva/IR/IRGen.h src/IR/IRGenDecl.cpp src/IR/IRGenStmt.cpp
git commit -m "irgen: split generator path from async; lazy initial suspend; type-based for-in dispatch"
```

---

## Task 8: Add follow-up generator runtime tests

**Goal:** Lock in the new behavior with more end-to-end cases so future regressions are caught.

**Files:**
- Modify: `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Add three cases**

```cpp
TEST(RuntimeExecTest, Generator_EmptyRange_NoOutput) {
    auto r = compileAndRun(R"(
        func gen(n: i32) { var i: i32 = 0; while i < n { yield i; i = i + 1 } }
        func main() { for x in gen(0) { println(x) } }
    )", "gen_empty");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "");
}

TEST(RuntimeExecTest, Generator_BoundToVariable_Iterates) {
    auto r = compileAndRun(R"(
        func gen() { yield 7; yield 8 }
        func main() { let g = gen(); for x in g { println(x) } }
    )", "gen_bound");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "7\n8\n");
}

TEST(RuntimeExecTest, Generator_NestedYieldInIf_Yields) {
    auto r = compileAndRun(R"(
        func gen(n: i32) {
            var i: i32 = 0
            while i < n { if i % 2 == 0 { yield i }; i = i + 1 }
        }
        func main() { for x in gen(5) { println(x) } }
    )", "gen_nested");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "0\n2\n4\n");
}
```

- [ ] **Step 2: Run, expect all PASS**

```
ctest --test-dir build-clang -R RuntimeExecTest.Generator --output-on-failure
```

Any failure here means the state machine is incomplete — **do not** kludge fixes. Open a follow-up task and stop.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/RuntimeExecTest.cpp
git commit -m "test: lock in generator runtime behavior (empty/bound/nested cases)"
```

---

## Self-Review Notes

- **Spec coverage:** All four user-stated requirements covered — (1) hello-coroutine reference = Task 4, (2) initial suspend = Task 7, (3) e2e runtime test = Tasks 1+3+8, (4) generator/async separation = Task 7. (Note: Task 4 inspects what CoroSplit *actually* did rather than building a separate hand-written IR, because the e2e harness is the authoritative ABI test — a hand-written reference is only worth building if Task 4 reveals CoroSplit didn't run, in which case Task 6 fixes it.)
- **Placeholder scan:** Two intentional "match the project's actual API" notes (Task 5 Step 4, Task 7 Step 4) where the exact accessor name depends on what `Generator<T>` looks like post-construction. The reader should `grep` for the construction site cited in the plan and mirror it. This is *not* TBD — the construction site is named (`src/Sema/TypeChecker.cpp:2675`).
- **Type consistency:** `currentIsGenerator_` (Task 7) is a new flag distinct from `currentIsAsync_` (existing) and `node->isGenerator()` (existing AST query). `Generator<T>` Sema type construction is referenced consistently across Tasks 5 and 7. `compileAndRun` signature (Task 1) is reused in Tasks 2, 3, 8.
- **Risk:** Task 1 (test harness) is the longest and depends on the existing driver's compile-to-exe shape. If the driver doesn't expose a clean entry point, allow ~30 extra minutes for the small `Driver::compileToExe` refactor.
