# Liva Hata Ayiklama Rehberi

## Baslangic

Debug bilgisi ile derleyin:

```bash
livac -g main.liva             # Debug bilgisi
livac --debug main.liva        # Debug build (O0 + debug bilgisi)
```

DAP (Debug Adapter Protocol) sunucusunu baslatin:

```bash
livac dap                      # stdio uzerinden DAP baslat
```

## VS Code Yapilandirmasi

### launch.json

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Liva Programini Debug Et",
            "type": "liva",
            "request": "launch",
            "program": "${workspaceFolder}/main.liva",
            "stopOnEntry": false,
            "cwd": "${workspaceFolder}"
        }
    ]
}
```

## Neovim DAP Yapilandirmasi

[nvim-dap](https://github.com/mfussenegger/nvim-dap) ile:

```lua
local dap = require('dap')

dap.adapters.liva = {
    type = 'executable',
    command = 'livac',
    args = { 'dap' },
}

dap.configurations.liva = {
    {
        name = 'Liva Debug',
        type = 'liva',
        request = 'launch',
        program = '${file}',
        cwd = vim.fn.getcwd(),
    },
}
```

## Breakpoint Turleri

### Kosullu Breakpoint

Sadece kosul dogru oldugunda durur:

```json
{
    "line": 15,
    "condition": "x > 100"
}
```

VS Code'da: gutter'a sag tikla > "Add Conditional Breakpoint..."

### Hit Count Breakpoint

N kez vurulduktan sonra durur:

```json
{
    "line": 20,
    "hitCondition": "5"
}
```

Desteklenen operatorler: `=N` (tam N), `>N` (N'den fazla), `>=N`, `%N` (her N'inci)

### Logpoint

Durmadan mesaj yazdirir:

```json
{
    "line": 25,
    "logMessage": "x = {x}, y = {y}"
}
```

`{suslu parantez}` icindeki degiskenler degerlendirilir.

## Watch Ifadeleri

Debug sirasinda degerleri izlemek icin watch ifadeleri ekleyin:

```
x + y
array.length
point.x * 2
result == nil
```

Desteklenen ifadeler: aritmetik, karsilastirma, mantiksal, member erisimi, parantezler, literaller.

## Adim Adim Calistirma

| Komut | VS Code | Neovim |
|-------|---------|--------|
| Devam | F5 | `:lua require'dap'.continue()` |
| Uzerinden Atla | F10 | `:lua require'dap'.step_over()` |
| Icine Gir | F11 | `:lua require'dap'.step_into()` |
| Disina Cik | Shift+F11 | `:lua require'dap'.step_out()` |

## Debug Bilgi Turleri

Derleyici su tipler icin DWARF debug bilgisi uretir:
- Fonksiyonlar, parametreler, donus tipleri
- Yerel degiskenler, konum bilgisi
- Struct alanlari, tipler ve offset'ler
- Optional, Array, Enum debug gosterimleri

## Sorun Giderme

**Breakpoint tetiklenmiyor?** `-g` veya `--debug` ile derlediginizden emin olun.

**Degiskenler `<optimized out>` gosteriyor?** `-O0` kullanin.

**DAP sunucusu baslamiyor?** `livac dap` komutunu dogrudan test edin.
