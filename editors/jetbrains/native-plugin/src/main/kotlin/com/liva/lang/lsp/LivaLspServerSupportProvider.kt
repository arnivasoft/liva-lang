package com.liva.lang.lsp

import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.platform.lsp.api.LspServerSupportProvider
import com.intellij.platform.lsp.api.ProjectWideLspServerDescriptor

/**
 * Hooks the `livac lsp` process into the IntelliJ LSP framework.
 *
 * Requires IntelliJ Platform 2024.2 or newer. Older builds should use the
 * marketplace LSP4IJ plugin (see editors/jetbrains/README.md).
 */
class LivaLspServerSupportProvider : LspServerSupportProvider {
    override fun fileOpened(
        project: Project,
        file: VirtualFile,
        serverStarter: LspServerSupportProvider.LspServerStarter,
    ) {
        if (file.extension == "liva") {
            serverStarter.ensureServerStarted(LivaLspServerDescriptor(project))
        }
    }
}

class LivaLspServerDescriptor(project: Project) : ProjectWideLspServerDescriptor(project, "Liva") {
    override fun isSupportedFile(file: VirtualFile) = file.extension == "liva"

    override fun createCommandLine() = com.intellij.execution.configurations.GeneralCommandLine().apply {
        // Resolve the livac binary from PATH; users override via the IDE's
        // run configuration environment if needed.
        exePath = "livac"
        addParameter("lsp")
        withWorkDirectory(project.basePath)
    }
}
