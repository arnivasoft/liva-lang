import * as vscode from "vscode";
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;

export function activate(context: vscode.ExtensionContext): void {
    const config = vscode.workspace.getConfiguration("liva");
    const livacPath: string = config.get<string>("livacPath", "livac");

    const serverOptions: ServerOptions = {
        command: livacPath,
        args: ["lsp"],
        transport: TransportKind.stdio,
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: "file", language: "liva" }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher("**/*.liva"),
        },
        outputChannelName: "Liva Language Server",
    };

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

    // Re-start the client when the livacPath setting changes
    context.subscriptions.push(
        vscode.workspace.onDidChangeConfiguration((e) => {
            if (e.affectsConfiguration("liva.livacPath")) {
                if (client) {
                    client.stop().then(() => {
                        const updatedConfig =
                            vscode.workspace.getConfiguration("liva");
                        const updatedPath = updatedConfig.get<string>(
                            "livacPath",
                            "livac"
                        );
                        const newServerOptions: ServerOptions = {
                            command: updatedPath,
                            args: ["lsp"],
                            transport: TransportKind.stdio,
                        };
                        client = new LanguageClient(
                            "liva",
                            "Liva Language Server",
                            newServerOptions,
                            clientOptions
                        );
                        client.start();
                    });
                }
            }
        })
    );
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
