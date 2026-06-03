# Typed DB Accessors (getDouble/getBool/getDate/getTime/getDateTime) — Design

- **Date:** 2026-06-03
- **Status:** Approved
- **Scope:** Add a richer typed accessor set to the three DB result types, plus two new `time` types (`Date`, `Time`) and a `convert::toBool` helper. Almost entirely pure Liva — no new native/runtime/IRGen code expected.

## 1. Goal & context

The DB result types currently expose only text/int accessors:
- `sqlite::sqlite` `Stmt`: `columnText`, `columnInt`, `columnDouble`, `columnName`, `columnType`, `columnIsNull`, `columnBlob`.
- `postgres::postgres` `PgResult`: `getText`, `getInt`, `isNull`, `columnName`.
- `db::db` `Row`: `getText`, `getInt`, `isNull`, `byName`.

Add typed accessors for double, bool, date, time, and datetime so callers don't hand-parse column text.

`time::time` is backed by a single `f64 timestamp` (Unix seconds) with all calendar math in pure Liva over native intrinsics (`dateParse`, `dateYear`, `dateMonth`, `dateDay`, `dateHour`, `dateMinute`, `dateSecond`, `dateFormat`, …). This lets `Date`/`Time` be thin pure-Liva wrappers reusing those intrinsics — **no new native code**.

`convert::convert` already has `toInt`, `toInt64`, `toFloat` (all returning Optionals). Add `toBool`.

## 2. `time::time` — new `Date` and `Time` types

Mirror the existing `DateTime` struct (single `f64 timestamp`, native intrinsics):

```liva
// Calendar date (time-of-day component unused; parsed at midnight).
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
    pub func toString(ref self) -> String { return dateFormat(self.timestamp, "%Y-%m-%d") }
}

// Time-of-day (date component unused).
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
    pub func toString(ref self) -> String { return dateFormat(self.timestamp, "%H:%M:%S") }
}
```

Both reuse the existing `dateParse`/`dateYear`/… intrinsics already used by `DateTime` in the same file. `dateParse` with a time-only format leaves the date at its default; `dateHour`/`dateMinute`/`dateSecond` still extract the right components.

`Date.parse`/`Time.parse` mirror `DateTime.parse`'s non-optional contract: on unparseable input `dateParse` returns `0.0` (epoch) — acceptable degradation, consistent with the module's existing behavior.

## 3. `convert::convert` — `toBool`

```liva
pub func toBool(s: String) -> bool {
    let v = s.trim().toLower()
    return v == "1" || v == "t" || v == "true" || v == "yes" || v == "on"
}
```

Flexible across drivers: SQLite stores bool as `0/1` (text `"1"`), PostgreSQL returns `'t'`/`'f'`, and textual `"true"`/`"false"` are also recognized. Anything else → `false`. Single source of truth used by all three result types.

## 4. Accessor sets

All new accessors are **non-optional** and degrade to a default on parse failure (`0.0` / `false` / epoch `Date`/`Time`/`DateTime`), matching the existing `getInt` (returns `0`) and `getText` (returns `""`) style.

### 4.1 `sqlite::sqlite` `Stmt` — `column*` convention

`columnDouble` already exists. Add (after the existing column accessors):

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

Add `import convert::convert` and `import time::time` to `sqlite.liva`.

### 4.2 `postgres::postgres` `PgResult` — `get*` convention

```liva
pub func getDouble(ref self, row: i32, col: i32) -> f64 {
    let p = toFloat(self.getText(row, col))
    if let v = p { return v }
    return 0.0
}
pub func getBool(ref self, row: i32, col: i32) -> bool {
    return toBool(self.getText(row, col))
}
pub func getDate(ref self, row: i32, col: i32) -> Date {
    return Date.parse(self.getText(row, col))
}
pub func getTime(ref self, row: i32, col: i32) -> Time {
    return Time.parse(self.getText(row, col))
}
pub func getDateTime(ref self, row: i32, col: i32) -> DateTime {
    return DateTime.parse(self.getText(row, col), "%Y-%m-%d %H:%M:%S")
}
```

`postgres.liva` already imports `convert::convert`; add `import time::time`.

### 4.3 `db::db` `Row` — `get*` convention

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

`db.liva` already imports `convert::convert`; add `import time::time`. Array indexing uses `col as i64` (Liva array indices are i64).

`??` nil-coalescing must NOT be used for `getDouble` (fails in IRGen inside a non-optional-returning function); use `if let`.

## 5. Known risk: cross-module Named return-type resolution

The new accessors return **Named struct types** (`Date`/`Time`/`DateTime`) defined in `time::time`, called from `sqlite`/`postgres`/`db`. The TypeChecker's cross-module method-return-type propagation (added in the A4 BLOB task) is restricted to `Array`/`Optional` return kinds; primitive returns (f64/bool) already resolve (proven by existing `columnDouble`→f64 and `columnIsNull`→bool).

If a consumer chains on the returned value (e.g. `row.getDateTime(c).year()` or `stmt.columnDate(0).toString()`) and the TypeChecker fails to resolve the Named return type, extend that propagation to also register **Named** return types from imported impl methods — the same mechanism, widening the allowed kinds. This is a scoped compiler change; **full serial `ctest` is the gate** (the A4/C2 reviews flagged the Array/Optional restriction as deliberate to limit inference blast radius, so widening must be verified against the whole suite).

The implementation will discover empirically whether this change is needed (write the behavioral test that chains a method on the returned type; if it fails to type-check, apply the propagation extension).

## 6. Testing

- **time Date/Time** (`tests/unit/RuntimeExecTest.cpp` or StdlibModuleTest): construct via `Date.parse("2024-01-15")`/`Time.parse("13:45:30")`, assert `year/month/day` and `hour/minute/second` and `toString`.
- **sqlite `Stmt`** (behavioral, real SQLite): a table with REAL/BOOLEAN/DATE/TIME/TIMESTAMP-ish TEXT columns; assert `columnDouble`, `columnBool`, and `columnDate(...).year()` / `columnDateTime(...).toString()` round-trip. This also exercises the cross-module Named return (risk §5).
- **db `Row`** (behavioral, via SqliteDatabase): query a row, assert `getDouble`/`getBool`/`getDate(...).year()`.
- **postgres `PgResult`** (type-check, `StdlibModuleTest`): the 5 new accessors type-check (no live server).
- Full serial suite must remain green (no `-j`).

## 7. Files changed

- `stdlib/time/time.liva` — `Date`, `Time` structs.
- `stdlib/convert/convert.liva` — `toBool`.
- `stdlib/sqlite/sqlite.liva` — 4 column accessors + 2 imports.
- `stdlib/postgres/postgres.liva` — 5 get accessors + 1 import.
- `stdlib/db/db.liva` — 5 get accessors + 1 import.
- `src/Sema/TypeChecker.cpp` — ONLY if §5 risk materializes (Named return propagation).
- `tests/unit/RuntimeExecTest.cpp`, `tests/unit/StdlibModuleTest.cpp` — tests.
- `docs/en/API-REFERENCE.md`, `docs/tr/API-REFERENCE.md` — document new accessors + time Date/Time.

## 8. Out of scope (YAGNI)

- Separate date/time arithmetic on `Date`/`Time` (add/diff) — only what the accessors need (parse + components + toString).
- Timezone handling beyond what `time::time` already does.
- Optional-returning accessor variants — non-optional degrade-to-default matches the existing API.
