package com.liva.lang.lexer

import com.intellij.psi.tree.IElementType
import com.liva.lang.LivaLanguage

class LivaTokenType(debugName: String) : IElementType(debugName, LivaLanguage)

object LivaTokens {
    val KEYWORD       = LivaTokenType("KEYWORD")
    val PRIMITIVE_TYPE = LivaTokenType("PRIMITIVE_TYPE")
    val IDENTIFIER    = LivaTokenType("IDENTIFIER")
    val NUMBER        = LivaTokenType("NUMBER")
    val STRING        = LivaTokenType("STRING")
    val CHAR          = LivaTokenType("CHAR")
    val LIFETIME      = LivaTokenType("LIFETIME")
    val LINE_COMMENT  = LivaTokenType("LINE_COMMENT")
    val BLOCK_COMMENT = LivaTokenType("BLOCK_COMMENT")
    val DOC_COMMENT   = LivaTokenType("DOC_COMMENT")
    val OPERATOR      = LivaTokenType("OPERATOR")
    val LBRACE        = LivaTokenType("LBRACE")
    val RBRACE        = LivaTokenType("RBRACE")
    val LBRACKET      = LivaTokenType("LBRACKET")
    val RBRACKET      = LivaTokenType("RBRACKET")
    val LPAREN        = LivaTokenType("LPAREN")
    val RPAREN        = LivaTokenType("RPAREN")
    val SEMICOLON     = LivaTokenType("SEMICOLON")
    val COMMA         = LivaTokenType("COMMA")
    val WHITE_SPACE   = LivaTokenType("WHITE_SPACE")
    val BAD_CHARACTER = LivaTokenType("BAD_CHARACTER")
}
