# Liva Language — Native JetBrains Plugin

Kotlin-based IntelliJ Platform plugin for Liva. Targets IntelliJ
Platform 2024.2+ (the first release that ships a stable
`LspServerSupportProvider` API).

This is the **native** alternative to the TextMate Bundle + LSP4IJ
setup documented in `../README.md`. Once built, the resulting
`liva-lang-*.zip` installs into any IntelliJ-family IDE — IntelliJ
IDEA, CLion, RustRover, Rider, PyCharm Pro, GoLand, WebStorm — and
provides:

- File type detection for `.liva`
- Lexer-based syntax highlighting (keywords, primitive types,
  numbers, strings, lifetimes, operators, line/block/doc comments)
- A **Liva** color settings page (Settings → Editor → Color Scheme →
  Liva) — every token category is independently customizable
- Line (`//`) and block (`/* */`) commenter integration
- Brace, bracket, and parenthesis matching
- LSP client running `livac lsp` — completion, hover,
  go-to-definition, find references, rename, code actions,
  diagnostics, inlay hints, call hierarchy

## Build

Requires JDK 17 and Gradle (`./gradlew` ships a wrapper).

```bash
cd editors/jetbrains/native-plugin
./gradlew buildPlugin
# -> build/distributions/liva-lang-0.1.0.zip
```

## Install

1. Download the .zip from `build/distributions/` (or release assets)
2. In your IDE: **Settings → Plugins → ⚙ → Install Plugin from Disk**
3. Select the .zip and restart the IDE
4. Open any `.liva` file — highlighting and the LSP client both
   activate automatically

`livac` must be on the IDE's `PATH`. If it isn't, the LSP startup
fails silently (the highlighter still works); fix this by either
installing livac globally or launching the IDE from a shell where
livac resolves.

## Run from sources

For iterative development, the Gradle plugin provides a sandbox:

```bash
./gradlew runIde
```

This launches a clean IDE instance with the plugin auto-loaded.

## Architecture

| File | Role |
|------|------|
| `LivaLanguage.kt` | Singleton `Language` registration |
| `LivaFileType.kt` | `.liva` extension binding |
| `LivaIcons.kt` + `icons/liva.svg` | File icon |
| `lexer/LivaTokenType.kt` | Token element types |
| `lexer/LivaLexer.kt` | Hand-written lexer driving the highlighter |
| `highlighter/LivaSyntaxHighlighter.kt` | Token → color mapping |
| `highlighter/LivaSyntaxHighlighterFactory.kt` | IDE plumbing |
| `highlighter/LivaColorSettingsPage.kt` | Color scheme UI |
| `editor/LivaCommenter.kt` | `//` and `/* */` toggle support |
| `editor/LivaBraceMatcher.kt` | `{} [] ()` matching |
| `lsp/LivaLspServerSupportProvider.kt` | Native LSP integration |

The lexer is deliberately conservative — it only has to drive
highlighting and structural editing. Parsing, semantic analysis,
and refactoring are delegated to `livac lsp`. There is no
Grammar-Kit BNF or PSI tree.

## Compatibility

| IDE build | Behavior |
|-----------|----------|
| 2024.2+   | Full plugin (this README) |
| 2023.3 – 2024.1 | Use TextMate Bundle + LSP4IJ (see parent README) |
| < 2023.3  | TextMate Bundle only |

## License

Same as the rest of the Liva-Lang repository.
