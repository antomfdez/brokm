; Zed highlight queries for brokm.
; Uses the flatter capture set Zed themes (plain @keyword, @type, @function, ...).

; ---- comments ---------------------------------------------------------------
(line_comment) @comment
(block_comment) @comment

; ---- literals ---------------------------------------------------------------
(number_literal) @number
(string_literal) @string
(char_literal) @string
(escape_sequence) @string.escape
(boolean_literal) @constant
(null_literal) @constant
(this) @variable.special

; ---- preprocessor -----------------------------------------------------------
"#" @keyword
"include" @keyword
(include_directive path: (string_literal) @string)

; ---- types ------------------------------------------------------------------
(primitive_type) @type.builtin
(type_identifier) @type

; ---- declarations -----------------------------------------------------------
(function_definition name: (identifier) @function)
(method_definition name: (identifier) @function)
(parameter name: (identifier) @variable)
(field_declaration name: (identifier) @property)
(field_expression field: (identifier) @property)

; ---- calls ------------------------------------------------------------------
(call_expression function: (identifier) @function)
(call_expression function: (field_expression field: (identifier) @function))

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
  "if" "else" "switch" "case" "default"
  "while" "for" "do"
  "return" "break" "continue"
  "class" "struct"
] @keyword

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
