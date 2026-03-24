; Tree-sitter highlight queries for Liva

; === Keywords ===

["func" "struct" "enum" "impl" "protocol" "import" "class" "macro" "extern" "type" "test"] @keyword
["let" "var" "const"] @keyword.storage
["pub" "override" "private" "dyn" "comptime"] @keyword.modifier
["if" "else" "while" "for" "in" "match" "case" "return" "break" "continue" "guard"] @keyword.control
["async" "await"] @keyword.coroutine
["try"] @keyword.exception
["as"] @keyword.operator
["where"] @keyword.type
["init" "deinit"] @keyword.function
["self"] @variable.builtin
["super"] @variable.builtin
["nil"] @constant.builtin
["true" "false"] @boolean

; === Types ===

(primitive_type) @type.builtin
(named_type (identifier) @type)
(generic_param (identifier) @type.parameter)
(type_arguments (_) @type)
(dyn_type "dyn" @keyword (identifier) @type)

; === Functions ===

(function_declaration name: (identifier) @function)
(function_signature name: (identifier) @function)
(call_expression (identifier) @function.call)
(init_declaration "init" @constructor)
(deinit_declaration "deinit" @constructor)

; === Declarations ===

(struct_declaration name: (identifier) @type.definition)
(enum_declaration name: (identifier) @type.definition)
(class_declaration name: (identifier) @type.definition)
(protocol_declaration name: (identifier) @type.definition)
(type_alias_declaration name: (identifier) @type.definition)
(macro_declaration name: (identifier) @function.macro)
(test_declaration name: (string_literal) @string.special)

(field_declaration name: (identifier) @property)
(enum_case name: (identifier) @constant)
(variable_declaration name: (identifier) @variable)
(parameter name: (identifier) @variable.parameter)

; === Literals ===

(integer_literal) @number
(float_literal) @number.float
(string_literal) @string
(char_literal) @character
(boolean_literal) @boolean
(nil_literal) @constant.builtin

; === Operators ===

["+" "-" "*" "/" "%" "==" "!=" "<" "<=" ">" ">=" "&&" "||" "!" "&" "|" "^" "~" "<<" ">>" "??" ".." "->"] @operator
["=" "+=" "-=" "*=" "/=" "%="] @operator
["=>" "::"] @punctuation.special
["." "?."] @punctuation.delimiter
["," ":" ";"] @punctuation.delimiter
["(" ")" "[" "]" "{" "}"] @punctuation.bracket
["?" "!"] @punctuation.special

; === Comments ===

(line_comment) @comment
(block_comment) @comment

; === Members ===

(member_expression (identifier) @property)
(struct_literal (identifier) @type (identifier) @property)

; === Module paths ===

(module_path (identifier) @module)

; === Match ===

(match_arm (pattern) @variable)
