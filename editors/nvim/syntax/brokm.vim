" Vim syntax file
" Language:    brokm (HolyC-flavored)
" Maintainer:  brokm project
" Filenames:   *.bk
"
" Drop-in regex syntax for Vim and Neovim. For the Tree-sitter alternative
" (nvim-treesitter), see editors/tree-sitter-brokm/.

if exists("b:current_syntax")
  finish
endif

let s:cpo_save = &cpo
set cpo&vim

syntax case match

" ---- Comments --------------------------------------------------------------
syntax keyword brokmTodo contained TODO FIXME XXX NOTE HACK
syntax region  brokmComment start="//" end="$" contains=brokmTodo,@Spell keepend
syntax region  brokmComment start="/\*" end="\*/" contains=brokmTodo,@Spell

" ---- Preprocessor ----------------------------------------------------------
" #include "path"  — the only directive; the path is highlighted as a string.
syntax match  brokmInclude "^\s*#\s*include\>" nextgroup=brokmIncludePath skipwhite
syntax region brokmIncludePath contained start=+"+ end=+"+

" ---- Types (storage classes) ----------------------------------------------
syntax keyword brokmType U0 U8 U16 U32 U64 I8 I16 I32 I64 F64 Bool

" ---- Keywords --------------------------------------------------------------
syntax keyword brokmConditional if else switch
syntax keyword brokmRepeat      while for do
syntax keyword brokmLabel       case default
syntax keyword brokmStatement   return break continue
syntax keyword brokmStructure   class struct

" ---- Constants -------------------------------------------------------------
syntax keyword brokmBoolean  TRUE FALSE
syntax keyword brokmConstant NULL
syntax keyword brokmThis     this

" ---- Built-in / native functions ------------------------------------------
syntax keyword brokmBuiltin Print PrintErr ReadFile WriteFile
syntax keyword brokmBuiltin Len Append
syntax keyword brokmBuiltin CharAt Chr Substr IndexOf ToInt ToStr
syntax keyword brokmBuiltin Abs Min Max Sqrt Pow Floor Ceil
syntax keyword brokmBuiltin MapNew MapSet MapGet MapHas MapDelete MapLen MapKeys
syntax keyword brokmBuiltin MAlloc Free
syntax keyword brokmBuiltin PeekU8 PeekI64 PeekF64 PeekPtr PokeU8 PokeI64 PokeF64 PokePtr
syntax keyword brokmBuiltin GcCollect GcMinor GcDisable GcEnable
syntax keyword brokmBuiltin Opcode Assemble MakeClass AddMethod

" ---- Numbers ---------------------------------------------------------------
syntax match brokmNumber  "\<\d\+\>"
syntax match brokmHex     "\<0[xX]\x\+\>"
syntax match brokmFloat   "\<\d\+\.\d\+\([eE][+-]\?\d\+\)\?\>"
syntax match brokmFloat   "\<\d\+[eE][+-]\?\d\+\>"

" ---- Strings & characters --------------------------------------------------
syntax match  brokmEscape contained "\\\([ntr0\\\"']\|x\x\+\)"
syntax region brokmString start=+"+ skip=+\\.+ end=+"+ contains=brokmEscape,@Spell
syntax match  brokmCharacter "'\([^'\\]\|\\[ntr0\\'"]\|\\x\x\+\)'"

" ---- Functions & classes ---------------------------------------------------
" A name immediately followed by '(' is a call/definition.
syntax match brokmFunction "\<[A-Za-z_]\w*\>\ze\s*("
" A name following class/struct is a type name.
syntax match brokmClassName "\<[A-Z_]\w*\>" contained
syntax match brokmClassDecl "\<\(class\|struct\)\s\+[A-Za-z_]\w*"
      \ contains=brokmStructure,brokmClassName

" ---- Operators -------------------------------------------------------------
syntax match brokmOperator "[-+*/%=<>!&|^~.]"
syntax match brokmOperator "++\|--\|&&\|||\|<<\|>>"
syntax match brokmOperator "[-+*/%&|^]=\|<<=\|>>=\|==\|!=\|<=\|>="

" ---- Highlight links -------------------------------------------------------
highlight default link brokmComment      Comment
highlight default link brokmTodo         Todo
highlight default link brokmInclude      Include
highlight default link brokmIncludePath  String
highlight default link brokmType         Type
highlight default link brokmConditional  Conditional
highlight default link brokmRepeat       Repeat
highlight default link brokmLabel        Label
highlight default link brokmStatement    Statement
highlight default link brokmStructure    Structure
highlight default link brokmBoolean      Boolean
highlight default link brokmConstant     Constant
highlight default link brokmThis         Identifier
highlight default link brokmBuiltin      Function
highlight default link brokmNumber       Number
highlight default link brokmHex          Number
highlight default link brokmFloat        Float
highlight default link brokmString       String
highlight default link brokmEscape       SpecialChar
highlight default link brokmCharacter    Character
highlight default link brokmFunction     Function
highlight default link brokmClassName    Type
highlight default link brokmOperator     Operator

let b:current_syntax = "brokm"

let &cpo = s:cpo_save
unlet s:cpo_save
