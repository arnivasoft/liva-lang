# Liva Language — JetBrains IDE Support

JetBrains IDE'leri (IntelliJ IDEA, CLion, PyCharm, vb.) için Liva dili desteği.

## Syntax Highlighting

### TextMate Bundle

JetBrains IDE'leri TextMate grammar dosyalarını destekler:

1. **Settings** → **Editor** → **TextMate Bundles** → **+** (Add)
2. Bu dizini seçin: `editors/jetbrains/`
3. IDE'yi yeniden başlatın
4. `.liva` dosyaları otomatik olarak syntax highlighting ile açılır

## LSP Desteği

### Yöntem 1 — LSP4IJ Plugin (Önerilen)

[LSP4IJ](https://plugins.jetbrains.com/plugin/23257-lsp4ij), JetBrains IDE'leri için LSP istemcisi sağlar:

1. **Settings** → **Plugins** → **Marketplace** → "LSP4IJ" ara ve kur
2. IDE'yi yeniden başlatın
3. **Settings** → **Languages & Frameworks** → **Language Servers**
4. **+** (Add) → **New Language Server**
   - **Name:** Liva
   - **Command:** `livac lsp`
   - **File patterns:** `*.liva`
5. **Apply** → **OK**

Desteklenen özellikler:
- Completion (otomatik tamamlama)
- Hover (imza/dokümantasyon)
- Go to Definition
- Find References
- Rename
- Code Actions
- Diagnostics
- Inlay Hints
- Call Hierarchy

### Yöntem 2 — Built-in LSP (2024.2+)

JetBrains 2024.2 ve sonrası, sınırlı built-in LSP API desteği sunar.
Şu anda plugin geliştirme gerektirir — LSP4IJ daha pratiktir.

## DAP Desteği

JetBrains IDE'lerinde DAP doğrudan desteklenmez.
Debug için `livac dap` komutunu bir terminal DAP istemcisi ile kullanabilirsiniz.

## Gereksinimler

- JetBrains IDE 2023.1+
- `livac` PATH'te erişilebilir olmalı
- LSP için: [LSP4IJ](https://plugins.jetbrains.com/plugin/23257-lsp4ij) plugin
