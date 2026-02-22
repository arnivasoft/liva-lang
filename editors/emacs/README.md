# Liva Language — Emacs Support

Emacs için Liva dili major mode, LSP ve DAP desteği.

## Kurulum

### 1. Major Mode (liva-mode)

`liva-mode.el` dosyasını load-path'e ekleyin:

```elisp
;; init.el veya .emacs
(add-to-list 'load-path "/path/to/liva-lang/editors/emacs")
(require 'liva-mode)
```

Veya `use-package` ile:

```elisp
(use-package liva-mode
  :load-path "/path/to/liva-lang/editors/emacs"
  :mode "\\.liva\\'")
```

`.liva` dosyaları otomatik olarak `liva-mode` ile açılır.

### 2. LSP Entegrasyonu

#### Yöntem A — eglot (Emacs 29+ built-in)

```elisp
(with-eval-after-load 'eglot
  (add-to-list 'eglot-server-programs '(liva-mode "livac" "lsp")))
(add-hook 'liva-mode-hook 'eglot-ensure)
```

#### Yöntem B — lsp-mode

```elisp
(with-eval-after-load 'lsp-mode
  (add-to-list 'lsp-language-id-configuration '(liva-mode . "liva"))
  (lsp-register-client
    (make-lsp-client
      :new-connection (lsp-stdio-connection '("livac" "lsp"))
      :activation-fn (lsp-activate-on "liva")
      :server-id 'liva-lsp)))
(add-hook 'liva-mode-hook 'lsp-deferred)
```

### 3. DAP Entegrasyonu (dap-mode)

```elisp
(with-eval-after-load 'dap-mode
  (dap-register-debug-provider "liva"
    (lambda (conf)
      (plist-put conf :dap-server-path '("livac" "dap"))))
  (dap-register-debug-template "Liva Launch"
    (list :type "liva"
          :request "launch"
          :name "Launch Liva Program"
          :program "${file}")))
```

### Desteklenen Özellikler

- Syntax highlighting (font-lock)
- Akıllı indentation (4-space, brace-based)
- Comment desteği (`//` ve `/* */`)
- LSP: completion, hover, goto-def, references, rename, code actions
- DAP: breakpoints, step-in/out/over, variable inspection

### Gereksinimler

- Emacs 27.1+
- `livac` PATH'te erişilebilir olmalı
- LSP için: eglot (Emacs 29+) veya [lsp-mode](https://github.com/emacs-lsp/lsp-mode)
- DAP için: [dap-mode](https://github.com/emacs-lsp/dap-mode)
