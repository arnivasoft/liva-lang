# Liva Debugging Guide

## Getting Started

Build with debug info enabled:

```bash
livac -g main.liva             # Debug info
livac --debug main.liva        # Debug build (O0 + debug info)
```

Start the DAP (Debug Adapter Protocol) server:

```bash
livac dap                      # Start DAP server on stdio
```

## VS Code Configuration

### launch.json

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug Liva Program",
            "type": "liva",
            "request": "launch",
            "program": "${workspaceFolder}/main.liva",
            "stopOnEntry": false,
            "cwd": "${workspaceFolder}"
        }
    ]
}
```

### settings.json

```json
{
    "liva.dap.path": "livac",
    "liva.dap.args": ["dap"]
}
```

## Neovim DAP Configuration

Using [nvim-dap](https://github.com/mfussenegger/nvim-dap):

```lua
local dap = require('dap')

dap.adapters.liva = {
    type = 'executable',
    command = 'livac',
    args = { 'dap' },
}

dap.configurations.liva = {
    {
        name = 'Debug Liva',
        type = 'liva',
        request = 'launch',
        program = '${file}',
        cwd = vim.fn.getcwd(),
    },
}
```

## Breakpoints

### Basic Breakpoints

Set breakpoints by line number. In VS Code, click the gutter. In DAP:

```json
{
    "command": "setBreakpoints",
    "arguments": {
        "source": { "path": "main.liva" },
        "breakpoints": [
            { "line": 10 }
        ]
    }
}
```

### Conditional Breakpoints

Break only when a condition is true:

```json
{
    "line": 15,
    "condition": "x > 100"
}
```

In VS Code: right-click gutter > "Add Conditional Breakpoint..."

### Hit Count Breakpoints

Break after N hits:

```json
{
    "line": 20,
    "hitCondition": "5"
}
```

Supported operators: `=N` (exactly N), `>N` (more than N), `>=N`, `%N` (every Nth hit).

### Logpoints

Print a message without stopping:

```json
{
    "line": 25,
    "logMessage": "x = {x}, y = {y}"
}
```

In VS Code: right-click gutter > "Add Logpoint..."

Variables in `{braces}` are evaluated and interpolated.

### Exception Breakpoints

Break automatically when runtime errors occur:

- **All Exceptions**: Break on any runtime error or panic
- **Uncaught Exceptions**: Break only on unhandled errors (default: enabled)

In VS Code: open the Breakpoints panel and check "All Exceptions" or "Uncaught Exceptions".

DAP request:
```json
{
    "command": "setExceptionBreakpoints",
    "arguments": {
        "filters": ["all"]
    }
}
```

Available filters: `"all"` (every error), `"uncaught"` (unhandled only).

## Watch Expressions

Add watch expressions to monitor values during debugging:

```
x + y
array.length
point.x * 2
result == nil
```

The expression evaluator supports:
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Logical: `&&`, `||`, `!`
- Member access: `obj.field`
- Parentheses: `(a + b) * c`
- Literals: integers, floats, strings, booleans, nil

## Stepping

| Command | VS Code | Neovim (nvim-dap) |
|---------|---------|-------------------|
| Continue | F5 | `:lua require'dap'.continue()` |
| Step Over | F10 | `:lua require'dap'.step_over()` |
| Step Into | F11 | `:lua require'dap'.step_into()` |
| Step Out | Shift+F11 | `:lua require'dap'.step_out()` |
| Pause | F6 | `:lua require'dap'.pause()` |
| Stop | Shift+F5 | `:lua require'dap'.terminate()` |

## Debug Info Types

The compiler generates DWARF debug info for:

- **Functions**: Name, parameters, return type
- **Variables**: Local variables, parameters with location info
- **Structs**: Field names, types, and offsets
- **Optionals**: Represented as DWARF structure types
- **Arrays**: Dynamic array debug representation
- **Enums**: Enum variants with discriminant info

## Troubleshooting

**Breakpoint not hit?**
- Ensure you compiled with `-g` or `--debug`
- Check the source path matches (absolute vs relative)
- Verify the line has executable code (not a comment or blank line)

**Variables show `<optimized out>`?**
- Use `-O0` (or `--debug`) to disable optimizations
- Higher optimization levels may eliminate variables

**DAP server doesn't start?**
- Check `livac dap` runs without error
- Verify the VS Code extension or Neovim plugin is configured correctly
- Check the DAP adapter path in your editor settings
