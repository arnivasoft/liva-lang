package com.liva.lang.editor

import com.intellij.lang.BracePair
import com.intellij.lang.PairedBraceMatcher
import com.intellij.openapi.fileTypes.FileType
import com.intellij.psi.PsiFile
import com.intellij.psi.tree.IElementType
import com.liva.lang.lexer.LivaTokens

class LivaBraceMatcher : PairedBraceMatcher {
    override fun getPairs(): Array<BracePair> = PAIRS
    override fun isPairedBracesAllowedBeforeType(lbraceType: IElementType, contextType: IElementType?) = true
    override fun getCodeConstructStart(file: PsiFile?, openingBraceOffset: Int) = openingBraceOffset

    companion object {
        private val PAIRS = arrayOf(
            BracePair(LivaTokens.LBRACE,   LivaTokens.RBRACE,   true),
            BracePair(LivaTokens.LBRACKET, LivaTokens.RBRACKET, false),
            BracePair(LivaTokens.LPAREN,   LivaTokens.RPAREN,   false),
        )
    }
}
