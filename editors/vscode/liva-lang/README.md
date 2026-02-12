# Liva Language for Visual Studio Code

Language support for the [Liva](https://github.com/liva-lang/liva-lang) programming language.

## Features

- **Syntax highlighting** -- Full TextMate grammar covering keywords, types, operators, strings (with interpolation and escape sequences), numbers, comments, function definitions, type annotations, closures, and more.
- **Diagnostics** -- Real-time error and warning reporting powered by the Liva compiler.
- **Autocompletion** -- Keyword, built-in, and symbol completions via the LSP server.
- **Hover information** -- Type and signature information for functions, variables, structs, and enums.
- **Go-to-definition** -- Jump to the definition of any symbol.
- **Document symbols** -- Outline view of functions, structs, enums, and protocols.
- **Bracket matching and auto-closing** -- Automatic bracket, parenthesis, and quote pairing.
- **Code folding** -- Fold code blocks and region markers.

## Requirements

The extension requires the `livac` compiler to be available for LSP features (diagnostics, completion, hover, go-to-definition). Syntax highlighting works without it.

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

## Extension Settings

| Setting           | Default  | Description                            |
|-------------------|----------|----------------------------------------|
| `liva.livacPath`  | `livac`  | Path to the livac compiler executable. |

## Supported Liva Syntax

The grammar covers the full Liva language including:

- Variable declarations (`let`, `var`, `const`)
- Functions (`func`, `async func`), closures (`|args| { body }`)
- Control flow (`if`/`else`, `while`, `for`/`in`, `match`/`case`, `guard`, `break`, `continue`, `return`)
- Types (`struct`, `enum`, `protocol`, `impl`, `type` aliases)
- Ownership and borrowing (`ref`, `mut`, `self`)
- Primitive types (`i8`--`u64`, `f32`, `f64`, `bool`, `string`, `void`)
- Collections (`[T]` arrays, `Map<K,V>`, `Set<T>`)
- Operators (`??`, `?.`, `..`, `->`, `=>`, `::`, bitwise, arithmetic, comparison, logical)
- String interpolation (`\(expr)`) and escape sequences
- Numeric literals (decimal, hex `0x`, binary `0b`, octal `0o`, floats)
- Line comments (`//`) and block comments (`/* */`, nestable)
- Module imports (`import`)
- Async/await
