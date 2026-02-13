# Liva Language for Visual Studio Code

Language support for the [Liva](https://github.com/liva-lang/liva-lang) programming language -- a modern, compiled language with Swift-like syntax and Rust-style ownership/borrowing.

## Features

### LSP-Powered (12 Features)

All LSP features are provided by the `livac lsp` language server:

- **Diagnostics** -- Real-time error and warning reporting (type mismatches, ownership violations, unused variables, unreachable code, shadowed variables).
- **Autocompletion** -- Keywords, built-in functions, and symbol completions.
- **Hover** -- Type and signature information for functions, variables, structs, and enums.
- **Go-to-definition** -- Jump to the definition of any symbol.
- **References** -- Find all references of a symbol across the document.
- **Rename** -- Rename a symbol and all its references.
- **Signature help** -- Parameter hints for function calls (15 built-in function signatures + user-defined).
- **Document symbols** -- Outline view of functions, structs, enums, and protocols.
- **Semantic tokens** -- Compiler-accurate syntax highlighting beyond TextMate grammar.
- **Formatting** -- Automatic code formatting.
- **Folding ranges** -- Fold/unfold code blocks, structs, enums, functions, and region markers.
- **Document highlight** -- Highlight all occurrences of a symbol under the cursor.

### Editor Features

- **Syntax highlighting** -- Full TextMate grammar covering keywords, types, operators, strings (with interpolation and escape sequences), numbers, comments, closures, and more.
- **Bracket matching and auto-closing** -- Automatic bracket, parenthesis, angle bracket, and quote pairing.
- **Code folding** -- Fold code blocks and `// #region` / `// #endregion` markers.
- **Indentation** -- Automatic indent/outdent for braces.

## Requirements

The extension requires the `livac` compiler for LSP features. Syntax highlighting works without it.

- Install the Liva compiler (`livac`) and ensure it is on your `PATH`, **or**
- Set the path explicitly in VS Code settings:

```json
{
    "liva.livacPath": "/path/to/livac"
}
```

## Installation

### From VSIX (local)

1. Build the extension:
   ```
   cd editors/vscode/liva-lang
   npm install
   npm run compile
   npx vsce package
   ```
2. Install the generated `.vsix` file:
   - In VS Code, open the Command Palette and run **Extensions: Install from VSIX...**
   - Select the `.vsix` file.

### From source (development)

1. Open `editors/vscode/liva-lang` in VS Code.
2. Run `npm install` to install dependencies.
3. Press **F5** to launch a new Extension Development Host window.

## Debugging

The extension provides integrated debugging via LLVM's `lldb-dap` (Debug Adapter Protocol) backend.

### Requirements

- **LLVM with lldb-dap** -- Included with LLVM 16+. On Windows, install LLVM and ensure `C:\LLVM\bin\lldb-dap.exe` exists.
- **Debug info** -- Your program must be compiled with `livac build -g` (the extension does this automatically by default).

### Quick Start

1. Open a Liva project in VS Code.
2. Press **F5** (or **Run > Start Debugging**).
3. If no `launch.json` exists, the extension creates a default one.
4. The extension runs `livac build -g`, then launches the debugger.

### launch.json Example

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "type": "liva",
            "request": "launch",
            "name": "Debug Liva Program",
            "program": "${workspaceFolder}/myproject.exe",
            "args": [],
            "cwd": "${workspaceFolder}",
            "stopOnEntry": false,
            "preBuildTask": true
        }
    ]
}
```

### Configuration Options

| Property       | Type     | Default                  | Description                                |
|----------------|----------|--------------------------|--------------------------------------------|
| `program`      | string   | *derived from liva.toml* | Path to the compiled executable.           |
| `args`         | string[] | `[]`                     | Command-line arguments.                    |
| `cwd`          | string   | `${workspaceFolder}`     | Working directory.                         |
| `stopOnEntry`  | boolean  | `false`                  | Break at program entry point.              |
| `env`          | object   | `{}`                     | Environment variables.                     |
| `preBuildTask` | boolean  | `true`                   | Run `livac build -g` before debugging.     |

### lldb-dap Discovery

The extension searches for `lldb-dap` in this order:

1. `liva.lldbDapPath` setting
2. `LIVA_LLDB_DAP_PATH` environment variable
3. Platform defaults (`C:\LLVM\bin\lldb-dap.exe`, `/opt/homebrew/opt/llvm/bin/lldb-dap`, `/usr/bin/lldb-dap`)
4. `PATH`

To override, add to your VS Code settings:

```json
{
    "liva.lldbDapPath": "C:\\LLVM\\bin\\lldb-dap.exe"
}
```

## Extension Settings

| Setting                     | Default  | Description                                      |
|-----------------------------|----------|--------------------------------------------------|
| `liva.livacPath`            | `livac`  | Path to the livac compiler executable.           |
| `liva.lldbDapPath`          | `""`     | Path to `lldb-dap` executable for debugging.     |
| `liva.enableSemanticTokens` | `true`   | Enable semantic token highlighting from the LSP. |
| `liva.trace.server`         | `off`    | Trace LSP communication (`off`/`messages`/`verbose`). |

## Commands

| Command                      | Description                    |
|------------------------------|--------------------------------|
| `Liva: Restart Server`       | Restart the Liva LSP server.   |

## Supported Liva Syntax

The grammar covers the full Liva language including:

- Variable declarations (`let`, `var`, `const`)
- Functions (`func`, `async func`), closures (`|args| { body }`)
- Control flow (`if`/`else`, `while`, `for`/`in`, `match`/`case`, `guard`, `break`, `continue`, `return`)
- Types (`struct`, `enum`, `protocol`, `impl`, `type` aliases)
- Ownership and borrowing (`ref`, `mut`, `self`)
- Primitive types (`i8`--`u64`, `f32`, `f64`, `bool`, `string`, `void`)
- Collections (`[T]` arrays, `Map<K,V>`, `Set<T>`)
- Operators (`??`, `?.`, `..`, `...`, `->`, `=>`, `::`, bitwise, arithmetic, comparison, logical)
- String interpolation (`\(expr)`) and escape sequences (`\n`, `\t`, `\u{hex}`)
- Numeric literals (decimal, hex `0x`, binary `0b`, octal `0o`, floats with exponents)
- Line comments (`//`) and block comments (`/* */`, nestable)
- Module imports (`import`)
- Async/await, try expressions
- Generics (`<T>`, where clauses, trait bounds)
- Pattern matching (exhaustive, nested, wildcard `_`)
