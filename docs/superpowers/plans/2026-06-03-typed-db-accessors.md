# Typed DB Accessors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `getDouble`/`getBool`/`getDate`/`getTime`/`getDateTime` (and SQLite `column*` equivalents) to the three DB result types, backed by two new `time::time` types (`Date`, `Time`) and a `convert::toBool` helper.

**Architecture:** Almost entirely pure Liva. `Date`/`Time` mirror the existing `DateTime` struct (a single `f64 timestamp` over native intrinsics `dateParse`/`dateYear`/`dateFormat`/… — no new native code). Each result type's typed accessor reads the cell text and parses it (`convert::toFloat`/`toBool`, `Date.parse`/`Time.parse`/`DateTime.parse`). Accessors are non-optional and degrade to a default (`0.0`/`false`/epoch) on parse failure, matching the existing `getInt`/`getText`.

**Tech Stack:** Liva stdlib (`.liva`), C++20/LLVM only if the §5 cross-module Named-return risk materializes. Build: `build_clang.bat` → `build-clang/`. Test: `ctest --test-dir build-clang -R <name> --output-on-failure` — **always SERIAL (no `-j`)**; the suite has pre-existing parallel races under `-j`.

---

## Reference (read before starting)

- `stdlib/time/time.liva`: `DateTime { var timestamp: f64 }` with `parse(s, fmt)`, `year/month/day/hour/minute/second(ref self) -> i32`, `format(ref self, fmt) -> String`, all delegating to native intrinsics `dateParse(s, fmt) -> f64`, `dateYear/dateMonth/dateDay/dateHour/dateMinute/dateSecond(ts) -> i32`, `dateFormat(ts, fmt) -> String`. These intrinsics are builtins in the `std::time` module — reusable by new structs in the same file with no extra wiring.
- `stdlib/convert/convert.liva`: `toInt(s) -> i32?`, `toInt64(s) -> i64?`, `toFloat(s) -> f64?`. String methods available in Liva: `s.trim()`, `s.toLower()`, `s == other`.
- DB result types: `sqlite::sqlite` `Stmt` (`column*` naming; already has `columnText/columnInt/columnDouble/columnName/columnType/columnIsNull/columnBlob`), `postgres::postgres` `PgResult` (`get*`; has `getText/getInt/isNull/columnName/rowCount/colCount/clear`), `db::db` `Row` (`get*`; has `getText/getInt/isNull/byName`, fields `names/vals/nulls: [String]/[String]/[bool]`).
- Liva gotchas (confirmed in prior work): array indexing needs an i64 index (`vals[col as i64]`); `??` nil-coalescing FAILS in IRGen inside a non-optional-returning function — use `if let`; struct fields are copied by value.
- Test harnesses: `tests/unit/RuntimeExecTest.cpp` `compileAndRun(src, name) -> {exit_code, stdout_output}` runs a real exe (real SQLite via winsqlite3.dll); `tests/unit/StdlibModuleTest.cpp` `check(src, true, "stdlib") -> {passed}` is type-check only.
- **§5 cross-module Named return (the one risk):** Returning a Named struct (`Date`/`Time`/`DateTime`) from a method of `Row`/`PgResult`/`Stmt` and CHAINING (`row.getDate(0).year()`) may not type-check if the TypeChecker doesn't propagate Named return types across modules (its import-propagation, added for BLOB, was restricted to Array/Optional). A method returning the *same module's* Named type already resolves (e.g. `DateTime.now().year()` type-checks today). The test in Task 5 (and Task 3) chains a method on the returned type to surface this; the conditional fix is spelled out in Task 5 Step 4b.

---

## Task 1: `convert::toBool`

**Files:**
- Modify: `stdlib/convert/convert.liva`
- Test: `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Write the failing behavioral test**

Add to `tests/unit/RuntimeExecTest.cpp` (inside `#ifdef LIVA_HAS_LLVM`):

```cpp
TEST(RuntimeExecTest, ConvertToBool) {
    auto r = compileAndRun(
        "import convert::convert\n"
        "func yn(b: bool) -> String { if b { return \"Y\" } return \"N\" }\n"
        "func main() {\n"
        "    println(yn(toBool(\"1\")))\n"
        "    println(yn(toBool(\"t\")))\n"
        "    println(yn(toBool(\"TRUE\")))\n"
        "    println(yn(toBool(\" yes \")))\n"
        "    println(yn(toBool(\"0\")))\n"
        "    println(yn(toBool(\"f\")))\n"
        "    println(yn(toBool(\"\")))\n"
        "}\n",
        "convert_tobool");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "Y\nY\nY\nY\nN\nN\nN\n");
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R RuntimeExecTest.ConvertToBool --output-on-failure`
Expected: FAIL (`toBool` not found → compile failure).

- [ ] **Step 3: Add `toBool` to `stdlib/convert/convert.liva`**

Add after the `toFloat` function:

```liva
// Flexible boolean parse: "1"/"t"/"true"/"yes"/"on" (any case, trimmed) -> true,
// everything else -> false. Handles SQLite (0/1) and PostgreSQL ('t'/'f') text.
pub func toBool(s: String) -> bool {
    let v = s.trim().toLower()
    return v == "1" || v == "t" || v == "true" || v == "yes" || v == "on"
}
```

- [ ] **Step 4: Run it to confirm it passes**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R RuntimeExecTest.ConvertToBool --output-on-failure`
Expected: PASS — stdout `Y\nY\nY\nY\nN\nN\nN\n`.

- [ ] **Step 5: Commit**

```bash
git add stdlib/convert/convert.liva tests/unit/RuntimeExecTest.cpp
git commit -m "convert: add flexible toBool"
```

---

## Task 2: `time::time` `Date` and `Time` types

**Files:**
- Modify: `stdlib/time/time.liva`
- Test: `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Write the failing behavioral test**

Add to `tests/unit/RuntimeExecTest.cpp`:

```cpp
TEST(RuntimeExecTest, TimeDateAndTime) {
    auto r = compileAndRun(
        "import time::time\n"
        "func main() {\n"
        "    let d = Date.parse(\"2024-06-15\")\n"
        "    println(d.year())\n"
        "    println(d.month())\n"
        "    println(d.day())\n"
        "    let t = Time.parse(\"13:45:30\")\n"
        "    println(t.second())\n"
        "}\n",
        "time_date_time");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "2024\n6\n15\n30\n");
}
```

(Assertions chosen to be timezone-robust: a mid-month date's year/month/day and a time's seconds don't shift under whole-hour tz offsets. Avoid asserting `Time.hour()` or full-string round-trips, which can shift with the host timezone.)

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R RuntimeExecTest.TimeDateAndTime --output-on-failure`
Expected: FAIL (`Date`/`Time` unknown).

- [ ] **Step 3: Add `Date` and `Time` to `stdlib/time/time.liva`**

Insert AFTER the `DateTime` `impl` block closes and BEFORE the free functions (`pub func fromUnix`). (Read the file to find the exact line where `impl DateTime { ... }` ends.)

```liva
// =============================================================
// Date — a calendar date (year/month/day). Backed by a timestamp at the
// parsed instant; the time-of-day component is unused. Reuses the same native
// intrinsics as DateTime.
// =============================================================

pub struct Date {
    var timestamp: f64
}

impl Date {
    pub func parse(s: String) -> Date {
        return Date { timestamp: dateParse(s, "%Y-%m-%d") }
    }
    pub func year(ref self) -> i32 { return dateYear(self.timestamp) }
    pub func month(ref self) -> i32 { return dateMonth(self.timestamp) }
    pub func day(ref self) -> i32 { return dateDay(self.timestamp) }
    pub func toString(ref self) -> String {
        return dateFormat(self.timestamp, "%Y-%m-%d")
    }
}

// =============================================================
// Time — a time-of-day (hour/minute/second). The date component is unused.
// =============================================================

pub struct Time {
    var timestamp: f64
}

impl Time {
    pub func parse(s: String) -> Time {
        return Time { timestamp: dateParse(s, "%H:%M:%S") }
    }
    pub func hour(ref self) -> i32 { return dateHour(self.timestamp) }
    pub func minute(ref self) -> i32 { return dateMinute(self.timestamp) }
    pub func second(ref self) -> i32 { return dateSecond(self.timestamp) }
    pub func toString(ref self) -> String {
        return dateFormat(self.timestamp, "%H:%M:%S")
    }
}
```

- [ ] **Step 4: Run it to confirm it passes**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R RuntimeExecTest.TimeDateAndTime --output-on-failure`
Expected: PASS — stdout `2024\n6\n15\n30\n`.

If `Date.parse(...).year()` chaining fails to type-check (unlikely — `DateTime.now().year()` already works for the same-module case), the type isn't resolving for the same-module static-return; investigate before proceeding (this would also predict a harder §5 issue downstream).

- [ ] **Step 5: Commit**

```bash
git add stdlib/time/time.liva tests/unit/RuntimeExecTest.cpp
git commit -m "time: add Date and Time types"
```

---

## Task 3: SQLite `Stmt` typed accessors

**Files:**
- Modify: `stdlib/sqlite/sqlite.liva`
- Test: `tests/unit/RuntimeExecTest.cpp`

- [ ] **Step 1: Write the failing behavioral test**

Add to `tests/unit/RuntimeExecTest.cpp`:

```cpp
TEST(RuntimeExecTest, SqliteTypedColumns) {
    auto r = compileAndRun(
        "import sqlite::sqlite\n"
        "func main() {\n"
        "    if let db = SqliteDB.openMemory() {\n"
        "        var d = db\n"
        "        d.exec(\"CREATE TABLE t(price REAL, active INTEGER, dt TEXT, tm TEXT, ts TEXT)\")\n"
        "        d.exec(\"INSERT INTO t VALUES (19.99, 1, '2024-06-15', '13:45:30', '2024-06-15 13:45:30')\")\n"
        "        if let s = d.prepare(\"SELECT price, active, dt, tm, ts FROM t\") {\n"
        "            var stmt = s\n"
        "            if stmt.step() {\n"
        "                if stmt.columnDouble(0) > 19.0 { println(\"price-ok\") } else { println(\"price-bad\") }\n"
        "                if stmt.columnBool(1) { println(\"active\") } else { println(\"inactive\") }\n"
        "                println(stmt.columnDate(2).year())\n"
        "                println(stmt.columnTime(3).second())\n"
        "                println(stmt.columnDateTime(4).year())\n"
        "            }\n"
        "            stmt.finalize()\n"
        "        }\n"
        "        d.close()\n"
        "    }\n"
        "}\n",
        "sqlite_typed_columns");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "price-ok\nactive\n2024\n30\n2024\n");
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R RuntimeExecTest.SqliteTypedColumns --output-on-failure`
Expected: FAIL (`columnBool`/`columnDate`/etc. unknown).

- [ ] **Step 3: Add imports + accessors to `stdlib/sqlite/sqlite.liva`**

At the top of the file (with the existing `import std::sqlite`), add:

```liva
import convert::convert
import time::time
```

In `impl Stmt`, after `columnBlob` (the last column accessor), add:

```liva
    pub func columnBool(ref self, col: i32) -> bool {
        return toBool(self.columnText(col))
    }

    pub func columnDate(ref self, col: i32) -> Date {
        return Date.parse(self.columnText(col))
    }

    pub func columnTime(ref self, col: i32) -> Time {
        return Time.parse(self.columnText(col))
    }

    pub func columnDateTime(ref self, col: i32) -> DateTime {
        return DateTime.parse(self.columnText(col), "%Y-%m-%d %H:%M:%S")
    }
```

- [ ] **Step 4: Run it to confirm it passes**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R RuntimeExecTest.SqliteTypedColumns --output-on-failure`
Expected: PASS — stdout `price-ok\nactive\n2024\n30\n2024\n`.

- [ ] **Step 4b (conditional — §5 cross-module Named return):** If Step 4 fails to COMPILE/type-check with an error about resolving a member (e.g. `.year()` / `.second()` on the `columnDate`/`columnDateTime` result, or "cannot resolve member"), the TypeChecker isn't propagating the Named return type (`Date`/`Time`/`DateTime` from `time::time`) of `sqlite`'s methods. Apply the fix in **Task 5 Step 4b** (it's the same mechanism; do it now, here, then re-run this test and Task 5's). If Step 4 passes, no compiler change is needed — proceed.

- [ ] **Step 5: Commit**

```bash
git add stdlib/sqlite/sqlite.liva tests/unit/RuntimeExecTest.cpp
git commit -m "sqlite: add columnBool/columnDate/columnTime/columnDateTime"
```

---

## Task 4: PostgreSQL `PgResult` typed accessors

**Files:**
- Modify: `stdlib/postgres/postgres.liva`
- Test: `tests/unit/StdlibModuleTest.cpp` (type-check only — no live server)

- [ ] **Step 1: Write the failing type-check test**

Add to `tests/unit/StdlibModuleTest.cpp`:

```cpp
TEST_F(StdlibModuleTest, PostgresTypedAccessors) {
    auto r = check(
        "import postgres::postgres\n"
        "func main() {\n"
        "    if let c = PgConn.open(\"host=localhost\") {\n"
        "        var conn = c\n"
        "        if let res = conn.query(\"SELECT 1\") {\n"
        "            var rs = res\n"
        "            let f = rs.getDouble(0, 0)\n"
        "            let b = rs.getBool(0, 0)\n"
        "            let dy = rs.getDate(0, 0).year()\n"
        "            let tm = rs.getTime(0, 0).second()\n"
        "            let yr = rs.getDateTime(0, 0).year()\n"
        "            rs.clear()\n"
        "        }\n"
        "        conn.close()\n"
        "    }\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "PgResult typed accessors should type-check";
}
```

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R StdlibModuleTest.PostgresTypedAccessors --output-on-failure`
Expected: FAIL (`getDouble`/etc. unknown).

- [ ] **Step 3: Add `import time::time` + accessors to `stdlib/postgres/postgres.liva`**

At the top (it already has `import std::postgres` and `import convert::convert`), add:

```liva
import time::time
```

In `impl PgResult`, after the existing accessors (e.g. after `columnName`, before `clear`), add:

```liva
    pub func getDouble(ref self, row: i32, col: i32) -> f64 {
        let p = toFloat(pgResultText(self.handle, row, col))
        if let v = p { return v }
        return 0.0
    }

    pub func getBool(ref self, row: i32, col: i32) -> bool {
        return toBool(pgResultText(self.handle, row, col))
    }

    pub func getDate(ref self, row: i32, col: i32) -> Date {
        return Date.parse(pgResultText(self.handle, row, col))
    }

    pub func getTime(ref self, row: i32, col: i32) -> Time {
        return Time.parse(pgResultText(self.handle, row, col))
    }

    pub func getDateTime(ref self, row: i32, col: i32) -> DateTime {
        return DateTime.parse(pgResultText(self.handle, row, col), "%Y-%m-%d %H:%M:%S")
    }
```

(Use `pgResultText(self.handle, row, col)` directly — the same builtin `getText` wraps — to avoid relying on `self.getText` method resolution within the impl. `toFloat`/`toBool` come from `convert::convert`, already imported.)

- [ ] **Step 4: Run it to confirm it passes**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R StdlibModuleTest.PostgresTypedAccessors --output-on-failure`
Expected: PASS. (If `.year()`/`.second()` chaining fails to type-check, the §5 fix from Task 5 Step 4b is needed — but Task 3/5 should already have applied it. Re-run after that.)

- [ ] **Step 5: Commit**

```bash
git add stdlib/postgres/postgres.liva tests/unit/StdlibModuleTest.cpp
git commit -m "postgres: add getDouble/getBool/getDate/getTime/getDateTime to PgResult"
```

---

## Task 5: `db::db` `Row` typed accessors

**Files:**
- Modify: `stdlib/db/db.liva`
- Test: `tests/unit/RuntimeExecTest.cpp` (behavioral via SqliteDatabase, real SQLite)
- Possibly: `src/Sema/TypeChecker.cpp` (only if §5 risk materializes — Step 4b)

- [ ] **Step 1: Write the failing behavioral test**

Add to `tests/unit/RuntimeExecTest.cpp`:

```cpp
TEST(RuntimeExecTest, DbRowTypedAccessors) {
    auto r = compileAndRun(
        "import db::db\n"
        "func main() {\n"
        "    if let d = SqliteDatabase.openMemory() {\n"
        "        var db = d\n"
        "        db.exec(\"CREATE TABLE t(price REAL, active INTEGER, ts TEXT)\")\n"
        "        db.exec(\"INSERT INTO t VALUES (19.99, 1, '2024-06-15 13:45:30')\")\n"
        "        let rows = db.query(\"SELECT price, active, ts FROM t\", [])\n"
        "        if rows.length > 0 {\n"
        "            let row = rows[0]\n"
        "            if row.getDouble(0) > 19.0 { println(\"price-ok\") } else { println(\"price-bad\") }\n"
        "            if row.getBool(1) { println(\"active\") } else { println(\"inactive\") }\n"
        "            println(row.getDateTime(2).year())\n"
        "        }\n"
        "        db.close()\n"
        "    }\n"
        "}\n",
        "db_row_typed");
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.stdout_output, "price-ok\nactive\n2024\n");
}
```

(`db.query(sql, [])` passes an empty params array; confirm an empty `[]` is accepted for a `[String]` parameter — the C2 work made array-literal args build a DynArray, and `["0"]` was used in the SqliteDatabase test, so `[]` should lower to an empty DynArray. If `[]` needs an explicit element type, write `let noargs: [String] = []` and pass `noargs`.)

- [ ] **Step 2: Run it to confirm it fails**

Run: `ctest --test-dir build-clang -R RuntimeExecTest.DbRowTypedAccessors --output-on-failure`
Expected: FAIL (`getDouble`/`getBool`/`getDateTime` unknown).

- [ ] **Step 3: Add `import time::time` + accessors to `stdlib/db/db.liva`**

At the top (it already imports `convert::convert`, `sqlite::sqlite`, `postgres::postgres`), add:

```liva
import time::time
```

In `impl Row`, after `byName` (or after `getInt`), add:

```liva
    pub func getDouble(ref self, col: i32) -> f64 {
        let p = toFloat(self.vals[col as i64])
        if let v = p { return v }
        return 0.0
    }

    pub func getBool(ref self, col: i32) -> bool {
        return toBool(self.vals[col as i64])
    }

    pub func getDate(ref self, col: i32) -> Date {
        return Date.parse(self.vals[col as i64])
    }

    pub func getTime(ref self, col: i32) -> Time {
        return Time.parse(self.vals[col as i64])
    }

    pub func getDateTime(ref self, col: i32) -> DateTime {
        return DateTime.parse(self.vals[col as i64], "%Y-%m-%d %H:%M:%S")
    }
```

- [ ] **Step 4: Run it to confirm it passes**

Run: `build_clang.bat` then `ctest --test-dir build-clang -R RuntimeExecTest.DbRowTypedAccessors --output-on-failure`
Expected: PASS — stdout `price-ok\nactive\n2024\n`.

- [ ] **Step 4b (conditional — §5 cross-module Named return resolution):**

If Step 4 (or Task 3 Step 4) fails to type-check because `row.getDateTime(2).year()` (or `stmt.columnDate(...).year()`) can't resolve the member on the returned `DateTime`/`Date`/`Time` — i.e. the TypeChecker doesn't know `Row.getDateTime` returns `DateTime` because that Named type comes from a *different* imported module (`time::time`) than the one defining `Row` (`db::db`):

Extend the TypeChecker's cross-module method-return-type propagation to also register **Named** return types (it currently registers only Array/Optional). Find, in `src/Sema/TypeChecker.cpp`, the module-import loop that walks imported `ImplDecl`s and records method return types into `typeMethodReturnTypes_` (added for the BLOB task; search for `typeMethodReturnTypes_` and the Array/Optional kind guard near the import handling, ~lines 357-394). Widen the guard so a `Named` (struct) return type is also recorded, e.g. change a condition like:

```cpp
if (retKind == TypeRepr::Kind::Array || retKind == TypeRepr::Kind::Optional) {
```

to also accept `TypeRepr::Kind::Named`:

```cpp
if (retKind == TypeRepr::Kind::Array || retKind == TypeRepr::Kind::Optional ||
    retKind == TypeRepr::Kind::Named) {
```

(Read the actual code to find the exact condition and variable names — the snippet is illustrative. Keep the registration keyed `StructName::method` as before; Named keys don't collide with the existing Array/Optional entries since they're keyed by struct+method.)

Then rebuild and re-run BOTH the sqlite test (Task 3) and this test. After this change, run the **FULL serial suite** (`ctest --test-dir build-clang --output-on-failure`, no `-j`) and confirm 100% — widening return-type inference is the one change here with regression potential, so the whole suite is the gate. If any test regresses, narrow the approach (e.g. only register Named returns whose type name is a known struct) and re-verify.

If Step 4 passed without this, skip 4b entirely.

- [ ] **Step 5: Commit**

```bash
git add stdlib/db/db.liva tests/unit/RuntimeExecTest.cpp src/Sema/TypeChecker.cpp
git commit -m "db: add typed Row accessors (getDouble/getBool/getDate/getTime/getDateTime)"
```

(Drop `src/Sema/TypeChecker.cpp` from the `git add` if Step 4b was not needed.)

---

## Task 6: Documentation (TR + EN)

**Files:**
- Modify: `docs/en/API-REFERENCE.md`, `docs/tr/API-REFERENCE.md`

- [ ] **Step 1: Locate sections**

Run: `grep -n "SQLite\|postgres::postgres\|db::db\|time::time\|DateTime" docs/en/API-REFERENCE.md docs/tr/API-REFERENCE.md` to find the SQLite, postgres, db, and time sections in each file.

- [ ] **Step 2: Update `docs/en/API-REFERENCE.md`**

In the **time** section, document the new types:

```markdown
- `Date.parse(s) -> Date` (`"YYYY-MM-DD"`), `.year()/.month()/.day() -> i32`, `.toString() -> String`.
- `Time.parse(s) -> Time` (`"HH:MM:SS"`), `.hour()/.minute()/.second() -> i32`, `.toString() -> String`.
```

In the **SQLite "New SQLite methods"** list, add:

```markdown
- `Stmt.columnBool(col) -> bool`, `columnDate(col) -> Date`, `columnTime(col) -> Time`, `columnDateTime(col) -> DateTime` — typed column reads (parsed from text; `columnDouble` already exists).
```

In the **postgres `PgResult`** bullet and the **db `Row`** bullet, extend the accessor lists:

```markdown
- `PgResult`: … plus `getDouble(r,c) -> f64`, `getBool(r,c) -> bool`, `getDate(r,c) -> Date`, `getTime(r,c) -> Time`, `getDateTime(r,c) -> DateTime`.
- `Row`: … plus `getDouble(col) -> f64`, `getBool(col) -> bool`, `getDate(col) -> Date`, `getTime(col) -> Time`, `getDateTime(col) -> DateTime`.
```

Add a note: typed accessors are non-optional and return a default (`0.0`/`false`/epoch) on unparseable text; `getBool` accepts `1/t/true/yes/on` (any case).

- [ ] **Step 3: Update `docs/tr/API-REFERENCE.md`** with the Turkish equivalents (translate prose, keep identifiers):

```markdown
- `Date.parse(s) -> Date` (`"YYYY-MM-DD"`), `.year()/.month()/.day() -> i32`, `.toString() -> String`.
- `Time.parse(s) -> Time` (`"HH:MM:SS"`), `.hour()/.minute()/.second() -> i32`, `.toString() -> String`.
- `Stmt.columnBool/columnDate/columnTime/columnDateTime` — tipli kolon okuma (metinden ayrıştırılır; `columnDouble` zaten var).
- `PgResult`/`Row`: ayrıca `getDouble/getBool/getDate/getTime/getDateTime`.
- Tipli accessor'lar non-optional; ayrıştırılamayan metinde varsayılan (`0.0`/`false`/epoch) döner. `getBool`: `1/t/true/yes/on` (her durum) → true.
```

- [ ] **Step 4: Commit**

```bash
git add docs/en/API-REFERENCE.md docs/tr/API-REFERENCE.md
git commit -m "docs: document typed DB accessors + time Date/Time"
```

---

## Task 7: Full regression

- [ ] **Step 1: Run the whole suite SERIALLY**

Run: `ctest --test-dir build-clang --output-on-failure` (no `-j`)
Expected: 100% pass (prior 2328 + the new tests). `RuntimeExecTest.PgRealRoundTrip` skipped.

- [ ] **Step 2: If anything regressed, fix before finishing**

Most likely culprit if 4b was applied: the widened Named return-type inference affecting unrelated method calls. Use `superpowers:systematic-debugging`; narrow the §5 change if needed. Re-run until green.

- [ ] **Step 3: Update memory test count**

Update `MEMORY.md`'s "2328 tests passing" to the new total.

---

## Self-Review

- **Spec coverage:** §2 time Date/Time → Task 2; §3 convert::toBool → Task 1; §4.1 sqlite column* → Task 3; §4.2 postgres get* → Task 4; §4.3 db Row get* → Task 5; §5 risk → Task 3/5 Step 4b; §6 testing → tests in each task + Task 7; §7 files → all covered; docs → Task 6. All spec sections map to tasks.
- **Placeholder scan:** No TBD/TODO; every code step has complete code; Step 4b is a clearly-bounded conditional, not a placeholder.
- **Type consistency:** `toBool(s) -> bool`, `Date.parse(s) -> Date`, `Time.parse(s) -> Time`, `DateTime.parse(s, fmt) -> DateTime` used identically across Tasks 3/4/5; accessor names match the spec (`column*` for Stmt, `get*` for PgResult/Row); `if let` (not `??`) for getDouble everywhere; `col as i64` indexing in Row.
- **Ordering:** Task 1 (toBool) and Task 2 (Date/Time) precede the accessors that use them. Task 3 (sqlite, real-SQLite behavioral) surfaces the §5 cross-module risk first; Task 5 carries the fix definition.
