import * as vscode from 'vscode';
import { AXIS_SYMBOLS, KEYWORD_DOCS } from './symbols';

export function registerHover(context: vscode.ExtensionContext): void {
    const provider: vscode.HoverProvider = {
        provideHover(document, position) {
            const range = document.getWordRangeAtPosition(
                position,
                /[A-Za-z_][A-Za-z0-9_]*/,
            );
            if (!range) {
                return undefined;
            }
            const word = document.getText(range);

            const symbol = AXIS_SYMBOLS.find(s => s.name === word);
            if (symbol) {
                const md = new vscode.MarkdownString();
                md.appendCodeblock(symbol.signature, 'axis');
                md.appendMarkdown('\n' + symbol.summary);
                if (symbol.example) {
                    md.appendMarkdown('\n\n**Example:**');
                    md.appendCodeblock(symbol.example, 'axis');
                }
                return new vscode.Hover(md, range);
            }

            const kw = KEYWORD_DOCS[word];
            if (kw) {
                const md = new vscode.MarkdownString();
                md.appendCodeblock(word, 'axis');
                md.appendMarkdown('\n' + kw);
                return new vscode.Hover(md, range);
            }

            return undefined;
        },
    };

    context.subscriptions.push(
        vscode.languages.registerHoverProvider('axis', provider),
    );
}
