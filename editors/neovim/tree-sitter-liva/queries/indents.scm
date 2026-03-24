; Tree-sitter indent queries for Liva

[
  (block)
  (struct_declaration)
  (enum_declaration)
  (class_declaration)
  (impl_declaration)
  (protocol_declaration)
  (match_expression)
  (array_literal)
  (parameter_list)
  (closure_expression)
] @indent

[
  "}"
  "]"
  ")"
] @indent.dedent
