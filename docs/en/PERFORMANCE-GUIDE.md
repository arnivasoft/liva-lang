# Liva Performance Tuning Guide

## Optimization Levels

```bash
livac -O0 main.liva          # No optimization (fastest compile, debug-friendly)
livac -O1 main.liva          # Basic optimizations
livac -O2 main.liva          # Standard optimizations (recommended for release)
livac -O3 main.liva          # Aggressive optimizations (may increase binary size)
livac --release main.liva    # Shorthand for -O2 + no debug info
livac --debug main.liva      # Shorthand for -O0 + debug info
```

| Level | Compile Speed | Runtime Speed | Binary Size | Debug Quality |
|-------|--------------|---------------|-------------|---------------|
| `-O0` | Fastest | Slowest | Largest | Best |
| `-O1` | Fast | Moderate | Moderate | Good |
| `-O2` | Moderate | Fast | Moderate | Limited |
| `-O3` | Slowest | Fastest | May grow | Limited |

## Link-Time Optimization (LTO)

LTO enables cross-module optimization at link time:

```toml
# liva.toml
[build]
lto = "thin"    # Thin LTO (recommended: good speed/quality tradeoff)
# lto = "full"  # Full LTO (best optimization, slower link)
```

```bash
livac --lto=thin main.liva
livac --lto=full main.liva
```

**Thin LTO** is recommended for most projects — it provides ~80% of Full LTO's benefits with much faster link times.

## Profile-Guided Optimization (PGO)

PGO uses runtime profiling data to optimize hot paths:

```bash
# Step 1: Build with profiling instrumentation
livac --pgo=generate -O2 main.liva -o main_instrumented

# Step 2: Run the instrumented binary with representative workload
./main_instrumented

# Step 3: Build with profile data
livac --pgo=use --pgo-profile=default.profraw -O2 main.liva -o main_optimized
```

PGO typically provides 10-20% speedup for CPU-bound programs.

## Compile-Time Profiling

Use `--dump-timings` to identify compilation bottlenecks:

```bash
livac --dump-timings main.liva
```

Output:
```
=== Compilation Timings ===
  Parse:    2.1 ms
  Sema:    15.3 ms
  IRGen:   28.7 ms
  Optimize: 12.4 ms
  Emit:     5.1 ms
  Link:    45.2 ms
  Total:  108.8 ms

=== Monomorphization Stats ===
  Functions:  42 (cache hits: 18)
  Methods:    31 (cache hits: 12)
  Structs:    15 (cache hits: 5)
```

### Interpreting Timings

| Phase | What It Does | If Slow... |
|-------|-------------|------------|
| **Parse** | Tokenize + build AST | Very large files; split into modules |
| **Sema** | Type checking + ownership | Complex generics or deep trait hierarchies |
| **IRGen** | Generate LLVM IR | Heavy monomorphization; reduce generic usage |
| **Optimize** | LLVM optimization passes | Lower opt level (-O1 vs -O2) |
| **Emit** | Write object file | Normal; proportional to code size |
| **Link** | Link object files to executable | Use Thin LTO; reduce dependencies |

## Incremental Builds

Liva's build cache avoids recompiling unchanged files:

```bash
livac build                  # First build: compiles everything
livac build                  # Second build: only changed files recompile
livac build --rebuild        # Force full rebuild
livac clean                  # Clear build cache
```

The cache stores compiled `.o` files in `.liva-cache/` with:
- **mtime fast-path**: Skips hashing if file modification time unchanged
- **FNV-1a hash**: Content-based staleness detection
- **Per-file tracking**: Only recompiles modified source files

## Monomorphization Tips

Generics are monomorphized (specialized for each type). Excessive generic usage increases compile time and binary size.

**Reduce monomorphization cost:**

```liva
// Instead of many generic instantiations:
func process<T>(items: [T]) { ... }
// Called with i32, f64, String, Point, Color → 5 specializations

// Consider using trait objects for runtime dispatch:
func process(items: [dyn Processable]) { ... }
// Single function, no monomorphization
```

## Build Configuration

```toml
# liva.toml
[project]
name = "myapp"
version = "1.0.0"
entry = "src/main.liva"

[build]
opt-level = 2       # 0, 1, 2, or 3
debug = false        # Include debug info
lto = "thin"         # "none", "thin", or "full"
jobs = 0             # Parallel compilation (0 = auto-detect CPU count)
```

## Cross-Compilation

```bash
livac --target x86_64-unknown-linux-gnu main.liva
livac --target aarch64-apple-darwin main.liva
livac --target wasm32-unknown-unknown main.liva
```

WASM builds automatically add `-nostdlib -Wl,--no-entry -Wl,--export-all`.
