import * as vscode from 'vscode';
import { AxisLinter } from './linter';
import { registerCompletions } from './completions';
import { registerHover } from './hover';

let linter: AxisLinter | undefined;

export function activate(context: vscode.ExtensionContext): void {
    const diagnostics = vscode.languages.createDiagnosticCollection('axis');
    linter = new AxisLinter(diagnostics);
    context.subscriptions.push(diagnostics);

    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(doc => linter?.lint(doc)),
        vscode.workspace.onDidSaveTextDocument(doc => linter?.lint(doc)),
        vscode.workspace.onDidCloseTextDocument(doc => diagnostics.delete(doc.uri)),
    );

    let timer: NodeJS.Timeout | undefined;
    context.subscriptions.push(
        vscode.workspace.onDidChangeTextDocument(event => {
            if (event.document.languageId !== 'axis') {
                return;
            }
            const cfg = vscode.workspace.getConfiguration('axis', event.document.uri);
            if (!cfg.get<boolean>('lintOnType', true)) {
                return;
            }
            if (timer) {
                clearTimeout(timer);
            }
            const delay = cfg.get<number>('lintDelay', 500);
            timer = setTimeout(() => linter?.lint(event.document), delay);
        }),
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('axis.check', () => {
            const editor = vscode.window.activeTextEditor;
            if (editor) {
                linter?.lint(editor.document);
            }
        }),
        vscode.commands.registerCommand('axis.clearDiagnostics', () => {
            diagnostics.clear();
        }),
    );

    vscode.workspace.textDocuments.forEach(doc => linter?.lint(doc));

    registerCompletions(context);
    registerHover(context);
}

export function deactivate(): void {
    linter = undefined;
}
