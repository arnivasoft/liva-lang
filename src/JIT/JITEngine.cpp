#include "liva/JIT/JITEngine.h"

#ifdef LIVA_HAS_LLVM

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>

// Include runtime header for symbol addresses
#include "runtime.h"

namespace liva {

JITEngine::JITEngine(std::unique_ptr<llvm::orc::LLJIT> jit)
    : jit_(std::move(jit)) {}

JITEngine::~JITEngine() = default;

/// Register all liva_runtime symbols as absolute symbols in the main JITDylib.
/// This is needed on Windows where statically linked symbols are not visible
/// via GetForCurrentProcess/GetProcAddress.
static void registerRuntimeSymbols(llvm::orc::LLJIT &jit) {
    auto &es = jit.getExecutionSession();
    auto &mainDylib = jit.getMainJITDylib();

    llvm::orc::SymbolMap symbols;
    auto flags = llvm::JITSymbolFlags::Exported | llvm::JITSymbolFlags::Callable;

    #define REG(name) \
        symbols[es.intern(#name)] = { \
            llvm::orc::ExecutorAddr::fromPtr(reinterpret_cast<void*>(&name)), flags }

    // Memory
    REG(liva_alloc);
    REG(liva_free);
    REG(liva_alloc_zeroed);

    // String
    REG(liva_str_concat);
    REG(liva_str_equal);
    REG(liva_str_compare);
    REG(liva_str_length);
    REG(liva_str_byte_length);
    REG(liva_i32_to_str);
    REG(liva_i64_to_str);
    REG(liva_f64_to_str);
    REG(liva_bool_to_str);
    REG(liva_str_parse_i32);
    REG(liva_str_parse_i64);
    REG(liva_str_parse_f64);
    REG(liva_str_contains);
    REG(liva_str_starts_with);
    REG(liva_str_ends_with);
    REG(liva_str_index_of);
    REG(liva_str_substring);
    REG(liva_str_trim);
    REG(liva_str_to_upper);
    REG(liva_str_to_lower);
    REG(liva_str_replace);
    REG(liva_str_split);
    REG(liva_str_array_free);
    REG(liva_str_repeat);
    REG(liva_str_pad_left);
    REG(liva_str_pad_right);
    REG(liva_str_join);
    REG(liva_str_trim_left);
    REG(liva_str_trim_right);
    REG(liva_str_reverse);
    REG(liva_str_chars);
    REG(liva_str_lines);
    REG(liva_string_free);
    REG(liva_string_compare);

    // Print
    REG(liva_print_i32);
    REG(liva_print_i64);
    REG(liva_print_f64);
    REG(liva_print_bool);
    REG(liva_print_str);
    REG(liva_println_str);

    // File I/O
    REG(liva_file_open);
    REG(liva_file_close);
    REG(liva_file_read_line);
    REG(liva_file_read_all);
    REG(liva_file_write);
    REG(liva_file_write_line);
    REG(liva_file_seek);
    REG(liva_file_tell);
    REG(liva_file_size);
    REG(liva_read_line);

    // Directory/Path
    REG(liva_dir_list);
    REG(liva_dir_create);
    REG(liva_dir_remove);
    REG(liva_dir_exists);
    REG(liva_path_join);
    REG(liva_path_dirname);
    REG(liva_path_basename);
    REG(liva_path_extension);
    REG(liva_path_exists);
    REG(liva_file_is_file);

    // Array
    REG(liva_array_new);
    REG(liva_array_free);
    REG(liva_array_push);
    REG(liva_array_pop);
    REG(liva_array_contains);
    REG(liva_array_index_of);
    REG(liva_array_reverse);
    REG(liva_array_reversed);
    REG(liva_array_sorted);
    REG(liva_array_any);
    REG(liva_array_all);
    REG(liva_array_count);

    // Map
    REG(liva_map_new);
    REG(liva_map_free);
    REG(liva_map_insert);
    REG(liva_map_get);
    REG(liva_map_remove);
    REG(liva_map_contains);

    // Set
    REG(liva_set_new);
    REG(liva_set_free);
    REG(liva_set_insert);
    REG(liva_set_contains);
    REG(liva_set_remove);

    // Random
    REG(liva_rand_int);
    REG(liva_rand_float);
    REG(liva_rand_seed);
    REG(liva_rand_i64);
    REG(liva_rand_uuid);
    REG(liva_rand_uuid_v7);

    // System
    REG(liva_init_args);
    REG(liva_env_get);
    REG(liva_args);
    REG(liva_args_free);
    REG(liva_exec);
    REG(liva_exec_output);
    REG(liva_process_start);
    REG(liva_process_wait);
    REG(liva_process_kill);
    REG(liva_process_read);
    REG(liva_process_close);

    // Time
    REG(liva_clock);
    REG(liva_clock_ms);
    REG(liva_sleep);

    // Regex
    REG(liva_regex_match);
    REG(liva_regex_find);
    REG(liva_regex_find_all);
    REG(liva_regex_replace);
    REG(liva_regex_find_groups);
    REG(liva_regex_compile);
    REG(liva_regex_test);
    REG(liva_regex_exec);
    REG(liva_regex_exec_groups);
    REG(liva_regex_replace_compiled);
    REG(liva_regex_free);

    // HTTP
    REG(liva_http_get);
    REG(liva_http_post);
    REG(liva_http_put);
    REG(liva_http_patch);
    REG(liva_http_delete);
    REG(liva_http_response_free);
    REG(liva_http_response_status);
    REG(liva_http_response_body);
    REG(liva_http_response_header);

    // Async/Coroutine
    REG(liva_task_complete);
    REG(liva_task_is_done);
    REG(liva_task_get_handle);
    REG(liva_task_set_parent);
    REG(liva_task_destroy);
    REG(liva_task_cancel);
    REG(liva_task_is_cancelled);
    REG(liva_coro_resume);
    REG(liva_coro_destroy);
    REG(liva_scheduler_run);
    REG(liva_async_sleep);

    // Channel
    REG(liva_channel_create);
    REG(liva_channel_send);
    REG(liva_channel_receive);
    REG(liva_channel_close);
    REG(liva_channel_len);
    REG(liva_channel_free);

    // TaskGroup
    REG(liva_task_group_create);
    REG(liva_task_group_spawn);
    REG(liva_task_group_await_all);
    REG(liva_task_group_cancel_all);
    REG(liva_task_group_count);
    REG(liva_task_group_free);

    // JSON (DOM API — old string-based API removed)

    // Logging
    REG(liva_log_debug);
    REG(liva_log_info);
    REG(liva_log_warn);
    REG(liva_log_error);
    REG(liva_log_set_level);

    // Assert
    REG(liva_assert);
    REG(liva_assert_msg);
    REG(liva_assert_eq);
    REG(liva_assert_eq_str);
    REG(liva_assert_eq_float);

    // Test framework
    REG(liva_test_begin);
    REG(liva_test_run);
    REG(liva_test_end);
    REG(liva_test_fail);

    // Date/Time
    REG(liva_date_now);
    REG(liva_time_now);
    REG(liva_datetime_now);
    REG(liva_date_format);
    REG(liva_date_year);
    REG(liva_date_month);
    REG(liva_date_day);
    REG(liva_date_weekday);
    REG(liva_iso_format_utc);
    REG(liva_iso_parse);

    // Encoding
    REG(liva_base64_encode);
    REG(liva_base64_decode);
    REG(liva_hex_encode);
    REG(liva_hex_decode);
    REG(liva_crc32);
    REG(liva_base64_url_encode);
    REG(liva_base64_url_decode);
    REG(liva_jwt_hs256_sig);
    REG(liva_jwt_hs512_sig);
    REG(liva_const_time_eq);
    REG(liva_array_clone);
    REG(liva_str_to_bytes);
    REG(liva_bytes_to_str);
    REG(liva_hex_encode_bytes);
    REG(liva_hex_decode_bytes);
    REG(liva_base64_url_encode_bytes);
    REG(liva_base64_url_decode_bytes);
    REG(liva_gzip_encode_bytes);
    REG(liva_gzip_decode_bytes);

    // Mutex
    REG(liva_mutex_create);
    REG(liva_mutex_lock);
    REG(liva_mutex_unlock);
    REG(liva_mutex_try_lock);
    REG(liva_mutex_free);

    // Atomic
    REG(liva_atomic_create);
    REG(liva_atomic_load);
    REG(liva_atomic_store);
    REG(liva_atomic_add);
    REG(liva_atomic_sub);
    REG(liva_atomic_cas);
    REG(liva_atomic_free);

    // RWLock
    REG(liva_rwlock_create);
    REG(liva_rwlock_read_lock);
    REG(liva_rwlock_read_unlock);
    REG(liva_rwlock_write_lock);
    REG(liva_rwlock_write_unlock);
    REG(liva_rwlock_try_read_lock);
    REG(liva_rwlock_try_write_lock);
    REG(liva_rwlock_free);

    // ConditionVariable
    REG(liva_condvar_create);
    REG(liva_condvar_wait);
    REG(liva_condvar_notify_one);
    REG(liva_condvar_notify_all);
    REG(liva_condvar_free);

    // Channel non-blocking
    REG(liva_channel_try_send);
    REG(liva_channel_try_receive);

    // Crypto (hashes, HMAC)
    REG(liva_sha256);
    REG(liva_md5);
    REG(liva_hmac_sha256);
    REG(liva_sha1);
    REG(liva_sha512);
    REG(liva_hmac_sha1);
    REG(liva_hmac_sha512);

    // TOML
    REG(liva_toml_parse);
    REG(liva_toml_get_string);
    REG(liva_toml_get_int);
    REG(liva_toml_get_bool);
    REG(liva_toml_has_key);
    REG(liva_toml_free);

    // Benchmark
    REG(liva_bench_start);
    REG(liva_bench_iter);
    REG(liva_bench_done);
    REG(liva_bench_report);
    REG(liva_bench_reset);

    #undef REG

    llvm::cantFail(mainDylib.define(llvm::orc::absoluteSymbols(std::move(symbols))));
}

std::unique_ptr<JITEngine> JITEngine::create() {
    // Initialize native target (idempotent)
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    auto builder = llvm::orc::LLJITBuilder();
    auto jit = builder.create();
    if (!jit) {
        llvm::consumeError(jit.takeError());
        return nullptr;
    }

    auto &mainDylib = (*jit)->getMainJITDylib();
    // Add process symbols as fallback (works on Linux/macOS, partial on Windows)
    auto gen = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        (*jit)->getDataLayout().getGlobalPrefix());
    if (!gen) {
        llvm::consumeError(gen.takeError());
        return nullptr;
    }
    mainDylib.addGenerator(std::move(*gen));

    // Register all runtime symbols as absolute symbols (needed on Windows)
    registerRuntimeSymbols(**jit);

    return std::unique_ptr<JITEngine>(new JITEngine(std::move(*jit)));
}

int JITEngine::evaluate(std::unique_ptr<llvm::LLVMContext> ctx,
                         std::unique_ptr<llvm::Module> module,
                         std::string &errorOut) {
    // Create a fresh JITDylib per evaluation to avoid symbol conflicts
    std::string dlName = "__repl_eval_" + std::to_string(evalCounter_++);
    auto &es = jit_->getExecutionSession();
    auto dl = es.createJITDylib(dlName);
    if (!dl) {
        errorOut = llvm::toString(dl.takeError());
        return -1;
    }

    // Link against main dylib for process symbols (runtime functions)
    dl->addToLinkOrder(jit_->getMainJITDylib());

    // Add module to the fresh dylib
    auto tsm = llvm::orc::ThreadSafeModule(std::move(module), std::move(ctx));
    if (auto err = jit_->addIRModule(*dl, std::move(tsm))) {
        errorOut = llvm::toString(std::move(err));
        return -1;
    }

    // Look up main
    auto mainSym = jit_->lookup(*dl, "main");
    if (!mainSym) {
        errorOut = llvm::toString(mainSym.takeError());
        return -1;
    }

    // Execute main
    auto *mainFn = mainSym->toPtr<int()>();
    int result = mainFn();

    // Clean up dylib
    if (auto err = es.removeJITDylib(*dl))
        llvm::consumeError(std::move(err));

    return result;
}

} // namespace liva

#endif // LIVA_HAS_LLVM
