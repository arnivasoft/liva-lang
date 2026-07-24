# Liva Examples Index

77 `.liva` source files, grouped by category. The 11 new stdlib examples
(marked **NEW (2026-07)** below) are compiled — and the 9 offline ones also
run with pinned output — by `tests/unit/ExamplesTest.cpp`; the remaining
examples are not yet under automated checks.

## Prerequisites

- **Build any example**: `livac <file>.liva -o <out>` from the `examples/`
  directory (add `.exe` on Windows implicitly via the toolchain), then run
  `./<out>`.
- **UI examples** (`ui_*.liva`) link against wxWidgets and need the wx
  runtime DLLs (`wxbase331u_vc_x64_custom.dll`, `wxmsw331u_core_vc_x64_custom.dll`,
  plus `libpng16.dll`, `jpeg62.dll`, `tiff.dll`, `libwebp*.dll`, `pcre2-16.dll`,
  `z.dll`, `liblzma.dll`, `libsharpyuv.dll`) sitting next to the built `.exe`.
  These are already present locally in `examples/` (gitignored — not
  committed to the repo).
- **`http_demo.liva` / `websocket_demo.liva`** need a live internet
  connection to actually run; the test suite only compile-checks them (see
  the header comment in each file for run instructions).

## Language Features (47)

| File | Description |
|---|---|
| hello.liva | Hello world, basic println |
| fibonacci.liva | Recursive fibonacci function |
| structs.liva | Struct definitions and usage |
| struct_test.liva | Struct field/method test cases |
| control_flow.liva | If/while/for control flow |
| enum_match.liva | Enum + match expression |
| associated_values.liva | Enum cases with associated values |
| break_continue.liva | Loop break/continue statements |
| array_test.liva | Array basics test |
| math.liva | Basic math operations |
| module_main.liva | Cross-module import example |
| ref_demo.liva | Reference/borrow demo |
| optional_chaining.liva | Optional chaining with `?.` |
| math_demo.liva | Math builtin functions (abs, etc) |
| map_demo.liva | `Map<K,V>` collection demo |
| io_demo.liva | stdin/stdout I/O demo |
| for_collections.liva | For-in loops over collections |
| string_methods.liva | String method demo |
| parse_demo.liva | parseInt/parseInt64 demo |
| array_methods.liva | Array method demo |
| higher_order.liva | Higher-order functions/closures |
| string_index.liva | String indexing demo |
| slicing.liva | Array/string slicing |
| default_args.liva | Default function arguments |
| ternary.liva | Ternary conditional expression |
| type_alias.liva | Type alias declarations |
| tuple_demo.liva | Tuple construction/destructuring |
| capture_ref.liva | Closure capture-by-reference |
| ownership_cleanup.liva | Drop/ownership cleanup demo |
| closures.liva | Closures demo |
| generics.liva | Generic functions/types |
| pattern_matching.liva | Match/pattern matching |
| protocols.liva | Protocol (trait) demo |
| error_handling.liva | Result/error handling demo |
| ownership.liva | Ownership rules demo |
| ownership_demo.liva | Ownership move/borrow demo |
| async_example.liva | Async/await demo |
| collections.liva | Arrays, Maps, Sets, iteration |
| ffi_demo.liva | FFI: calling C functions |
| test_demo.liva | Built-in test framework demo |
| concurrency_demo.liva | Channels and TaskGroups |
| benchmark_demo.liva | Benchmarking framework demo |
| comptime_demo.liva | Compile-time evaluation demo |
| macro_demo.liva | Pattern-based macro demo |
| dyn_protocol_demo.liva | `dyn` trait objects / dynamic dispatch |
| cross_compile_demo.liva | Cross-compilation target demo |
| classes.liva | Swift-style class demo |

## Stdlib (12)

| File | Description |
|---|---|
| db_unified_demo.liva | Unified DB layer over SQLite/Postgres via `db::db` |
| map_set_demo.liva | **NEW (2026-07)** — `Map<K,V>` / `Set<T>` tour: insert/get/contains/remove/size/isEmpty/clear, `for (k,v)`, keys()/values() |
| json_demo.liva | **NEW (2026-07)** — `json::json` parse + typed field reads + build-and-serialize round trip |
| csv_demo.liva | **NEW (2026-07)** — `csv::csv` row parse (quoted fields) + join/escape round trip |
| toml_demo.liva | **NEW (2026-07)** — `toml::toml` parse + typed getters (string/int/bool) |
| sqlite_demo.liva | **NEW (2026-07)** — `sqlite::sqlite` in-memory DB: create/insert/prepare/bind/step/columns |
| regex_demo.liva | **NEW (2026-07)** — `regex::regex` wrapper tour: match/find/findAll/replace/groups, plus the full-match vs contains distinction (`regexMatch` vs `regexFind`) |
| crypto_jwt_demo.liva | **NEW (2026-07)** — `crypto::crypto` hashing + `jwt::jwt` sign/verify round trip |
| time_demo.liva | **NEW (2026-07)** — `time::time` Date/Time/DateTime parsing, formatting, arithmetic |
| cli_demo.liva | **NEW (2026-07)** — `cli::cli` ArgParser: flags/options/positionals/usage |
| http_demo.liva | **NEW (2026-07)** — `http::http` GET request, status/header/JSON body read (REQUIRES NETWORK ACCESS; compile-checked only) |
| websocket_demo.liva | **NEW (2026-07)** — `websocket::websocket` connect/send/recv/close (REQUIRES NETWORK ACCESS; compile-checked only) |

## UI (18)

| File | Description |
|---|---|
| ui_hello.liva | Minimal UI window hello world |
| ui_hello_wx.liva | Minimal wxWidgets-backed hello window |
| ui_counter.liva | Counter app with button/label |
| ui_counter_helper.liva | Counter demo helper module |
| ui_form.liva | Basic form with inputs |
| ui_form_themed.liva | Themed form demo |
| ui_callback_demo.liva | Event callback wiring demo |
| ui_composite_demo.liva | Composite/nested widget demo |
| ui_menu_demo.liva | Menu bar demo |
| ui_paint.liva | Custom paint/drawing demo |
| ui_showcase.liva | Widget showcase gallery |
| ui_showcase_demo.liva | Extended widget showcase demo |
| ui_validation_demo.liva | Form input validation demo |
| ui_widgets_demo.liva | Core widgets demo |
| ui_widgets_advanced.liva | Advanced widgets demo |
| ui_layout_align.liva | Layout/alignment/sizer demo |
| ui_data_binding.liva | Data-binding demo |
| ui_collection_binding.liva | Collection/list data-binding demo |
