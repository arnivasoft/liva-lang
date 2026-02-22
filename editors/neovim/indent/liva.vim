" Vim indent file
" Language: Liva

if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

setlocal indentexpr=GetLivaIndent()
setlocal indentkeys=0{,0},0),!^F,o,O,e

if exists("*GetLivaIndent")
  finish
endif

function! GetLivaIndent()
  let lnum = prevnonblank(v:lnum - 1)
  if lnum == 0
    return 0
  endif

  let prev = getline(lnum)
  let curr = getline(v:lnum)
  let ind = indent(lnum)

  " Increase indent after lines ending with { or (
  if prev =~ '[{(]\s*$'
    let ind += shiftwidth()
  endif

  " Decrease indent for lines starting with } or )
  if curr =~ '^\s*[})]\s*$'
    let ind -= shiftwidth()
  endif

  " Handle case/match arms with =>
  if prev =~ '=>\s*$'
    let ind += shiftwidth()
  endif

  return ind
endfunction
