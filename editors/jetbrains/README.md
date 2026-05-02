# Liva Language — JetBrains IDE Support

JetBrains IDE'leri (IntelliJ IDEA, CLion, PyCharm, vb.) için Liva dili desteği.

İki yol var:

1. **Native Plugin** (Önerilen, IDE 2024.2+) — Bu repo'daki
   [`native-plugin/`](native-plugin/) altında derlenebilir Kotlin
   plugin. Lexer-based syntax highlighting + LSP entegrasyonu +
   commenter + brace matcher tek pakette. Build: `cd native-plugin
   && ./gradlew buildPlugin`. Kurulum: oluşan `.zip`'i **Settings →
   Plugins → ⚙ → Install Plugin from Disk** ile yükle.

2. **TextMate Bundle + LSP4IJ** (Eski IDE'ler için) — Aşağıdaki
   talimatları izle. Native plugin desteklenmeyen IDE versiyonları
   için kalır.

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

## Run/Debug Configuration

### Run (Shell Script)

1. **Run** → **Edit Configurations** → **+** → **Shell Script**
2. Yapılandırma:
   - **Name:** Run Liva
   - **Script text:** `livac run $FilePath$`
   - **Working directory:** `$ProjectFileDir$`
3. **Apply** → **OK**

Alternatif olarak build + execute ayrı adımda:

- **Script text:** `livac build $FilePath$ -o $ProjectFileDir$/output && $ProjectFileDir$/output`

> **Not:** `$FilePath$` ve `$ProjectFileDir$` JetBrains path macro'larıdır,
> çalıştırma sırasında otomatik olarak aktif dosya ve proje dizini ile değiştirilir.

### Debug (DAP)

JetBrains IDE'leri DAP protokolünü doğrudan desteklemez, ancak iki yöntem kullanılabilir:

**Yöntem A — LSP4IJ DAP Desteği (Önerilen)**

LSP4IJ 0.4+ sürümü DAP desteği içerir:

1. LSP4IJ plugin'inin kurulu olduğundan emin olun
2. **Settings** → **Languages & Frameworks** → **Language Servers** → Liva sunucusunu seçin
3. **Debug Adapter** sekmesi:
   - **Command:** `livac dap`
   - **File patterns:** `*.liva`
4. **Run** → **Edit Configurations** → **+** → **DAP (LSP4IJ)**
   - **Name:** Debug Liva
   - **Server:** Liva
   - **Request:** launch
   - **Program:** `$FilePath$`
5. Breakpoint koyun ve **Debug** butonuna tıklayın

**Yöntem B — External Tool + Terminal DAP**

1. **Run** → **Edit Configurations** → **+** → **Shell Script**
   - **Name:** Debug Liva (Terminal)
   - **Script text:** `livac build $FilePath$ -g -o $ProjectFileDir$/output_dbg && gdb $ProjectFileDir$/output_dbg`
2. `-g` flag'i DWARF debug bilgisi üretir

### Kısayol Atama

Hızlı erişim için run configuration'a kısayol atayabilirsiniz:

1. **Settings** → **Keymap** → "Run" ara
2. **Run 'Run Liva'** veya **Debug 'Debug Liva'** öğesine sağ tıklayın
3. **Add Keyboard Shortcut** ile istediğiniz tuşu atayın

## Gereksinimler

- JetBrains IDE 2023.1+
- `livac` PATH'te erişilebilir olmalı
- LSP için: [LSP4IJ](https://plugins.jetbrains.com/plugin/23257-lsp4ij) plugin
- DAP için: LSP4IJ 0.4+ veya terminal debugger (gdb/lldb)
