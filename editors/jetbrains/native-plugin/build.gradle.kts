// Liva Language — JetBrains IDE plugin
//
// Build: ./gradlew buildPlugin   → ./build/distributions/liva-lang-*.zip
// Run sandbox: ./gradlew runIde  → opens a sandbox IDE with the plugin loaded
//
// Targets IntelliJ Platform 2024.2+ for the native LspServerSupportProvider
// API; older builds should keep using the LSP4IJ marketplace plugin.

plugins {
    id("java")
    id("org.jetbrains.kotlin.jvm") version "1.9.25"
    id("org.jetbrains.intellij.platform") version "2.1.0"
}

group = "com.liva.lang"
version = "0.1.0"

repositories {
    mavenCentral()
    intellijPlatform {
        defaultRepositories()
    }
}

dependencies {
    intellijPlatform {
        intellijIdeaCommunity("2024.2.4")
        bundledPlugin("com.intellij.platform.lsp")
    }
}

intellijPlatform {
    pluginConfiguration {
        ideaVersion {
            sinceBuild = "242"
            untilBuild = "243.*"
        }
        changeNotes = """
            <h3>0.1.0</h3>
            <ul>
                <li>Initial native plugin: file type, lexer-based syntax
                    highlighting, line/block commenter, brace matcher,
                    LSP integration via livac lsp.</li>
            </ul>
        """.trimIndent()
    }
}

kotlin {
    jvmToolchain(17)
}

tasks {
    wrapper { gradleVersion = "8.10" }
}
