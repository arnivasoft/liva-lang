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
let statusBarItem: vscode.StatusBarItem | undefined;

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

function updateStatusBar(state: "starting" | "running" | "stopped"): void {
    if (!statusBarItem) return;
    switch (state) {
        case "starting":
            statusBarItem.text = "$(loading~spin) Liva LSP";
            statusBarItem.tooltip = "Liva Language Server starting...";
            break;
        case "running":
            statusBarItem.text = "$(check) Liva LSP";
            statusBarItem.tooltip = "Liva Language Server running";
            break;
        case "stopped":
            statusBarItem.text = "$(circle-slash) Liva LSP";
            statusBarItem.tooltip = "Liva Language Server stopped";
            break;
    }
    statusBarItem.show();
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

    updateStatusBar("starting");

    client.start().then(() => {
        updateStatusBar("running");
    }, () => {
        updateStatusBar("stopped");
    });

    context.subscriptions.push({
        dispose: () => {
            if (client) {
                client.stop();
            }
        },
    });
}

// ---------------------------------------------------------------------------
// Debug: find livac executable for DAP
// ---------------------------------------------------------------------------

function findLivac(): string | undefined {
    const config = vscode.workspace.getConfiguration("liva");
    const livacPath = config.get<string>("livacPath", "livac");

    // If absolute path, check it exists
    if (path.isAbsolute(livacPath)) {
        return fs.existsSync(livacPath) ? livacPath : undefined;
    }

    // Search PATH
    const exeName = process.platform === "win32"
        ? (livacPath.endsWith(".exe") ? livacPath : livacPath + ".exe")
        : livacPath;
    const pathDirs = (process.env["PATH"] || "").split(path.delimiter);
    for (const dir of pathDirs) {
        const fullPath = path.join(dir, exeName);
        if (fs.existsSync(fullPath)) {
            return fullPath;
        }
    }

    // Fallback: return as-is (let OS resolve it)
    return livacPath;
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
                program: "${file}",
                stopOnEntry: false,
            },
        ];
    }

    resolveDebugConfiguration(
        folder: vscode.WorkspaceFolder | undefined,
        config: vscode.DebugConfiguration,
        _token?: vscode.CancellationToken
    ): vscode.ProviderResult<vscode.DebugConfiguration> {
        // If launched with F5 without a launch.json, fill in defaults
        if (!config.type && !config.request && !config.name) {
            const editor = vscode.window.activeTextEditor;
            if (editor && editor.document.languageId === "liva") {
                config.type = "liva";
                config.request = "launch";
                config.name = "Debug Liva Program";
                config.program = editor.document.uri.fsPath;
                config.stopOnEntry = false;
            }
        }

        // Default program to active editor
        if (!config.program) {
            const editor = vscode.window.activeTextEditor;
            if (editor && editor.document.languageId === "liva") {
                config.program = editor.document.uri.fsPath;
            }
        }

        if (!config.program) {
            vscode.window.showErrorMessage(
                "Liva Debug: 'program' is not set. Open a .liva file or specify the path in launch.json."
            );
            return undefined;
        }

        return config;
    }

    resolveDebugConfigurationWithSubstitutedVariables(
        _folder: vscode.WorkspaceFolder | undefined,
        config: vscode.DebugConfiguration,
        _token?: vscode.CancellationToken
    ): vscode.ProviderResult<vscode.DebugConfiguration> {

        // Verify the source file exists
        if (!fs.existsSync(config.program)) {
            vscode.window.showErrorMessage(
                `Liva Debug: Source file not found: ${config.program}`
            );
            return undefined;
        }

        return config;
    }
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
        const livacPath = findLivac();
        if (!livacPath) {
            vscode.window.showErrorMessage(
                "Liva Debug: livac not found. Set 'liva.livacPath' in settings."
            );
            return undefined;
        }
        return new vscode.DebugAdapterExecutable(livacPath, ["dap"]);
    }
}

// ---------------------------------------------------------------------------
// Build / Run / Test commands
// ---------------------------------------------------------------------------

function getActiveFilePath(): string | undefined {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== "liva") {
        vscode.window.showWarningMessage("No active .liva file.");
        return undefined;
    }
    return editor.document.uri.fsPath;
}

function getLivacCommand(): string {
    const config = vscode.workspace.getConfiguration("liva");
    return config.get<string>("livacPath", "livac");
}

// ---------------------------------------------------------------------------
// Test Explorer
// ---------------------------------------------------------------------------

function setupTestExplorer(context: vscode.ExtensionContext): void {
    const ctrl = vscode.tests.createTestController("livaTests", "Liva Tests");
    context.subscriptions.push(ctrl);

    // Discover tests in a document
    function discoverTests(doc: vscode.TextDocument): void {
        if (doc.languageId !== "liva") return;

        const uri = doc.uri;
        let fileItem = ctrl.items.get(uri.toString());
        if (!fileItem) {
            fileItem = ctrl.createTestItem(
                uri.toString(),
                path.basename(uri.fsPath),
                uri
            );
            ctrl.items.add(fileItem);
        }
        fileItem.children.replace([]);

        const text = doc.getText();
        const testRegex = /^\s*test\s+"([^"]+)"\s*\{/gm;
        let match: RegExpExecArray | null;
        while ((match = testRegex.exec(text)) !== null) {
            const testName = match[1];
            const line = doc.positionAt(match.index).line;
            const testItem = ctrl.createTestItem(
                `${uri.toString()}::${testName}`,
                testName,
                uri
            );
            testItem.range = new vscode.Range(line, 0, line, match[0].length);
            fileItem.children.add(testItem);
        }

        // Remove file item if no tests found
        if (fileItem.children.size === 0) {
            ctrl.items.delete(uri.toString());
        }
    }

    // Watch for document changes
    const watcher = vscode.workspace.createFileSystemWatcher("**/*.liva");
    watcher.onDidChange(uri => {
        vscode.workspace.openTextDocument(uri).then(doc => discoverTests(doc));
    });
    watcher.onDidCreate(uri => {
        vscode.workspace.openTextDocument(uri).then(doc => discoverTests(doc));
    });
    watcher.onDidDelete(uri => {
        ctrl.items.delete(uri.toString());
    });
    context.subscriptions.push(watcher);

    // Discover in currently open documents
    vscode.workspace.textDocuments.forEach(discoverTests);
    vscode.workspace.onDidOpenTextDocument(discoverTests, undefined, context.subscriptions);

    // Run profile
    ctrl.createRunProfile(
        "Run Tests",
        vscode.TestRunProfileKind.Run,
        async (request, token) => {
            const run = ctrl.createTestRun(request);
            const livac = getLivacCommand();

            const items: vscode.TestItem[] = [];
            if (request.include) {
                request.include.forEach(item => items.push(item));
            } else {
                ctrl.items.forEach(item => items.push(item));
            }

            for (const item of items) {
                if (token.isCancellationRequested) break;

                // If file-level item, run all tests in that file
                const fileUri = item.uri;
                if (!fileUri) continue;

                run.started(item);

                try {
                    const result = await new Promise<{ code: number; output: string }>((resolve) => {
                        const proc = require("child_process").spawn(
                            livac, ["test", fileUri.fsPath],
                            { cwd: path.dirname(fileUri.fsPath) }
                        );
                        let output = "";
                        proc.stdout?.on("data", (data: Buffer) => output += data.toString());
                        proc.stderr?.on("data", (data: Buffer) => output += data.toString());
                        proc.on("close", (code: number) => resolve({ code: code ?? 1, output }));
                        if (token.isCancellationRequested) proc.kill();
                    });

                    if (result.code === 0) {
                        // Mark all children as passed
                        item.children.forEach(child => run.passed(child));
                        if (item.children.size === 0) run.passed(item);
                    } else {
                        const msg = new vscode.TestMessage(result.output);
                        item.children.forEach(child => run.failed(child, msg));
                        if (item.children.size === 0) run.failed(item, msg);
                    }
                } catch (err) {
                    run.failed(item, new vscode.TestMessage(`Error: ${err}`));
                }
            }

            run.end();
        }
    );
}

// ---------------------------------------------------------------------------
// Activation
// ---------------------------------------------------------------------------

export function activate(context: vscode.ExtensionContext): void {
    // Status bar
    statusBarItem = vscode.window.createStatusBarItem(
        vscode.StatusBarAlignment.Left, 100
    );
    statusBarItem.command = "liva.restartServer";
    context.subscriptions.push(statusBarItem);

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

    // Build command
    context.subscriptions.push(
        vscode.commands.registerCommand("liva.build", () => {
            const filePath = getActiveFilePath();
            if (!filePath) return;
            const livac = getLivacCommand();
            const terminal = vscode.window.createTerminal("Liva Build");
            terminal.show();
            terminal.sendText(`${livac} "${filePath}"`);
        })
    );

    // Run command
    context.subscriptions.push(
        vscode.commands.registerCommand("liva.run", () => {
            const filePath = getActiveFilePath();
            if (!filePath) return;
            const livac = getLivacCommand();
            const terminal = vscode.window.createTerminal("Liva Run");
            terminal.show();
            terminal.sendText(`${livac} run "${filePath}"`);
        })
    );

    // Test command
    context.subscriptions.push(
        vscode.commands.registerCommand("liva.test", () => {
            const filePath = getActiveFilePath();
            if (!filePath) return;
            const livac = getLivacCommand();
            const terminal = vscode.window.createTerminal("Liva Test");
            terminal.show();
            terminal.sendText(`${livac} test "${filePath}"`);
        })
    );

    // Test Explorer
    setupTestExplorer(context);

    // Debug
    const configProvider = new LivaDebugConfigurationProvider();
    const adapterFactory = new LivaDebugAdapterDescriptorFactory();

    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider("liva", configProvider),
        vscode.debug.registerDebugAdapterDescriptorFactory("liva", adapterFactory)
    );
}

export function deactivate(): Thenable<void> | undefined {
    if (statusBarItem) {
        statusBarItem.dispose();
        statusBarItem = undefined;
    }
    if (!client) {
        return undefined;
    }
    return client.stop();
}
