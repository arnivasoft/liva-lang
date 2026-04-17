# Liva Language Support for Notepad++

Syntax highlighting for `.liva` files using Notepad++ User Defined Language (UDL).

## Features

- Syntax highlighting for all Liva keywords, types, and builtins
- Line comments (`//`) and block comments (`/* */`)
- String and character literal highlighting with escape sequences
- Number literals (decimal, hex `0x`, binary `0b`, octal `0o`, underscores)
- Code folding via `{ }` braces
- 120+ builtin function names highlighted

### Keyword Group Colors

| Group | Color | Contents |
|-------|-------|----------|
| **Keywords1** | Blue, bold | Declaration keywords (`func`, `let`, `struct`, `class`, ...) |
| **Keywords2** | Purple, bold | Control flow (`if`, `else`, `for`, `match`, `async`, ...) |
| **Keywords3** | Teal | Types (`i32`, `f64`, `bool`, `string`, `Map`, ...) |
| **Keywords4** | Blue | Constants & special (`true`, `false`, `nil`, `self`, ...) |
| **Keywords5** | Orange, italic | Modifiers (`mut`, `ref`, `dyn`, `override`, `comptime`, ...) |
| **Keywords6** | Brown | Core builtins (`print`, `len`, `sqrt`, `assert`, ...) |
| **Keywords7** | Brown | Stdlib builtins (`fileRead`, `strSplit`, `forEach`, ...) |
| **Keywords8** | Brown | Extended builtins (`httpGet`, `jsonGet`, `sha256`, ...) |

## Installation

### Method 1: Import via Menu

1. Open Notepad++
2. Go to **Language > User Defined Language > Define your language...**
3. Click **Import...**
4. Select `liva-udl.xml` from this directory
5. Restart Notepad++

### Method 2: Manual Copy

1. Copy `liva-udl.xml` to `%AppData%\Notepad++\userDefineLangs\`
   - If the folder doesn't exist, create it
2. Restart Notepad++

After installation, `.liva` files will automatically use Liva syntax highlighting.
You can also manually select it via **Language > Liva**.

## LSP Support (Optional)

For code intelligence (autocomplete, diagnostics, go-to-definition), you can use
the [NppLSP](https://github.com/nicktgn/NppLSP) or
[LSP Client](https://github.com/nicktgn/NppLSP) plugin:

1. Install the LSP plugin from **Plugins > Plugins Admin**
2. Configure Liva language server in the plugin settings:
   - **Command:** `livac lsp`
   - **Transport:** stdio
   - **Language ID:** `liva`
   - **File pattern:** `*.liva`

## Customization

To modify colors or add keywords:

1. Go to **Language > User Defined Language > Define your language...**
2. Select **Liva** from the dropdown
3. Edit keyword lists, colors, or styles as desired
