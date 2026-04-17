;;; liva-mode.el --- Major mode for the Liva programming language -*- lexical-binding: t; -*-

;; Author: Liva Language Team
;; Version: 0.1.0
;; Keywords: languages liva
;; Package-Requires: ((emacs "27.1"))

;;; Commentary:

;; Major mode for editing Liva source files (.liva).
;; Provides syntax highlighting, indentation, and comment support.
;; Use with eglot or lsp-mode for LSP integration.

;;; Code:

(defgroup liva nil
  "Major mode for the Liva language."
  :prefix "liva-"
  :group 'languages)

(defcustom liva-indent-offset 4
  "Number of spaces for each indentation level in Liva."
  :type 'integer
  :group 'liva)

;; --- Syntax Table ---

(defvar liva-mode-syntax-table
  (let ((table (make-syntax-table)))
    ;; C-style comments: // and /* */
    (modify-syntax-entry ?/ ". 124b" table)
    (modify-syntax-entry ?* ". 23" table)
    (modify-syntax-entry ?\n "> b" table)
    ;; Strings
    (modify-syntax-entry ?\" "\"" table)
    ;; Underscores are word constituents
    (modify-syntax-entry ?_ "w" table)
    ;; Punctuation
    (modify-syntax-entry ?{ "(}" table)
    (modify-syntax-entry ?} "){" table)
    (modify-syntax-entry ?[ "(]" table)
    (modify-syntax-entry ?] ")[" table)
    (modify-syntax-entry ?\( "()" table)
    (modify-syntax-entry ?\) ")(" table)
    table)
  "Syntax table for `liva-mode'.")

;; --- Font Lock (Syntax Highlighting) ---

(defvar liva-font-lock-keywords
  (let* ((keywords-control '("if" "else" "while" "for" "in" "match" "case"
                             "return" "break" "continue" "guard" "try" "test"))
         (keywords-decl '("func" "let" "var" "const" "struct" "enum" "impl"
                          "protocol" "import" "pub" "type" "class" "extern" "macro"
                          "extension" "subscript"))
         (keywords-modifier '("override" "private" "public" "open" "internal"
                              "fileprivate" "static" "final" "dyn" "comptime"
                              "convenience" "lazy"))
         (keywords-op '("as" "is" "ref" "mut" "where"))
         (keywords-async '("async" "await" "yield"))
         (keywords-accessor '("get" "set" "willSet" "didSet"))
         (keywords-special '("self" "super" "newValue" "oldValue"))
         (keywords-special-func '("init" "deinit"))
         (constants '("true" "false" "nil"))
         (types '("i8" "i16" "i32" "i64" "u8" "u16" "u32" "u64"
                  "f32" "f64" "bool" "string" "void" "Map" "Set"))
         (builtins '("println" "print" "len" "push" "pop" "append" "readLine"
                     "sqrt" "abs" "pow" "sin" "cos" "tan" "log" "ceil" "floor"
                     "round" "min" "max" "randInt" "randFloat" "toString"
                     "parseInt" "parseFloat" "parseInt64" "charToString"))
         ;; Regex builders
         (keywords-control-re (regexp-opt keywords-control 'words))
         (keywords-decl-re (regexp-opt keywords-decl 'words))
         (keywords-modifier-re (regexp-opt keywords-modifier 'words))
         (keywords-op-re (regexp-opt keywords-op 'words))
         (keywords-async-re (regexp-opt keywords-async 'words))
         (keywords-accessor-re (regexp-opt keywords-accessor 'words))
         (keywords-special-re (regexp-opt keywords-special 'words))
         (keywords-special-func-re (regexp-opt keywords-special-func 'words))
         (constants-re (regexp-opt constants 'words))
         (types-re (regexp-opt types 'words))
         (builtins-re (concat (regexp-opt builtins 'words) "\\s-*(")))
    `(
      ;; Function definitions: func name(
      ("\\<func\\>\\s-+\\([a-zA-Z_][a-zA-Z0-9_]*\\)\\s-*(" 1 font-lock-function-name-face)
      ;; async func name(
      ("\\<async\\>\\s-+\\<func\\>\\s-+\\([a-zA-Z_][a-zA-Z0-9_]*\\)\\s-*(" 1 font-lock-function-name-face)
      ;; Struct/class/enum/protocol definitions
      ("\\<struct\\>\\s-+\\([a-zA-Z_][a-zA-Z0-9_]*\\)" 1 font-lock-type-face)
      ("\\<class\\>\\s-+\\([a-zA-Z_][a-zA-Z0-9_]*\\)" 1 font-lock-type-face)
      ("\\<enum\\>\\s-+\\([a-zA-Z_][a-zA-Z0-9_]*\\)" 1 font-lock-type-face)
      ("\\<protocol\\>\\s-+\\([a-zA-Z_][a-zA-Z0-9_]*\\)" 1 font-lock-type-face)
      ("\\<impl\\>\\s-+\\([a-zA-Z_][a-zA-Z0-9_]*\\)" 1 font-lock-type-face)
      ;; Type alias
      ("\\<type\\>\\s-+\\([a-zA-Z_][a-zA-Z0-9_]*\\)\\s-*=" 1 font-lock-type-face)
      ;; Keywords
      (,keywords-control-re . font-lock-keyword-face)
      (,keywords-decl-re . font-lock-keyword-face)
      (,keywords-modifier-re . font-lock-keyword-face)
      (,keywords-op-re . font-lock-keyword-face)
      (,keywords-async-re . font-lock-keyword-face)
      (,keywords-accessor-re . font-lock-keyword-face)
      ;; Special identifiers
      (,keywords-special-re . font-lock-builtin-face)
      (,keywords-special-func-re . font-lock-function-name-face)
      ;; Constants
      (,constants-re . font-lock-constant-face)
      ;; Built-in types
      (,types-re . font-lock-type-face)
      ;; Built-in functions
      (,builtins-re 1 font-lock-builtin-face)
      ;; PascalCase type names
      ("\\<[A-Z][a-zA-Z0-9_]*\\>" . font-lock-type-face)
      ;; Numeric literals
      ("\\<0[xX][0-9a-fA-F_]+\\>" . font-lock-constant-face)
      ("\\<0[bB][01_]+\\>" . font-lock-constant-face)
      ("\\<0[oO][0-7_]+\\>" . font-lock-constant-face)
      ("\\<[0-9][0-9_]*\\.[0-9][0-9_]*\\([eE][+-]?[0-9]+\\)?\\>" . font-lock-constant-face)
      ("\\<[0-9][0-9_]*\\>" . font-lock-constant-face)
      ;; Operators
      ("->" . font-lock-keyword-face)
      ("=>" . font-lock-keyword-face)
      ("::" . font-lock-keyword-face)
      ("\\?\\?" . font-lock-keyword-face)
      ("\\?\\." . font-lock-keyword-face)))
  "Font lock keywords for `liva-mode'.")

;; --- Indentation ---

(defun liva-indent-line ()
  "Indent current line as Liva code."
  (interactive)
  (let ((indent (liva--calculate-indent)))
    (when indent
      (save-excursion
        (beginning-of-line)
        (delete-horizontal-space)
        (indent-to indent))
      (when (< (current-column) indent)
        (move-to-column indent)))))

(defun liva--calculate-indent ()
  "Calculate the indentation for the current line."
  (save-excursion
    (beginning-of-line)
    (if (bobp)
        0
      (let ((cur-indent 0)
            (cur-line (string-trim (thing-at-point 'line t)))
            (prev-indent 0)
            (prev-line ""))
        ;; Find previous non-blank line
        (forward-line -1)
        (while (and (not (bobp))
                    (looking-at-p "^\\s-*$"))
          (forward-line -1))
        (setq prev-indent (current-indentation))
        (setq prev-line (string-trim (thing-at-point 'line t)))
        (setq cur-indent prev-indent)
        ;; Increase indent after { or (
        (when (string-match-p "[{(]\\s-*$" prev-line)
          (setq cur-indent (+ cur-indent liva-indent-offset)))
        ;; Increase indent after =>
        (when (string-match-p "=>\\s-*$" prev-line)
          (setq cur-indent (+ cur-indent liva-indent-offset)))
        ;; Decrease indent for } or )
        (when (string-match-p "^\\s-*[})]" cur-line)
          (setq cur-indent (- cur-indent liva-indent-offset)))
        (max 0 cur-indent)))))

;; --- Major Mode ---

;;;###autoload
(define-derived-mode liva-mode prog-mode "Liva"
  "Major mode for editing Liva source files."
  :syntax-table liva-mode-syntax-table
  (setq-local font-lock-defaults '(liva-font-lock-keywords))
  (setq-local comment-start "// ")
  (setq-local comment-end "")
  (setq-local comment-start-skip "//+\\s-*")
  (setq-local indent-line-function #'liva-indent-line)
  (setq-local indent-tabs-mode nil)
  (setq-local tab-width liva-indent-offset)
  (setq-local electric-indent-chars (append '(?\{ ?\} ?\( ?\)) electric-indent-chars)))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.liva\\'" . liva-mode))

(provide 'liva-mode)

;;; liva-mode.el ends here
