import * as vscode from "vscode";
import * as fs from "fs";
import * as path from "path";
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;

// ---------------------------------------------------------------------------
// LSP Client
// ---------------------------------------------------------------------------

function createClientOptions(): LanguageClientOptions {
    const config = vscode.workspace.getConfiguration("liva");

    return {
        documentSelector: [{ scheme: "file", language: "liva" }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher("**/*.liva"),
        },
        outputChannelName: "Liva Language Server",
        middleware: {
            provideCompletionItem: (document, position, context, token, next) => {
                return next(document, position, context, token);
            },
        },
        initializationOptions: {
            enableSemanticTokens: config.get<boolean>("enableSemanticTokens", true),
        },
    };
}

function createServerOptions(livacPath: string): ServerOptions {
    return {
        command: livacPath,
        args: ["lsp"],
        transport: TransportKind.stdio,
    };
}

function startClient(context: vscode.ExtensionContext): void {
    const config = vscode.workspace.getConfiguration("liva");
    const livacPath = config.get<string>("livacPath", "livac");

    const serverOptions = createServerOptions(livacPath);
    const clientOptions = createClientOptions();

    client = new LanguageClient(
        "liva",
        "Liva Language Server",
        serverOptions,
        clientOptions
    );

    client.start();

    context.subscriptions.push({
        dispose: () => {
            if (client) {
                client.stop();
            }
        },
    });
}

// ---------------------------------------------------------------------------
// Debug: find lldb-dap executable
// ---------------------------------------------------------------------------

function findLldbDap(): string | undefined {
    // 1. User setting
    const config = vscode.workspace.getConfiguration("liva");
    const configPath = config.get<string>("lldbDapPath", "");
    if (configPath && fs.existsSync(configPath)) {
        return configPath;
    }

    // 2. Environment variable
    const envPath = process.env["LIVA_LLDB_DAP_PATH"];
    if (envPath && fs.existsSync(envPath)) {
        return envPath;
    }

    // 3. Platform-specific well-known locations
    const candidates: string[] = [];
    if (process.platform === "win32") {
        candidates.push(
            "C:\\LLVM\\bin\\lldb-dap.exe",
            "C:\\Program Files\\LLVM\\bin\\lldb-dap.exe"
        );
    } else if (process.platform === "darwin") {
        candidates.push(
            "/opt/homebrew/opt/llvm/bin/lldb-dap",
            "/usr/local/opt/llvm/bin/lldb-dap"
        );
    } else {
        candidates.push(
            "/usr/bin/lldb-dap",
            "/usr/lib/llvm-21/bin/lldb-dap",
            "/usr/lib/llvm-19/bin/lldb-dap",
            "/usr/lib/llvm-18/bin/lldb-dap"
        );
    }

    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) {
            return candidate;
        }
    }

    // 4. Search PATH
    const exeName = process.platform === "win32" ? "lldb-dap.exe" : "lldb-dap";
    const pathDirs = (process.env["PATH"] || "").split(path.delimiter);
    for (const dir of pathDirs) {
        const fullPath = path.join(dir, exeName);
        if (fs.existsSync(fullPath)) {
            return fullPath;
        }
    }

    return undefined;
}

// ---------------------------------------------------------------------------
// Debug: Configuration Provider
// ---------------------------------------------------------------------------

class LivaDebugConfigurationProvider implements vscode.DebugConfigurationProvider {

    provideDebugConfigurations(
        _folder: vscode.WorkspaceFolder | undefined
    ): vscode.ProviderResult<vscode.DebugConfiguration[]> {
        return [
            {
                type: "liva",
                request: "launch",
                name: "Debug Liva Program",
                program: "${workspaceFolder}/${workspaceFolderBasename}.exe",
                args: [],
                cwd: "${workspaceFolder}",
                stopOnEntry: false,
                preBuildTask: true,
            },
        ];
    }

    async resolveDebugConfigurationWithSubstitutedVariables(
        folder: vscode.WorkspaceFolder | undefined,
        config: vscode.DebugConfiguration,
        _token?: vscode.CancellationToken
    ): Promise<vscode.DebugConfiguration | undefined> {

        // If no program specified, try to derive from liva.toml
        if (!config.program && folder) {
            const tomlPath = path.join(folder.uri.fsPath, "liva.toml");
            if (fs.existsSync(tomlPath)) {
                const content = fs.readFileSync(tomlPath, "utf-8");
                const nameMatch = content.match(/^\s*name\s*=\s*"([^"]+)"/m);
                if (nameMatch) {
                    const ext = process.platform === "win32" ? ".exe" : "";
                    config.program = path.join(folder.uri.fsPath, nameMatch[1] + ext);
                }
            }
        }

        if (!config.program) {
            vscode.window.showErrorMessage(
                "Liva Debug: 'program' is not set. Please specify the executable path in launch.json."
            );
            return undefined;
        }

        // Default cwd
        if (!config.cwd && folder) {
            config.cwd = folder.uri.fsPath;
        }

        // Pre-build with livac build -g
        if (config.preBuildTask !== false) {
            const livacPath = vscode.workspace
                .getConfiguration("liva")
                .get<string>("livacPath", "livac");

            const buildCwd = folder ? folder.uri.fsPath : config.cwd || ".";

            const exitCode = await runBuild(livacPath, buildCwd);
            if (exitCode !== 0) {
                vscode.window.showErrorMessage(
                    "Liva Debug: Build failed (livac build -g). Fix errors before debugging."
                );
                return undefined;
            }
        }

        // Verify the executable exists
        if (!fs.existsSync(config.program)) {
            vscode.window.showErrorMessage(
                `Liva Debug: Executable not found: ${config.program}`
            );
            return undefined;
        }

        return config;
    }
}

function runBuild(livacPath: string, cwd: string): Promise<number> {
    return new Promise((resolve) => {
        const task = new vscode.Task(
            { type: "liva", task: "build-debug" },
            vscode.TaskScope.Workspace,
            "Liva Build (Debug)",
            "liva",
            new vscode.ShellExecution(livacPath, ["build", "-g"], { cwd })
        );
        task.presentationOptions = {
            reveal: vscode.TaskRevealKind.Silent,
            panel: vscode.TaskPanelKind.Shared,
        };

        const disposable = vscode.tasks.onDidEndTaskProcess((e) => {
            if (e.execution.task === task) {
                disposable.dispose();
                resolve(e.exitCode ?? 1);
            }
        });

        vscode.tasks.executeTask(task).then(undefined, () => {
            disposable.dispose();
            resolve(1);
        });
    });
}

// ---------------------------------------------------------------------------
// Debug: Adapter Descriptor Factory
// ---------------------------------------------------------------------------

class LivaDebugAdapterDescriptorFactory
    implements vscode.DebugAdapterDescriptorFactory
{
    createDebugAdapterDescriptor(
        _session: vscode.DebugSession,
        _executable: vscode.DebugAdapterExecutable | undefined
    ): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
        const lldbDapPath = findLldbDap();
        if (!lldbDapPath) {
            vscode.window.showErrorMessage(
                "Liva Debug: lldb-dap not found. Install LLVM or set 'liva.lldbDapPath' in settings."
            );
            return undefined;
        }
        return new vscode.DebugAdapterExecutable(lldbDapPath);
    }
}

// ---------------------------------------------------------------------------
// Activation
// ---------------------------------------------------------------------------

export function activate(context: vscode.ExtensionContext): void {
    // LSP
    startClient(context);

    // Re-start the client when settings change
    context.subscriptions.push(
        vscode.workspace.onDidChangeConfiguration((e) => {
            if (
                e.affectsConfiguration("liva.livacPath") ||
                e.affectsConfiguration("liva.enableSemanticTokens")
            ) {
                if (client) {
                    client.stop().then(() => {
                        startClient(context);
                    });
                }
            }
        })
    );

    // Register restart command
    context.subscriptions.push(
        vscode.commands.registerCommand("liva.restartServer", async () => {
            if (client) {
                await client.stop();
            }
            startClient(context);
            vscode.window.showInformationMessage("Liva Language Server restarted.");
        })
    );

    // Debug
    const configProvider = new LivaDebugConfigurationProvider();
    const adapterFactory = new LivaDebugAdapterDescriptorFactory();

    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider("liva", configProvider),
        vscode.debug.registerDebugAdapterDescriptorFactory("liva", adapterFactory)
    );
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
