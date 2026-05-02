package com.liva.lang

import com.intellij.openapi.fileTypes.LanguageFileType
import javax.swing.Icon

object LivaFileType : LanguageFileType(LivaLanguage) {
    override fun getName() = "Liva"
    override fun getDescription() = "Liva source file"
    override fun getDefaultExtension() = "liva"
    override fun getIcon(): Icon? = LivaIcons.FILE
}
