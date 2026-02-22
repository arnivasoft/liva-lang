# Liva Language — Neovim Support

Neovim için Liva dili syntax highlighting, LSP ve DAP desteği.

## Kurulum

### 1. Syntax Highlighting (Manuel)

Bu dizini Neovim runtime path'ine kopyalayın:

```bash
# Linux/macOS
cp -r editors/neovim/* ~/.config/nvim/

# Windows
xcopy /E editors\neovim\* %LOCALAPPDATA%\nvim\
```

Veya symlink oluşturun:

```bash
# Linux/macOS
ln -s $(pwd)/editors/neovim/syntax/liva.vim ~/.config/nvim/syntax/liva.vim
ln -s $(pwd)/editors/neovim/ftdetect/liva.vim ~/.config/nvim/ftdetect/liva.vim
ln -s $(pwd)/editors/neovim/indent/liva.vim ~/.config/nvim/indent/liva.vim
ln -s $(pwd)/editors/neovim/ftplugin/liva.vim ~/.config/nvim/ftplugin/liva.vim
```

### 2. LSP Yapılandırması (nvim-lspconfig)

`init.lua` dosyanıza ekleyin:

```lua
-- Filetype tanımla
vim.filetype.add({ extension = { liva = "liva" } })

-- LSP yapılandırması
local lspconfig = require('lspconfig')
local configs = require('lspconfig.configs')

if not configs.liva then
  configs.liva = {
    default_config = {
      cmd = { 'livac', 'lsp' },
      filetypes = { 'liva' },
      root_dir = lspconfig.util.root_pattern('liva.toml', '.git'),
      settings = {},
    },
  }
end

lspconfig.liva.setup{}
```

Desteklenen LSP özellikleri:
- Completion (otomatik tamamlama)
- Hover (imza/dokümantasyon)
- Go to Definition
- Find References
- Rename
- Semantic Tokens
- Code Actions
- Folding Ranges
- Inlay Hints
- Call Hierarchy
- Diagnostics

### 3. DAP Yapılandırması (nvim-dap)

```lua
local dap = require('dap')

dap.adapters.liva = {
  type = 'executable',
  command = 'livac',
  args = { 'dap' },
}

dap.configurations.liva = {
  {
    type = 'liva',
    request = 'launch',
    name = 'Launch Liva Program',
    program = '${file}',
  },
}
```

### 4. Doğrulama

1. Bir `.liva` dosyası açın
2. Syntax renklendirmenin çalıştığını kontrol edin
3. `:LspInfo` ile LSP bağlantısını doğrulayın
4. `:LspLog` ile hata olup olmadığını kontrol edin

### Gereksinimler

- Neovim 0.8+
- `livac` PATH'te erişilebilir olmalı
- LSP için: [nvim-lspconfig](https://github.com/neovim/nvim-lspconfig)
- DAP için: [nvim-dap](https://github.com/mfussenegger/nvim-dap)
