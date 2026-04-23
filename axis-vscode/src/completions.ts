import * as vscode from 'vscode';
import { AXIS_SYMBOLS, SymbolDoc } from './symbols';

type CompletionSpec = {
    label: string;
    kind: vscode.CompletionItemKind;
    detail: string;
    doc?: string;
    insertText?: string | vscode.SnippetString;
};

const KEYWORDS: CompletionSpec[] = [
    { label: 'mode', kind: vscode.CompletionItemKind.Keyword, detail: 'mode directive' },
    { label: 'func', kind: vscode.CompletionItemKind.Keyword, detail: 'function declaration' },
    { label: 'return', kind: vscode.CompletionItemKind.Keyword, detail: 'return from function' },
    { label: 'when', kind: vscode.CompletionItemKind.Keyword, detail: 'conditional (if)' },
    { label: 'else', kind: vscode.CompletionItemKind.Keyword, detail: 'else branch' },
    { label: 'while', kind: vscode.CompletionItemKind.Keyword, detail: 'while loop' },
    { label: 'loop', kind: vscode.CompletionItemKind.Keyword, detail: 'infinite loop' },
    { label: 'repeat', kind: vscode.CompletionItemKind.Keyword, detail: 'repeat block' },
    { label: 'for', kind: vscode.CompletionItemKind.Keyword, detail: 'for-in loop' },
    { label: 'in', kind: vscode.CompletionItemKind.Keyword, detail: 'loop iterator clause' },
    { label: 'match', kind: vscode.CompletionItemKind.Keyword, detail: 'pattern match' },
    { label: 'break', kind: vscode.CompletionItemKind.Keyword, detail: 'exit current loop' },
    { label: 'stop', kind: vscode.CompletionItemKind.Keyword, detail: 'exit current loop (alias)' },
    { label: 'continue', kind: vscode.CompletionItemKind.Keyword, detail: 'skip to next iteration' },
    { label: 'skip', kind: vscode.CompletionItemKind.Keyword, detail: 'skip to next iteration (alias)' },
    { label: 'field', kind: vscode.CompletionItemKind.Keyword, detail: 'struct-like type' },
    { label: 'enum', kind: vscode.CompletionItemKind.Keyword, detail: 'enum type' },
    { label: 'const', kind: vscode.CompletionItemKind.Keyword, detail: 'immutable binding' },
    { label: 'update', kind: vscode.CompletionItemKind.Keyword, detail: 'mutable-parameter modifier' },
    { label: 'copy', kind: vscode.CompletionItemKind.Keyword, detail: 'value-copy parameter modifier' },
    { label: 'as', kind: vscode.CompletionItemKind.Keyword, detail: 'type cast' },
    { label: 'and', kind: vscode.CompletionItemKind.Operator, detail: 'logical AND' },
    { label: 'or', kind: vscode.CompletionItemKind.Operator, detail: 'logical OR' },
    { label: 'not', kind: vscode.CompletionItemKind.Operator, detail: 'logical NOT' },
    { label: 'syscall', kind: vscode.CompletionItemKind.Keyword, detail: 'raw system call' },
    { label: 'True', kind: vscode.CompletionItemKind.Value, detail: 'boolean true' },
    { label: 'False', kind: vscode.CompletionItemKind.Value, detail: 'boolean false' },
    { label: 'script', kind: vscode.CompletionItemKind.Value, detail: 'mode: script' },
    { label: 'compile', kind: vscode.CompletionItemKind.Value, detail: 'mode: compile' },
];

const TYPES: CompletionSpec[] = (
    ['i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64', 'bool', 'str', 'ptr', 'void']
).map(t => ({
    label: t,
    kind: vscode.CompletionItemKind.TypeParameter,
    detail: `primitive type: ${t}`,
}));

const SNIPPETS: CompletionSpec[] = [
    {
        label: 'main',
        kind: vscode.CompletionItemKind.Snippet,
        detail: 'func main() -> i32:',
        insertText: new vscode.SnippetString(
            'func main() -> i32:\n\t${0}\n\treturn 0',
        ),
    },
    {
        label: 'whenelse',
        kind: vscode.CompletionItemKind.Snippet,
        detail: 'when / else block',
        insertText: new vscode.SnippetString(
            'when ${1:condition}:\n\t${2}\nelse:\n\t${0}',
        ),
    },
    {
        label: 'forrange',
        kind: vscode.CompletionItemKind.Snippet,
        detail: 'for i in range(a, b)',
        insertText: new vscode.SnippetString(
            'for ${1:i} in range(${2:0}, ${3:10}):\n\t${0}',
        ),
    },
    {
        label: 'matchblock',
        kind: vscode.CompletionItemKind.Snippet,
        detail: 'match with wildcard',
        insertText: new vscode.SnippetString(
            'match ${1:expr}:\n\t${2:value}:\n\t\t${3}\n\t_:\n\t\t${0}',
        ),
    },
];

function toItem(spec: CompletionSpec): vscode.CompletionItem {
    const item = new vscode.CompletionItem(spec.label, spec.kind);
    item.detail = spec.detail;
    if (spec.doc) {
        item.documentation = new vscode.MarkdownString(spec.doc);
    }
    if (spec.insertText !== undefined) {
        item.insertText = spec.insertText;
    }
    return item;
}

function symbolToItem(sym: SymbolDoc): vscode.CompletionItem {
    const kind =
        sym.kind === 'function'
            ? vscode.CompletionItemKind.Function
            : sym.kind === 'type'
              ? vscode.CompletionItemKind.TypeParameter
              : vscode.CompletionItemKind.Keyword;
    const item = new vscode.CompletionItem(sym.name, kind);
    item.detail = sym.signature;
    const md = new vscode.MarkdownString();
    md.appendMarkdown(sym.summary + '\n');
    if (sym.example) {
        md.appendCodeblock(sym.example, 'axis');
    }
    item.documentation = md;
    if (sym.kind === 'function' && sym.snippet) {
        item.insertText = new vscode.SnippetString(sym.snippet);
    }
    return item;
}

export function registerCompletions(context: vscode.ExtensionContext): void {
    const provider: vscode.CompletionItemProvider = {
        provideCompletionItems(
            document: vscode.TextDocument,
            position: vscode.Position,
        ) {
            const linePrefix = document
                .lineAt(position.line)
                .text.slice(0, position.character);

            // Suppress completions inside string literals or after a comment.
            const withoutStrings = linePrefix.replace(/"(?:\\.|[^"\\])*"/g, '');
            if (/(^|[^:])(\/\/|#)/.test(withoutStrings)) {
                return [];
            }
            const quoteCount = (linePrefix.match(/"/g) ?? []).length;
            if (quoteCount % 2 === 1) {
                return [];
            }

            const items: vscode.CompletionItem[] = [];
            for (const spec of KEYWORDS) {
                items.push(toItem(spec));
            }
            for (const spec of TYPES) {
                items.push(toItem(spec));
            }
            for (const spec of SNIPPETS) {
                items.push(toItem(spec));
            }
            for (const sym of AXIS_SYMBOLS) {
                items.push(symbolToItem(sym));
            }
            return items;
        },
    };

    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider('axis', provider),
    );
}
