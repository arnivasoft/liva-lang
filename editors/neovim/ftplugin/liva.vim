" Vim ftplugin file
" Language: Liva

if exists("b:did_ftplugin")
  finish
endif
let b:did_ftplugin = 1

setlocal shiftwidth=4
setlocal tabstop=4
setlocal softtabstop=4
setlocal expandtab

setlocal commentstring=//\ %s
setlocal comments=s1:/*,mb:*,ex:*/,://

setlocal suffixesadd=.liva
