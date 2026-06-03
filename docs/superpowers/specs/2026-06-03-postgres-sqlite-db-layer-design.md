# PostgreSQL + Ortak DB Katmanı + SQLite İyileştirmeleri — Tasarım

- **Tarih:** 2026-06-03
- **Durum:** Onaylandı (uygulamaya hazır)
- **Kapsam:** PostgreSQL istemci modülü, sürücü-bağımsız ortak `Database` katmanı, mevcut SQLite modülüne 4 eksik özellik

## 1. Hedef ve bağlam

Liva-lang'a veritabanı desteğini tamamlamak. Mevcut durum incelemesi:

- **SQLite zaten tam çalışır durumda**: `stdlib/sqlite/sqlite.liva` (wrapper), `stdlib/runtime/runtime.cpp`/`.h` (`liva_sqlite_*` natives; Windows'ta `winsqlite3.dll` dinamik yükleme, POSIX'te `libsqlite3` link), `src/IR/IRGenCall.cpp` (intrinsic lowering), `src/Sema/TypeChecker.cpp` (imza kaydı), `src/Sema/ModuleLoader.cpp` (`std::sqlite` modülü).
- **PostgreSQL hiç yok**: kod tabanında `libpq`/postgres referansı yok.

Bu iş üç parçadan oluşur ve hepsi mevcut native-binding desenini izler:

```
stdlib/.liva wrapper  →  liva_* runtime fonksiyonları  →  IRGenCall.cpp intrinsic lowering
                      →  TypeChecker imza kaydı         →  ModuleLoader modül kaydı
```

## 2. PostgreSQL modülü (`postgres::postgres` / `std::postgres`)

### Native bağlantı stratejisi: dinamik yükle, fail-closed

SQLite'ın `winsqlite3` deseninin birebir aynısı:

- `libpq` çalışma anında çözülür: Windows `LoadLibraryA("libpq.dll")`, POSIX `dlopen("libpq.so"/"libpq.dylib")`. Birden çok aday isim denenir (ör. `libpq.so.5`).
- Kütüphane veya bir sembol bulunamazsa **tüm girişler fail-closed**: `open` → nil, `exec` → false, query → nil/boş, sayaçlar → 0.
- Derleme libpq olmadan da geçer; **CMake'te link YOK**.
- Sembol tablosu (lazy resolve, tek seferlik init):
  `PQconnectdb, PQstatus, PQfinish, PQexec, PQexecParams, PQresultStatus, PQresStatus, PQntuples, PQnfields, PQgetvalue, PQgetisnull, PQfname, PQcmdTuples, PQerrorMessage, PQclear`.
- Handle modeli: `PGconn*` ve `PGresult*` → `int64_t`'e cast (SQLite ile aynı yaklaşım). 0 = geçersiz.

### Liva API

```liva
pub struct PgResult {        // materialize edilmiş sonuç (PGresult*)
    var handle: i64
}
impl PgResult {
    pub func rowCount(ref self) -> i32
    pub func colCount(ref self) -> i32
    pub func getText(ref self, row: i32, col: i32) -> String
    pub func getInt(ref self, row: i32, col: i32) -> i64
    pub func getDouble(ref self, row: i32, col: i32) -> f64
    pub func isNull(ref self, row: i32, col: i32) -> bool
    pub func columnName(ref self, col: i32) -> String
    pub func clear(mut self)            // PQclear, handle = 0
}

pub struct PgConn {          // bağlantı (PGconn*)
    var handle: i64
}
impl PgConn {
    pub func open(connString: String) -> PgConn?   // "host=... dbname=... user=..."; PQstatus != OK ise nil
    pub func exec(ref self, sql: String) -> bool    // satır döndürmeyen komutlar; resultStatus COMMAND_OK/TUPLES_OK → true
    pub func query(ref self, sql: String) -> PgResult?
    pub func queryParams(ref self, sql: String, params: [String]) -> PgResult?  // PQexecParams, hepsi text
    pub func errorMessage(ref self) -> String       // PQerrorMessage
    pub func close(mut self)                         // PQfinish, handle = 0
}
```

> **Tasarım notu:** libpq'da SQLite'taki gibi ayrı kalıcı "prepared statement handle" nesnesi yoktur. Parametreli tek-atış sorgu için `PQexecParams` kullanılır; tüm parametreler text formatında (`paramFormats = NULL`) gönderilir ve sonuç tümüyle materialize edilir. SQLite ise step-based cursor olarak kalır. Ortak katman bu modeli farkını gizler.

### Yeni intrinsic'ler (IRGenCall.cpp) ve natives (runtime)

| Liva builtin | Native | Dönüş |
|---|---|---|
| `pgConnect(connStr)` | `liva_pg_connect` | i64 (0 = fail) |
| `pgClose(h)` | `liva_pg_close` | void |
| `pgExec(h, sql)` | `liva_pg_exec` | i32 rc → bool |
| `pgQuery(h, sql)` | `liva_pg_query` | i64 result handle (0 = fail) |
| `pgQueryParams(h, sql, params, n)` | `liva_pg_query_params` | i64 result handle |
| `pgResultRows(rh)` | `liva_pg_ntuples` | i32 |
| `pgResultCols(rh)` | `liva_pg_nfields` | i32 |
| `pgResultText(rh, r, c)` | `liva_pg_getvalue` | string |
| `pgResultIsNull(rh, r, c)` | `liva_pg_getisnull` | bool |
| `pgColumnName(rh, c)` | `liva_pg_fname` | string |
| `pgClear(rh)` | `liva_pg_clear` | void |
| `pgErrmsg(h)` | `liva_pg_errmsg` | string |

`getInt`/`getDouble` Liva tarafında `getText` + parse ile yapılır (ekstra native gerekmez). `queryParams` parametre dizisi runtime'a `(char** values, int n)` olarak geçer — IRGen'de `[String]`'in eleman pointer'ları ayrıştırılır (array temsili IRGen'de doğrulanacak; gerekirse Liva tarafında parametreleri tek bir ayraçlı string'e paketleyip runtime'da bölen daha basit bir köprü fallback olarak kullanılır).

## 3. Ortak DB katmanı (`db::db` / `std::db` gerekmez — saf Liva)

Saf Liva; yeni native gerektirmez. `dyn Database` (trait object, dinamik dispatch) Liva'da destekleniyor (`examples/dyn_protocol_demo.liva`).

```liva
pub struct Row {
    var names: [String]
    var vals:  [String]      // her hücre text temsili
    var nulls: [bool]
}
impl Row {
    pub func getText(ref self, col: i32) -> String
    pub func getInt(ref self, col: i32) -> i64
    pub func getDouble(ref self, col: i32) -> f64
    pub func isNull(ref self, col: i32) -> bool
    pub func byName(ref self, name: String) -> String?
}

pub protocol Database {
    func exec(ref self, sql: String) -> bool
    func query(ref self, sql: String, params: [String]) -> [Row]
    func lastInsertId(ref self) -> i64
    func errorMessage(ref self) -> String
    func close(mut self)
}
```

### Adapterlar (her ikisi `impl Database`)

- **`SqliteDatabase`** — içte `SqliteDB`. `query`: SQL'i prepare eder, `?` parametrelerini `bindText` ile sırayla bağlar, `step()` döngüsüyle satırları `[Row]`'a toplar (kolon adları `columnName`/`columnCount` üzerinden; SQLite modülüne gerekiyorsa `columnName` intrinsic'i eklenir). `lastInsertId` → `SqliteDB.lastInsertId`.
- **`PgDatabase`** — içte `PgConn`. `query`: `?` placeholder'larını `$1..$n`'e normalize eder, `queryParams` çağırır, `PgResult`'ı `[Row]`'a kopyalar. `lastInsertId`: Postgres'te otomatik yok → `RETURNING` deseni önerilir; metot 0 döndürür (belgelenir).

### Parametre normalizasyonu

Kullanıcı her zaman `?` yazar. Adapter sürücüye göre çevirir:

- **SQLite**: değiştirmez (`?` zaten geçerli).
- **Postgres**: soldan sağa `?` → `$1, $2, …`. **Tırnak-duyarlı tarayıcı**: tek tırnaklı string literal (`'...'`, `''` escape dahil) içindeki `?` atlanır.
- **Bilinen sınır (belgelenecek):** tarayıcı SQL'i tam parse etmez. SQL yorumları (`--`, `/* */`) ve dollar-quoted string (`$$...$$`) içindeki `?` yanlışlıkla çevrilebilir. Bu kenar durumlar kapsam dışı; kullanıcı bu durumda sürücüye özel `queryParams`'ı doğrudan kullanmalı.

### Kullanım

```liva
import db::db
import sqlite::sqlite      // adapter open için
// import postgres::postgres

var db: dyn Database = SqliteDatabase.open(":memory:")!
db.exec("CREATE TABLE u(id INTEGER PRIMARY KEY, name TEXT)")
let rows = db.query("SELECT name FROM u WHERE id > ?", ["0"])
for r in rows { println(r.getText(0)) }
db.close()
```

Aynı kod `PgDatabase.open("host=... dbname=...")!` ile değişmeden çalışır.

### Değer modeli

Parametreler ve okunan değerler **text** olarak taşınır. SQLite dinamik tipli olduğundan `bindText` her tipe uyar; Postgres `PQexecParams` text formatında gönderir, sunucu kolon tipine göre coerce eder. Tek tip `[String]` ile ikisi de çalışır. Typed `Value` enum'ı YAGNI — gerekirse sonraki iterasyonda.

## 4. SQLite eklemeleri

| Özellik | Yaklaşım | Yeni native |
|---|---|---|
| **Transaction** | `SqliteDB.begin()/commit()/rollback() -> bool` + `transaction(body: () -> bool) -> bool` (body true→COMMIT, false→ROLLBACK) | Hayır — saf Liva, mevcut `exec` üzerinden |
| **BLOB** | `Stmt.bindBlob(idx: i32, data: [u8]) -> bool`, `Stmt.columnBlob(col: i32) -> [u8]` | Evet — `liva_sqlite_bind_blob(stmt, idx, ptr, len)`, `liva_sqlite_column_blob` (+ `_column_bytes`) |
| **NULL & tip** | `Stmt.columnIsNull(col: i32) -> bool`, `Stmt.columnType(col: i32) -> i32` (1=INTEGER,2=FLOAT,3=TEXT,4=BLOB,5=NULL) | Evet — `liva_sqlite_column_type` (`isNull` = type==5) |
| **Named param** | `Stmt.bindByName(name: String, val: String) -> bool` (`:isim`/`@isim`/`$isim`); index bulunamazsa false | Evet — `liva_sqlite_bind_param_index(stmt, name) -> i32`, sonra mevcut bind |

`sqlite_api()` yapısına yeni semboller eklenir: `bind_blob, column_blob, column_bytes, column_type, bind_parameter_index`.

### BLOB `[u8]` ↔ runtime köprüsü

En belirsiz kısım. Plan: native imzalar `(ptr, len)` alır/döndürür. IRGen'de `[u8]` array'in data pointer'ı ve uzunluğu mevcut array temsiline göre ayrıştırılır. **İlk adım: IRGen'de `[u8]`/array bellek temsilini doğrulamak**; temsil köprülemeyi zorlaştırıyorsa, BLOB'u base64/hex string üzerinden taşıyan daha basit bir alternatif değerlendirilir (kararı uygulama sırasında, temsil netleştikten sonra ver).

## 5. Test stratejisi

- **PostgreSQL (sunucusuz, fail-closed)**: libpq yokken `PgConn.open` → nil, `exec` → false, query → nil; modülün import/derleme bütünlüğü. Gerçek sorgu testleri opsiyonel/elle (sunucu + libpq gerekir), CI'da çalışmaz.
- **SQLite yeni özellikler (in-memory)**: transaction rollback bir INSERT'i geri alıyor; blob round-trip (yaz→oku eşit); `columnIsNull` NULL kolonda true; `bindByName` `:isim` ile doğru değeri bağlıyor; `columnType` doğru tip kodu.
- **Ortak katman**: `?`→`$n` normalize birim testi (tırnak-içi atlama dahil); `SqliteDatabase` üzerinden uçtan uca `query` → `[Row]` doğrulaması; `dyn Database` ile polimorfik kullanım.
- Mevcut **2064 testin tamamı geçmeye devam etmeli** (regresyon yok).

## 6. Dosya değişiklikleri

**Yeni:**
- `stdlib/postgres/postgres.liva`
- `stdlib/db/db.liva`
- `examples/db_unified_demo.liva`
- `tests/.../PostgresTest`, `SqliteExtrasTest`, `DbLayerTest` (mevcut test yapısına uygun konum/isim)

**Değişen:**
- `stdlib/sqlite/sqlite.liva` — transaction/blob/null-tip/named param metotları
- `stdlib/runtime/runtime.cpp` / `runtime.h` — `liva_pg_*` natives + yeni `liva_sqlite_*` natives + `sqlite_api()` yeni semboller + `pg_api()` dinamik yükleyici
- `src/IR/IRGenCall.cpp` — yeni `pg*` ve `sqlite*` intrinsic lowering'leri
- `src/Sema/TypeChecker.cpp` — yeni builtin imzalar
- `src/Sema/ModuleLoader.cpp` — `std::postgres` modülü (+ gerekiyorsa `std::sqlite`'a yeni semboller)
- `CMakeLists.txt` — yeni `.liva` dosyaları kurulum/kopyalama listesine
- `docs/tr/API-REFERENCE.md`, `docs/en/API-REFERENCE.md` — postgres + db bölümleri, SQLite yeni metotlar

## 7. Açık riskler

1. **BLOB `[u8]` köprüsü** — IRGen array temsiline bağlı; en yüksek belirsizlik. İlk iş bunu doğrulamak.
2. **Postgres testi sunucusuz** — yalnız fail-closed doğrulanabilir; gerçek I/O testleri ortam-bağımlı.
3. **Param normalize sınırı** — yorum ve dollar-quoted string içindeki `?` çevrilebilir; belgelenir, edge-case'te `queryParams` doğrudan kullanılır.
4. **`std::sqlite`'a yeni intrinsic eklerken** mevcut 2064 testin kırılmaması — her eklemeden sonra `ctest`.
