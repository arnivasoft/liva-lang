package com.liva.lang.highlighter

import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.openapi.fileTypes.SyntaxHighlighter
import com.intellij.openapi.options.colors.AttributesDescriptor
import com.intellij.openapi.options.colors.ColorDescriptor
import com.intellij.openapi.options.colors.ColorSettingsPage
import com.liva.lang.LivaIcons
import javax.swing.Icon

class LivaColorSettingsPage : ColorSettingsPage {
    override fun getDisplayName() = "Liva"
    override fun getIcon(): Icon = LivaIcons.FILE
    override fun getHighlighter(): SyntaxHighlighter = LivaSyntaxHighlighter()
    override fun getAttributeDescriptors(): Array<AttributesDescriptor> = ATTRIBUTES
    override fun getColorDescriptors(): Array<ColorDescriptor> = ColorDescriptor.EMPTY_ARRAY
    override fun getAdditionalHighlightingTagToDescriptorMap(): Map<String, TextAttributesKey>? = null

    override fun getDemoText(): String = """
        // Greeter struct demonstrating Liva syntax highlighting
        import std::io

        pub struct Greeter<'a> {
            var name: String
            var greeting: String
        }

        impl Greeter {
            pub func new(name: String) -> Greeter {
                return Greeter { name: name, greeting: "Hello" }
            }

            pub func greet(ref self) -> String {
                let count: i32 = 3
                return "\(self.greeting), \(self.name)!"
            }
        }

        /// Entry point.
        func main() {
            let g = Greeter.new("World")
            println(g.greet())
            for i in 0..10 {
                if i % 2 == 0 { println(i) }
            }
        }
    """.trimIndent()

    companion object {
        private val ATTRIBUTES = arrayOf(
            AttributesDescriptor("Keyword",       LivaSyntaxHighlighter.KEYWORD),
            AttributesDescriptor("Primitive type", LivaSyntaxHighlighter.PRIMITIVE_TYPE),
            AttributesDescriptor("Identifier",    LivaSyntaxHighlighter.IDENTIFIER),
            AttributesDescriptor("Number",        LivaSyntaxHighlighter.NUMBER),
            AttributesDescriptor("String",        LivaSyntaxHighlighter.STRING),
            AttributesDescriptor("Lifetime",      LivaSyntaxHighlighter.LIFETIME),
            AttributesDescriptor("Line comment",  LivaSyntaxHighlighter.LINE_COMMENT),
            AttributesDescriptor("Block comment", LivaSyntaxHighlighter.BLOCK_COMMENT),
            AttributesDescriptor("Doc comment",   LivaSyntaxHighlighter.DOC_COMMENT),
            AttributesDescriptor("Operator",      LivaSyntaxHighlighter.OPERATOR),
            AttributesDescriptor("Brace",         LivaSyntaxHighlighter.BRACE),
            AttributesDescriptor("Bracket",       LivaSyntaxHighlighter.BRACKET),
            AttributesDescriptor("Parenthesis",   LivaSyntaxHighlighter.PAREN),
            AttributesDescriptor("Semicolon",     LivaSyntaxHighlighter.SEMICOLON),
            AttributesDescriptor("Comma",         LivaSyntaxHighlighter.COMMA),
            AttributesDescriptor("Bad character", LivaSyntaxHighlighter.BAD_CHARACTER),
        )
    }
}
