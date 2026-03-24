# Liva Compiler Plugin Guide

## Overview

Liva's plugin system lets you add custom analysis passes to the compilation pipeline. Plugins can inspect and report on the AST after parsing or after semantic analysis.

## Built-in Plugins

Liva ships with two built-in plugins:

### naming-convention

Checks naming conventions: PascalCase for types (struct, enum, class, protocol) and camelCase for functions.

```toml
[plugins]
naming-convention = true
```

### unused-function

Detects top-level functions that are defined but never called. Excludes `main`, `extern` functions, and methods.

```toml
[plugins]
unused-function = true
```

## Writing a Custom Plugin

### 1. Implement the CompilerPlugin Interface

```cpp
#include "liva/Plugin/PluginAPI.h"

class MyPlugin : public liva::CompilerPlugin {
public:
    std::string getName() const override { return "my-plugin"; }
    std::string getDescription() const override {
        return "Description of what my plugin does";
    }

    // Called after parsing, before semantic analysis
    bool afterParse(TranslationUnit &tu, DiagnosticsEngine &diag) override {
        // Inspect tu.getDeclarations()
        // Report issues via diag.report(loc, diagId, ...)
        return true; // return false to abort compilation
    }

    // Called after semantic analysis, before IR generation
    bool afterSema(TranslationUnit &tu, DiagnosticsEngine &diag) override {
        // AST now has resolved types
        return true;
    }

    // Called with TOML [plugins.my-plugin] key-value pairs
    void configure(const std::map<std::string, std::string> &options) override {
        // Read plugin-specific options
    }
};
```

### 2. Register the Plugin

```cpp
#include "liva/Plugin/PluginRegistry.h"

// In your initialization code:
PluginRegistry registry = PluginRegistry::createWithBuiltins();
registry.registerPlugin(std::make_unique<MyPlugin>());
```

### 3. Configure via liva.toml

```toml
[plugins]
my-plugin = true
naming-convention = false
unused-function = true
```

Set a plugin to `false` to disable it.

## Plugin API Reference

### CompilerPlugin (Base Class)

| Method | Description |
|--------|-------------|
| `getName()` | Return the plugin's unique name |
| `getDescription()` | Return a human-readable description |
| `afterParse(tu, diag)` | Hook: runs after parsing. Return `false` to abort |
| `afterSema(tu, diag)` | Hook: runs after sema. Return `false` to abort |
| `configure(options)` | Receive TOML key-value configuration |
| `isEnabled()` / `setEnabled(bool)` | Check/set enabled state |

### PluginRegistry

| Method | Description |
|--------|-------------|
| `registerPlugin(plugin)` | Add a plugin to the registry |
| `getPlugin(name)` | Look up a plugin by name |
| `runAfterParse(tu, diag)` | Run all enabled afterParse hooks |
| `runAfterSema(tu, diag)` | Run all enabled afterSema hooks |
| `configureFromTOML(doc)` | Load enable/disable from `[plugins]` section |
| `createWithBuiltins()` | Factory: create registry with built-in plugins |
| `size()` | Number of registered plugins |
| `clear()` | Remove all plugins |

### Hook Execution Order

```
Source Code
    |
    v
  [Parse]
    |
    v
  afterParse() <-- NamingConventionPlugin
    |
    v
  [Sema]
    |
    v
  afterSema()  <-- UnusedFunctionPlugin
    |
    v
  [IRGen]
    |
    v
  [CodeGen]
```

Plugins run in registration order. If any plugin returns `false`, the pipeline aborts.

## Diagnostic Reporting

Plugins report issues through `DiagnosticsEngine`:

```cpp
bool afterParse(TranslationUnit &tu, DiagnosticsEngine &diag) override {
    for (auto &decl : tu.getDeclarations()) {
        if (auto *fd = dynamic_cast<FuncDecl *>(decl.get())) {
            if (!isCamelCase(fd->getName())) {
                diag.report(fd->getRange().start,
                           DiagID::warn_plugin_naming_func,
                           fd->getName());
            }
        }
    }
    return true;
}
```

## AST Node Types Available

Plugins can inspect these declaration types:

- `FuncDecl` — functions (`getName()`, `getParams()`, `getBody()`, `isPublic()`, `isAsync()`, `isExtern()`)
- `StructDecl` — structs (`getName()`, `getFields()`, `isPublic()`)
- `EnumDecl` — enums (`getName()`, `getCases()`, `isPublic()`)
- `ClassDecl` — classes (`getName()`, `getMethods()`, `getFields()`, `getParentName()`)
- `ProtocolDecl` — protocols/traits (`getName()`, `getMethods()`)
- `ImplDecl` — impl blocks (`getTypeName()`, `getMethods()`)
- `VarDecl` — variables (`getName()`, `isMutable()`, `hasInit()`)
- `ImportDecl` — imports (`getPath()`)
