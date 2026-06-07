; Tree-sitter highlight queries for brokm.
; Shared by nvim-treesitter and Zed (both ignore captures they don't theme).

; ---- comments ---------------------------------------------------------------
(line_comment) @comment
(block_comment) @comment

; ---- literals ---------------------------------------------------------------
(number_literal) @number
(string_literal) @string
(char_literal) @character
(escape_sequence) @string.escape
(boolean_literal) @boolean
(null_literal) @constant.builtin
(this) @variable.builtin

; ---- preprocessor -----------------------------------------------------------
"#" @keyword.directive
"include" @keyword.directive
(include_directive path: (string_literal) @string.special.path)

; ---- types ------------------------------------------------------------------
(primitive_type) @type.builtin
(type_identifier) @type

; ---- declarations -----------------------------------------------------------
(function_definition name: (identifier) @function)
(method_definition name: (identifier) @function)
(parameter name: (identifier) @variable.parameter)
(field_declaration name: (identifier) @property)
(field_expression field: (identifier) @property)

; ---- calls ------------------------------------------------------------------
(call_expression
  function: (identifier) @function.call)
(call_expression
  function: (field_expression field: (identifier) @function.method.call))

; ---- built-in / native functions -------------------------------------------
((identifier) @function.builtin
  (#any-of? @function.builtin
    "Print" "PrintErr" "ReadFile" "WriteFile"
    "Len" "Append"
    "CharAt" "Chr" "Substr" "IndexOf" "ToInt" "ToStr"
    "Abs" "Min" "Max" "Sqrt" "Pow" "Floor" "Ceil"
    "MapNew" "MapSet" "MapGet" "MapHas" "MapDelete" "MapLen" "MapKeys"
    "MAlloc" "Free"
    "PeekU8" "PeekI64" "PeekF64" "PeekPtr"
    "PokeU8" "PokeI64" "PokeF64" "PokePtr"
    "GcCollect" "GcMinor" "GcDisable" "GcEnable"
    "Opcode" "Assemble" "MakeClass" "AddMethod"))

; ---- identifiers ------------------------------------------------------------
(identifier) @variable

; ---- keywords ---------------------------------------------------------------
[
  "if"
  "else"
  "switch"
  "case"
  "default"
] @keyword.conditional

[
  "while"
  "for"
  "do"
] @keyword.repeat

[
  "return"
  "break"
  "continue"
] @keyword.return

[
  "class"
  "struct"
] @keyword.type

; ---- operators --------------------------------------------------------------
[
  "=" "+" "-" "*" "/" "%"
  "==" "!=" "<" "<=" ">" ">="
  "&&" "||" "!"
  "&" "|" "^" "~" "<<" ">>"
  "++" "--"
  "+=" "-=" "*=" "/=" "%=" "&=" "|=" "^=" "<<=" ">>="
] @operator

; ---- punctuation ------------------------------------------------------------
[ "(" ")" "[" "]" "{" "}" ] @punctuation.bracket
[ "," ";" ":" "." ] @punctuation.delimiter
