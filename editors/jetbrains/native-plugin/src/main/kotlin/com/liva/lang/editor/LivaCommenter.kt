package com.liva.lang.editor

import com.intellij.lang.Commenter

class LivaCommenter : Commenter {
    override fun getLineCommentPrefix() = "// "
    override fun getBlockCommentPrefix() = "/*"
    override fun getBlockCommentSuffix() = "*/"
    override fun getCommentedBlockCommentPrefix(): String? = null
    override fun getCommentedBlockCommentSuffix(): String? = null
}
