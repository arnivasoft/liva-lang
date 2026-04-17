// Tree-sitter grammar for Liva programming language
// Usage: tree-sitter generate

module.exports = grammar({
  name: 'liva',

  extras: $ => [/\s/, $.line_comment, $.block_comment],

  word: $ => $.identifier,

  rules: {
    source_file: $ => repeat($._declaration),

    _declaration: $ => choice(
      $.function_declaration,
      $.struct_declaration,
      $.enum_declaration,
      $.class_declaration,
      $.impl_declaration,
      $.protocol_declaration,
      $.import_declaration,
      $.variable_declaration,
      $.type_alias_declaration,
      $.macro_declaration,
      $.test_declaration,
      $.extern_block,
    ),

    // === Declarations ===

    function_declaration: $ => seq(
      optional('pub'),
      optional('async'),
      'func',
      field('name', $.identifier),
      optional($.generic_params),
      $.parameter_list,
      optional(seq('->', $._type)),
      optional($.where_clause),
      $.block,
    ),

    struct_declaration: $ => seq(
      optional('pub'),
      'struct',
      field('name', $.identifier),
      optional($.generic_params),
      optional($.where_clause),
      '{', repeat($.field_declaration), '}',
    ),

    enum_declaration: $ => seq(
      optional('pub'),
      'enum',
      field('name', $.identifier),
      optional($.generic_params),
      '{', repeat($.enum_case), '}',
    ),

    class_declaration: $ => seq(
      optional(choice('pub', 'open', 'public', 'internal', 'fileprivate')),
      optional('final'),
      'class',
      field('name', $.identifier),
      optional($.generic_params),
      optional(seq(':', commaSep1($.identifier))),
      '{', repeat($._class_member), '}',
    ),

    extension_declaration: $ => seq(
      'extension',
      field('type', $.identifier),
      '{', repeat($.function_declaration), '}',
    ),

    impl_declaration: $ => seq(
      'impl',
      optional($.generic_params),
      field('type', $.identifier),
      optional(seq(':', $.identifier)),
      optional($.where_clause),
      '{', repeat($.function_declaration), '}',
    ),

    protocol_declaration: $ => seq(
      optional('pub'),
      'protocol',
      field('name', $.identifier),
      '{', repeat($._protocol_member), '}',
    ),

    import_declaration: $ => seq(
      'import',
      $.module_path,
    ),

    variable_declaration: $ => seq(
      choice('let', 'var', 'const'),
      field('name', $.identifier),
      optional(seq(':', $._type)),
      optional(seq('=', $._expression)),
    ),

    type_alias_declaration: $ => seq(
      optional('pub'),
      'type',
      field('name', $.identifier),
      '=',
      $._type,
    ),

    macro_declaration: $ => seq(
      optional('pub'),
      'macro',
      field('name', $.identifier),
      $.parameter_list,
      $.block,
    ),

    test_declaration: $ => seq(
      'test',
      field('name', $.string_literal),
      $.block,
    ),

    extern_block: $ => seq(
      'extern',
      $.string_literal,
      choice(
        seq('{', repeat($.function_declaration), '}'),
        $.function_declaration,
      ),
    ),

    // === Class members ===

    _class_member: $ => choice(
      $.field_declaration,
      $.function_declaration,
      $.init_declaration,
      $.deinit_declaration,
      $.subscript_declaration,
    ),

    _member_modifiers: $ => repeat1(choice(
      'private', 'public', 'pub', 'internal', 'fileprivate',
      'static', 'final', 'override', 'convenience', 'lazy',
    )),

    init_declaration: $ => seq(
      optional($._member_modifiers),
      'init',
      optional('?'),
      $.parameter_list,
      $.block,
    ),

    deinit_declaration: $ => seq('deinit', $.block),

    subscript_declaration: $ => seq(
      optional($._member_modifiers),
      'subscript',
      optional($.generic_params),
      $.parameter_list,
      optional(seq('->', $._type)),
      $.block,
    ),

    // === Protocol members ===

    _protocol_member: $ => choice(
      $.function_signature,
      $.associated_type,
    ),

    function_signature: $ => seq(
      optional('pub'),
      optional('async'),
      'func',
      field('name', $.identifier),
      $.parameter_list,
      optional(seq('->', $._type)),
    ),

    associated_type: $ => seq('type', $.identifier),

    // === Helpers ===

    field_declaration: $ => seq(
      choice('var', 'let'),
      field('name', $.identifier),
      ':',
      $._type,
    ),

    enum_case: $ => seq(
      'case',
      field('name', $.identifier),
      optional(seq('(', commaSep($._type), ')')),
    ),

    generic_params: $ => seq('<', commaSep1($.generic_param), '>'),

    generic_param: $ => seq(
      $.identifier,
      optional(seq(':', commaSep1($.identifier))),
    ),

    where_clause: $ => seq(
      'where',
      commaSep1(seq($.identifier, ':', commaSep1($.identifier))),
    ),

    parameter_list: $ => seq('(', commaSep($.parameter), ')'),

    parameter: $ => choice(
      'self',
      seq('ref', 'self'),
      seq('mut', 'self'),
      seq('ref', 'mut', 'self'),
      seq(
        field('name', $.identifier),
        ':',
        optional(choice('ref', seq('ref', 'mut'))),
        $._type,
        optional(seq('...'))
      ),
    ),

    module_path: $ => seq($.identifier, repeat(seq('::', $.identifier))),

    // === Statements ===

    _statement: $ => choice(
      $.variable_declaration,
      $.return_statement,
      $.if_statement,
      $.while_statement,
      $.for_statement,
      $.break_statement,
      $.continue_statement,
      $.expression_statement,
      $.block,
    ),

    return_statement: $ => seq('return', optional($._expression)),
    break_statement: $ => 'break',
    continue_statement: $ => 'continue',
    expression_statement: $ => $._expression,

    if_statement: $ => seq(
      'if',
      choice(
        seq('let', $.identifier, '=', $._expression),
        $._expression,
      ),
      $.block,
      optional(seq('else', choice($.if_statement, $.block))),
    ),

    while_statement: $ => seq('while', $._expression, $.block),

    for_statement: $ => seq(
      'for',
      optional('await'),
      choice(
        seq('(', commaSep1($.identifier), ')'),
        $.identifier,
      ),
      'in',
      $._expression,
      $.block,
    ),

    block: $ => seq('{', repeat($._statement), '}'),

    // === Expressions ===

    _expression: $ => choice(
      $.identifier,
      $.integer_literal,
      $.float_literal,
      $.string_literal,
      $.char_literal,
      $.boolean_literal,
      $.nil_literal,
      $.self_expression,
      $.super_expression,
      $.binary_expression,
      $.unary_expression,
      $.call_expression,
      $.member_expression,
      $.index_expression,
      $.array_literal,
      $.struct_literal,
      $.match_expression,
      $.closure_expression,
      $.ternary_expression,
      $.cast_expression,
      $.try_expression,
      $.await_expression,
      $.assignment,
      $.parenthesized_expression,
    ),

    binary_expression: $ => choice(
      ...[['+', 9], ['-', 9], ['*', 10], ['/', 10], ['%', 10],
        ['==', 6], ['!=', 6], ['<', 7], ['<=', 7], ['>', 7], ['>=', 7],
        ['&&', 2], ['||', 1], ['&', 5], ['|', 3], ['^', 4],
        ['<<', 8], ['>>', 8], ['??', 0],
      ].map(([op, prec]) =>
        prec(prec, seq($._expression, op, $._expression))
      ),
    ),

    unary_expression: $ => choice(
      prec(11, seq('-', $._expression)),
      prec(11, seq('!', $._expression)),
      prec(11, seq('~', $._expression)),
      prec(11, seq('ref', optional('mut'), $._expression)),
    ),

    call_expression: $ => prec(12, seq(
      $._expression, '(', commaSep($._expression), ')',
    )),

    member_expression: $ => prec(12, seq(
      $._expression, choice('.', '?.'), $.identifier,
    )),

    index_expression: $ => prec(12, seq(
      $._expression, '[', $._expression, ']',
    )),

    assignment: $ => prec.right(0, seq(
      $._expression,
      choice('=', '+=', '-=', '*=', '/=', '%='),
      $._expression,
    )),

    ternary_expression: $ => prec.right(0, seq(
      $._expression, '?', $._expression, ':', $._expression,
    )),

    cast_expression: $ => prec(11, seq($._expression, 'as', $._type)),

    try_expression: $ => prec(11, seq('try', $._expression)),

    await_expression: $ => prec(11, seq('await', $._expression)),

    match_expression: $ => seq(
      'match', $._expression, '{',
      repeat($.match_arm),
      '}',
    ),

    match_arm: $ => seq(
      $.pattern, optional(seq('where', $._expression)), '=>', $._expression,
    ),

    pattern: $ => choice(
      '_',
      $.identifier,
      $.integer_literal,
      $.string_literal,
      $.boolean_literal,
      seq('.', $.identifier, optional(seq('(', commaSep($.pattern), ')'))),
    ),

    closure_expression: $ => seq(
      '|', commaSep($.parameter), '|',
      optional(seq('->', $._type)),
      $.block,
    ),

    array_literal: $ => seq('[', commaSep($._expression), ']'),

    struct_literal: $ => seq(
      $.identifier, '{',
      commaSep(seq($.identifier, ':', $._expression)),
      '}',
    ),

    parenthesized_expression: $ => seq('(', $._expression, ')'),

    self_expression: $ => 'self',
    super_expression: $ => 'super',

    // === Types ===

    _type: $ => choice(
      $.primitive_type,
      $.named_type,
      $.optional_type,
      $.array_type,
      $.reference_type,
      $.function_type,
      $.dyn_type,
    ),

    primitive_type: $ => choice(
      'i8', 'i16', 'i32', 'i64',
      'u8', 'u16', 'u32', 'u64',
      'f32', 'f64', 'bool', 'string', 'void',
    ),

    named_type: $ => seq($.identifier, optional($.type_arguments)),
    type_arguments: $ => seq('<', commaSep1($._type), '>'),
    optional_type: $ => seq($._type, '?'),
    array_type: $ => seq('[', $._type, ']'),
    reference_type: $ => seq('ref', optional('mut'), $._type),
    function_type: $ => seq('(', commaSep($._type), ')', '->', $._type),
    dyn_type: $ => seq('dyn', $.identifier),

    // === Literals ===

    integer_literal: $ => choice(
      /[0-9][0-9_]*/,
      /0[xX][0-9a-fA-F][0-9a-fA-F_]*/,
      /0[bB][01][01_]*/,
      /0[oO][0-7][0-7_]*/,
    ),

    float_literal: $ => /[0-9][0-9_]*\.[0-9][0-9_]*([eE][+-]?[0-9]+)?/,

    string_literal: $ => choice(
      seq('"""', /[^]*?/, '"""'),
      seq('"', repeat(choice(/[^"\\]+/, /\\./)), '"'),
    ),

    char_literal: $ => seq("'", choice(/[^'\\]/, /\\./), "'"),

    boolean_literal: $ => choice('true', 'false'),
    nil_literal: $ => 'nil',

    // === Comments ===

    line_comment: $ => token(seq('//', /.*/)),
    block_comment: $ => token(seq('/*', /[^]*?/, '*/')),

    // === Identifiers ===

    identifier: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,
  },
});

function commaSep(rule) {
  return optional(commaSep1(rule));
}

function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)), optional(','));
}
