package com.liva.lang

import com.intellij.lang.Language

object LivaLanguage : Language("Liva") {
    private fun readResolve(): Any = LivaLanguage
}
