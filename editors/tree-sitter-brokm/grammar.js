/**
 * Tree-sitter grammar for brokm (HolyC-flavored).
 *
 * Scope: enough structure to drive syntax highlighting (functions, classes,
 * declarations, control flow, literals). It is deliberately permissive — the
 * goal is robust highlighting, not a verification-grade parse.
 */

const PREC = {
  assign: 1,
  ternary: 2,
  logical_or: 3,
  logical_and: 4,
  bitwise_or: 5,
  bitwise_xor: 6,
  bitwise_and: 7,
  equality: 8,
  relational: 9,
  shift: 10,
  additive: 11,
  multiplicative: 12,
  unary: 13,
  postfix: 14,
  call: 15,
  member: 16,
};

const TYPE_KEYWORDS = [
  'U0', 'U8', 'U16', 'U32', 'U64',
  'I8', 'I16', 'I32', 'I64', 'F64', 'Bool',
];

module.exports = grammar({
  name: 'brokm',

  extras: ($) => [/\s/, $.line_comment, $.block_comment],

  word: ($) => $.identifier,

  conflicts: ($) => [
    [$._type, $._primary_expression],
  ],

  rules: {
    source_file: ($) => repeat($._top_level),

    _top_level: ($) =>
      choice(
        $.include_directive,
        $.class_declaration,
        $.function_definition,
        $._statement,
      ),

    // ---- comments ----------------------------------------------------------
    line_comment: (_) => token(seq('//', /[^\n]*/)),
    block_comment: (_) => token(seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/')),

    // ---- preprocessor ------------------------------------------------------
    include_directive: ($) =>
      seq('#', 'include', field('path', $.string_literal)),

    // ---- types -------------------------------------------------------------
    primitive_type: (_) => choice(...TYPE_KEYWORDS),

    _type: ($) =>
      seq(
        choice($.primitive_type, alias($.identifier, $.type_identifier)),
        optional($.array_marker),
      ),

    array_marker: (_) => seq('[', ']'),

    // ---- classes -----------------------------------------------------------
    class_declaration: ($) =>
      seq(
        choice('class', 'struct'),
        field('name', alias($.identifier, $.type_identifier)),
        field('body', $.class_body),
      ),

    class_body: ($) => seq('{', repeat($._class_member), '}'),

    _class_member: ($) => choice($.method_definition, $.field_declaration),

    field_declaration: ($) =>
      seq(field('type', $._type), field('name', $.identifier), ';'),

    method_definition: ($) =>
      seq(
        field('type', $._type),
        field('name', $.identifier),
        field('parameters', $.parameter_list),
        field('body', $.block),
      ),

    // ---- functions ---------------------------------------------------------
    function_definition: ($) =>
      seq(
        field('type', $._type),
        field('name', $.identifier),
        field('parameters', $.parameter_list),
        choice(field('body', $.block), ';'),
      ),

    parameter_list: ($) =>
      seq('(', optional(seq($.parameter, repeat(seq(',', $.parameter)))), ')'),

    parameter: ($) =>
      seq(field('type', $._type), optional(field('name', $.identifier))),

    // ---- statements --------------------------------------------------------
    _statement: ($) =>
      choice(
        $.variable_declaration,
        $.if_statement,
        $.while_statement,
        $.for_statement,
        $.do_statement,
        $.switch_statement,
        $.return_statement,
        $.break_statement,
        $.continue_statement,
        $.block,
        $.expression_statement,
        $.empty_statement,
      ),

    block: ($) => seq('{', repeat($._statement), '}'),

    variable_declaration: ($) =>
      seq(
        field('type', $._type),
        field('name', $.identifier),
        optional(seq('=', field('value', $._expression))),
        ';',
      ),

    if_statement: ($) =>
      prec.right(
        seq(
          'if', '(', field('condition', $._expression), ')',
          field('consequence', $._statement),
          optional(seq('else', field('alternative', $._statement))),
        ),
      ),

    while_statement: ($) =>
      seq('while', '(', field('condition', $._expression), ')', $._statement),

    for_statement: ($) =>
      seq(
        'for', '(',
        field('initializer', choice($.variable_declaration, seq(optional($._expression), ';'))),
        field('condition', optional($._expression)), ';',
        field('update', optional($._expression)),
        ')',
        $._statement,
      ),

    do_statement: ($) =>
      seq('do', field('body', $._statement),
          'while', '(', field('condition', $._expression), ')', ';'),

    switch_statement: ($) =>
      seq('switch', '(', field('value', $._expression), ')', $.switch_body),

    switch_body: ($) => seq('{', repeat(choice($.case_clause, $.default_clause)), '}'),

    case_clause: ($) =>
      seq('case', field('value', $._expression), ':', repeat($._statement)),

    default_clause: ($) => seq('default', ':', repeat($._statement)),

    return_statement: ($) => seq('return', optional($._expression), ';'),
    break_statement: (_) => seq('break', ';'),
    continue_statement: (_) => seq('continue', ';'),
    empty_statement: (_) => ';',

    // A HolyC bare-string print is just an expression statement whose head is a
    // string literal followed by optional comma-separated arguments.
    expression_statement: ($) =>
      seq($._expression, repeat(seq(',', $._expression)), ';'),

    // ---- expressions -------------------------------------------------------
    _expression: ($) =>
      choice(
        $.assignment_expression,
        $.binary_expression,
        $.unary_expression,
        $.update_expression,
        $.call_expression,
        $.index_expression,
        $.field_expression,
        $._primary_expression,
      ),

    _primary_expression: ($) =>
      choice(
        $.identifier,
        $.this,
        $.number_literal,
        $.string_literal,
        $.char_literal,
        $.boolean_literal,
        $.null_literal,
        $.array_literal,
        $.parenthesized_expression,
      ),

    parenthesized_expression: ($) => seq('(', $._expression, ')'),

    array_literal: ($) =>
      seq('[', optional(seq($._expression, repeat(seq(',', $._expression)), optional(','))), ']'),

    assignment_expression: ($) =>
      prec.right(PREC.assign,
        seq(
          field('left', $._expression),
          field('operator', choice('=', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '<<=', '>>=')),
          field('right', $._expression),
        )),

    unary_expression: ($) =>
      prec.right(PREC.unary,
        seq(field('operator', choice('-', '+', '!', '~')), field('argument', $._expression))),

    update_expression: ($) =>
      choice(
        prec.right(PREC.unary, seq(field('operator', choice('++', '--')), field('argument', $._expression))),
        prec.left(PREC.postfix, seq(field('argument', $._expression), field('operator', choice('++', '--')))),
      ),

    binary_expression: ($) => {
      const table = [
        ['||', PREC.logical_or],
        ['&&', PREC.logical_and],
        ['|', PREC.bitwise_or],
        ['^', PREC.bitwise_xor],
        ['&', PREC.bitwise_and],
        ['==', PREC.equality],
        ['!=', PREC.equality],
        ['<', PREC.relational],
        ['<=', PREC.relational],
        ['>', PREC.relational],
        ['>=', PREC.relational],
        ['<<', PREC.shift],
        ['>>', PREC.shift],
        ['+', PREC.additive],
        ['-', PREC.additive],
        ['*', PREC.multiplicative],
        ['/', PREC.multiplicative],
        ['%', PREC.multiplicative],
      ];
      return choice(...table.map(([op, p]) =>
        prec.left(p, seq(
          field('left', $._expression),
          field('operator', op),
          field('right', $._expression),
        ))));
    },

    call_expression: ($) =>
      prec(PREC.call,
        seq(field('function', $._expression), field('arguments', $.argument_list))),

    argument_list: ($) =>
      seq('(', optional(seq($._expression, repeat(seq(',', $._expression)))), ')'),

    index_expression: ($) =>
      prec(PREC.call, seq(field('object', $._expression), '[', field('index', $._expression), ']')),

    field_expression: ($) =>
      prec(PREC.member,
        seq(field('object', $._expression), '.', field('field', $.identifier))),

    // ---- literals ----------------------------------------------------------
    number_literal: (_) =>
      token(choice(
        /0[xX][0-9a-fA-F]+/,
        /\d+\.\d+([eE][+-]?\d+)?/,
        /\d+[eE][+-]?\d+/,
        /\d+/,
      )),

    // brokm strings may span multiple lines (the lexer permits raw newlines).
    string_literal: ($) =>
      seq('"', repeat(choice($.escape_sequence, token.immediate(/[^"\\]+/))), '"'),

    char_literal: ($) =>
      seq("'", choice($.escape_sequence, token.immediate(/[^'\\\n]/)), "'"),

    escape_sequence: (_) => token.immediate(seq('\\', choice(/[ntr0\\"']/, /x[0-9a-fA-F]+/))),

    boolean_literal: (_) => choice('TRUE', 'FALSE'),
    null_literal: (_) => 'NULL',
    this: (_) => 'this',

    identifier: (_) => /[A-Za-z_][A-Za-z0-9_]*/,
  },
});
