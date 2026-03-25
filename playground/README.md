# Liva Playground

Browser-based Liva code editor and runner.

## Quick Start

Open `index.html` in any modern browser:

```bash
# macOS
open playground/index.html

# Linux
xdg-open playground/index.html

# Windows
start playground/index.html
```

## Features

- Syntax-aware code editor with tab support
- 7 built-in examples (Hello World, Fibonacci, Structs, Closures, Generics, Pattern Matching, Async)
- Basic syntax checking (brace/paren/bracket balance, main function detection)
- Simulated program output
- Share code via URL (base64 encoded)
- Resizable editor/output panels
- Keyboard shortcut: Ctrl+Enter to run
- Dark theme (Catppuccin Mocha)

## Future: WASM Compilation

The playground currently simulates execution. To enable real compilation:

1. Compile `livac` to WASM using Emscripten
2. Load the WASM module in the browser
3. Replace `simulateExecution()` with actual WASM compile + run

```bash
# Build livac for WASM (requires Emscripten SDK)
emcc src/main.cpp -o playground/livac.js \
  -s WASM=1 -s EXPORTED_FUNCTIONS='["_compile"]' \
  -s ALLOW_MEMORY_GROWTH=1
```

## Deployment

The playground is a single HTML file with no dependencies. Deploy to any static hosting:

```bash
# GitHub Pages, Netlify, Vercel, etc.
cp playground/index.html dist/
```
