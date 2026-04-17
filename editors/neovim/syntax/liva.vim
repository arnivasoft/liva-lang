" Vim syntax file
" Language: Liva
" Maintainer: Liva Language Team
" Latest Revision: 2026-02-22

if exists("b:current_syntax")
  finish
endif

let s:cpo_save = &cpo
set cpo&vim

" --- Comments ---
syn keyword livaTodo TODO FIXME XXX NOTE HACK contained
syn match livaLineComment "//.*$" contains=livaTodo
syn region livaBlockComment start="/\*" end="\*/" contains=livaTodo,livaBlockComment

" --- Strings ---
syn region livaTripleString start='"""' end='"""' contains=livaStringEscape,livaStringInterp
syn region livaString start='"' skip='\\"' end='"' contains=livaStringEscape,livaStringInterp
syn match livaStringEscape "\\[nrt\\'\"0]" contained
syn match livaStringEscape "\\u{[0-9a-fA-F]\+}" contained
syn region livaStringInterp start="\\(" end=")" contained contains=TOP

" --- Numbers ---
syn match livaHexNumber "\<0[xX][0-9a-fA-F][0-9a-fA-F_]*\>"
syn match livaBinaryNumber "\<0[bB][01][01_]*\>"
syn match livaOctalNumber "\<0[oO][0-7][0-7_]*\>"
syn match livaFloat "\<[0-9][0-9_]*\.[0-9][0-9_]*\([eE][+-]\?[0-9]\+\)\?\>"
syn match livaInteger "\<[0-9][0-9_]*\>"

" --- Keywords ---
" Control flow
syn keyword livaControlFlow if else while for in match case return break continue guard try test

" Declarations
syn keyword livaDeclaration func let var const struct enum impl protocol import pub type class extern macro extension subscript

" Storage modifiers
syn keyword livaStorageMod override private public open internal fileprivate static final dyn comptime convenience lazy

" Keyword operators
syn keyword livaKeywordOp as is ref mut where

" Property accessors
syn keyword livaAccessor get set willSet didSet

" Async
syn keyword livaAsync async await yield

" Special identifiers
syn keyword livaSpecial self super newValue oldValue
syn keyword livaSpecialFunc init deinit

" Constants
syn keyword livaBoolean true false
syn keyword livaNil nil

" --- Built-in Types ---
syn keyword livaType i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 bool string void
syn keyword livaCollectionType Map Set

" --- Built-in Functions ---
syn match livaBuiltinFunc "\<\(println\|print\|len\|push\|pop\|append\|readLine\|sqrt\|abs\|pow\|sin\|cos\|tan\|log\|ceil\|floor\|round\|min\|max\|randInt\|randFloat\|toString\|parseInt\|parseFloat\|parseInt64\|charToString\)\ze\s*("

" --- Operators ---
syn match livaArrow "->"
syn match livaFatArrow "=>"
syn match livaScope "::"
syn match livaNilCoalesce "??"
syn match livaOptChain "?\."
syn match livaSpread "\.\.\."
syn match livaRange "\.\."

" --- Type names (PascalCase) ---
syn match livaTypeName "\<[A-Z][a-zA-Z0-9_]*\>"

" --- Function definitions ---
syn match livaFuncDef "\<func\>\s\+\zs[a-zA-Z_][a-zA-Z0-9_]*\ze\s*(" contains=NONE
syn match livaAsyncFuncDef "\<async\>\s\+\<func\>\s\+\zs[a-zA-Z_][a-zA-Z0-9_]*\ze\s*(" contains=NONE

" --- Struct/Class/Enum/Protocol definitions ---
syn match livaStructName "\<struct\>\s\+\zs[a-zA-Z_][a-zA-Z0-9_]*"
syn match livaClassName "\<class\>\s\+\zs[a-zA-Z_][a-zA-Z0-9_]*"
syn match livaEnumName "\<enum\>\s\+\zs[a-zA-Z_][a-zA-Z0-9_]*"
syn match livaProtocolName "\<protocol\>\s\+\zs[a-zA-Z_][a-zA-Z0-9_]*"
syn match livaImplName "\<impl\>\s\+\zs[a-zA-Z_][a-zA-Z0-9_]*"

" --- Highlight Links ---
hi def link livaLineComment Comment
hi def link livaBlockComment Comment
hi def link livaTodo Todo

hi def link livaString String
hi def link livaTripleString String
hi def link livaStringEscape SpecialChar
hi def link livaStringInterp Special

hi def link livaHexNumber Number
hi def link livaBinaryNumber Number
hi def link livaOctalNumber Number
hi def link livaFloat Float
hi def link livaInteger Number

hi def link livaControlFlow Conditional
hi def link livaDeclaration Keyword
hi def link livaStorageMod StorageClass
hi def link livaKeywordOp Operator
hi def link livaAsync Keyword
hi def link livaAccessor Keyword
hi def link livaSpecial Identifier
hi def link livaSpecialFunc Function

hi def link livaBoolean Boolean
hi def link livaNil Constant

hi def link livaType Type
hi def link livaCollectionType Type
hi def link livaBuiltinFunc Function

hi def link livaArrow Operator
hi def link livaFatArrow Operator
hi def link livaScope Operator
hi def link livaNilCoalesce Operator
hi def link livaOptChain Operator
hi def link livaSpread Operator
hi def link livaRange Operator

hi def link livaTypeName Type
hi def link livaFuncDef Function
hi def link livaAsyncFuncDef Function
hi def link livaStructName Type
hi def link livaClassName Type
hi def link livaEnumName Type
hi def link livaProtocolName Type
hi def link livaImplName Type

let b:current_syntax = "liva"

let &cpo = s:cpo_save
unlet s:cpo_save
