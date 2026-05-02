package com.liva.lang.highlighter

import com.intellij.lexer.Lexer
import com.intellij.openapi.editor.DefaultLanguageHighlighterColors
import com.intellij.openapi.editor.HighlighterColors
import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.openapi.fileTypes.SyntaxHighlighterBase
import com.intellij.psi.tree.IElementType
import com.liva.lang.lexer.LivaLexer
import com.liva.lang.lexer.LivaTokens

class LivaSyntaxHighlighter : SyntaxHighlighterBase() {
    override fun getHighlightingLexer(): Lexer = LivaLexer()

    override fun getTokenHighlights(tokenType: IElementType?): Array<TextAttributesKey> {
        return when (tokenType) {
            LivaTokens.KEYWORD       -> KEYWORD_KEYS
            LivaTokens.PRIMITIVE_TYPE -> TYPE_KEYS
            LivaTokens.IDENTIFIER    -> IDENT_KEYS
            LivaTokens.NUMBER        -> NUMBER_KEYS
            LivaTokens.STRING        -> STRING_KEYS
            LivaTokens.CHAR          -> STRING_KEYS
            LivaTokens.LIFETIME      -> LIFETIME_KEYS
            LivaTokens.LINE_COMMENT  -> LINE_COMMENT_KEYS
            LivaTokens.BLOCK_COMMENT -> BLOCK_COMMENT_KEYS
            LivaTokens.DOC_COMMENT   -> DOC_COMMENT_KEYS
            LivaTokens.OPERATOR      -> OPERATOR_KEYS
            LivaTokens.LBRACE,
            LivaTokens.RBRACE        -> BRACE_KEYS
            LivaTokens.LBRACKET,
            LivaTokens.RBRACKET      -> BRACKET_KEYS
            LivaTokens.LPAREN,
            LivaTokens.RPAREN        -> PAREN_KEYS
            LivaTokens.SEMICOLON     -> SEMI_KEYS
            LivaTokens.COMMA         -> COMMA_KEYS
            LivaTokens.BAD_CHARACTER -> BAD_KEYS
            else                     -> EMPTY_KEYS
        }
    }

    companion object {
        val KEYWORD       = TextAttributesKey.createTextAttributesKey("LIVA_KEYWORD",       DefaultLanguageHighlighterColors.KEYWORD)
        val PRIMITIVE_TYPE = TextAttributesKey.createTextAttributesKey("LIVA_TYPE",          DefaultLanguageHighlighterColors.CLASS_NAME)
        val IDENTIFIER    = TextAttributesKey.createTextAttributesKey("LIVA_IDENTIFIER",    DefaultLanguageHighlighterColors.IDENTIFIER)
        val NUMBER        = TextAttributesKey.createTextAttributesKey("LIVA_NUMBER",        DefaultLanguageHighlighterColors.NUMBER)
        val STRING        = TextAttributesKey.createTextAttributesKey("LIVA_STRING",        DefaultLanguageHighlighterColors.STRING)
        val LIFETIME      = TextAttributesKey.createTextAttributesKey("LIVA_LIFETIME",      DefaultLanguageHighlighterColors.METADATA)
        val LINE_COMMENT  = TextAttributesKey.createTextAttributesKey("LIVA_LINE_COMMENT",  DefaultLanguageHighlighterColors.LINE_COMMENT)
        val BLOCK_COMMENT = TextAttributesKey.createTextAttributesKey("LIVA_BLOCK_COMMENT", DefaultLanguageHighlighterColors.BLOCK_COMMENT)
        val DOC_COMMENT   = TextAttributesKey.createTextAttributesKey("LIVA_DOC_COMMENT",   DefaultLanguageHighlighterColors.DOC_COMMENT)
        val OPERATOR      = TextAttributesKey.createTextAttributesKey("LIVA_OPERATOR",      DefaultLanguageHighlighterColors.OPERATION_SIGN)
        val BRACE         = TextAttributesKey.createTextAttributesKey("LIVA_BRACE",         DefaultLanguageHighlighterColors.BRACES)
        val BRACKET       = TextAttributesKey.createTextAttributesKey("LIVA_BRACKET",       DefaultLanguageHighlighterColors.BRACKETS)
        val PAREN         = TextAttributesKey.createTextAttributesKey("LIVA_PAREN",         DefaultLanguageHighlighterColors.PARENTHESES)
        val SEMICOLON     = TextAttributesKey.createTextAttributesKey("LIVA_SEMICOLON",     DefaultLanguageHighlighterColors.SEMICOLON)
        val COMMA         = TextAttributesKey.createTextAttributesKey("LIVA_COMMA",         DefaultLanguageHighlighterColors.COMMA)
        val BAD_CHARACTER = TextAttributesKey.createTextAttributesKey("LIVA_BAD_CHARACTER", HighlighterColors.BAD_CHARACTER)

        private val KEYWORD_KEYS       = arrayOf(KEYWORD)
        private val TYPE_KEYS          = arrayOf(PRIMITIVE_TYPE)
        private val IDENT_KEYS         = arrayOf(IDENTIFIER)
        private val NUMBER_KEYS        = arrayOf(NUMBER)
        private val STRING_KEYS        = arrayOf(STRING)
        private val LIFETIME_KEYS      = arrayOf(LIFETIME)
        private val LINE_COMMENT_KEYS  = arrayOf(LINE_COMMENT)
        private val BLOCK_COMMENT_KEYS = arrayOf(BLOCK_COMMENT)
        private val DOC_COMMENT_KEYS   = arrayOf(DOC_COMMENT)
        private val OPERATOR_KEYS      = arrayOf(OPERATOR)
        private val BRACE_KEYS         = arrayOf(BRACE)
        private val BRACKET_KEYS       = arrayOf(BRACKET)
        private val PAREN_KEYS         = arrayOf(PAREN)
        private val SEMI_KEYS          = arrayOf(SEMICOLON)
        private val COMMA_KEYS         = arrayOf(COMMA)
        private val BAD_KEYS           = arrayOf(BAD_CHARACTER)
        private val EMPTY_KEYS         = emptyArray<TextAttributesKey>()
    }
}
