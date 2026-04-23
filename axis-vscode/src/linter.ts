import * as vscode from 'vscode';
import * as cp from 'child_process';
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';

// Strip ANSI colour codes from compiler output.
const ANSI_RE = /\x1b\[[0-9;]*m/g;

interface CompilerInvocation {
    command: string;
    args: string[];
    cwd: string | undefined;
}

export class AxisLinter {
    private running = new Map<string, cp.ChildProcess>();

    constructor(private readonly collection: vscode.DiagnosticCollection) {}

    public lint(doc: vscode.TextDocument): void {
        if (doc.languageId !== 'axis') {
            return;
        }
        if (doc.uri.scheme !== 'file') {
            return;
        }

        const cfg = vscode.workspace.getConfiguration('axis', doc.uri);
        if (!cfg.get<boolean>('enableLinter', true)) {
            this.collection.delete(doc.uri);
            return;
        }

        // Write unsaved content to a temp file so we always lint the buffer,
        // not the last-saved version.
        let target = doc.fileName;
        let tempFile: string | undefined;
        if (doc.isDirty || doc.isUntitled) {
            tempFile = path.join(
                os.tmpdir(),
                `axis_lint_${process.pid}_${Date.now()}.axis`,
            );
            try {
                fs.writeFileSync(tempFile, doc.getText());
                target = tempFile;
            } catch {
                tempFile = undefined;
                target = doc.fileName;
            }
        }

        const invocation = this.buildInvocation(doc, target);

        const key = doc.uri.toString();
        const prev = this.running.get(key);
        if (prev && !prev.killed) {
            try {
                prev.kill();
            } catch {
                // ignore
            }
        }

        let child: cp.ChildProcess;
        try {
            child = cp.spawn(invocation.command, invocation.args, {
                cwd: invocation.cwd,
                windowsHide: true,
            });
        } catch (err) {
            this.cleanupTemp(tempFile);
            this.reportLauncherFailure(doc, err);
            return;
        }
        this.running.set(key, child);

        let output = '';
        child.stdout?.on('data', (chunk: Buffer) => {
            output += chunk.toString();
        });
        child.stderr?.on('data', (chunk: Buffer) => {
            output += chunk.toString();
        });
        child.on('error', err => {
            this.running.delete(key);
            this.cleanupTemp(tempFile);
            this.reportLauncherFailure(doc, err);
        });
        child.on('close', () => {
            this.running.delete(key);
            this.cleanupTemp(tempFile);
            const clean = output.replace(ANSI_RE, '');
            const diags = this.parse(clean, target, doc);
            this.collection.set(doc.uri, diags);
        });
    }

    private buildInvocation(
        doc: vscode.TextDocument,
        target: string,
    ): CompilerInvocation {
        const cfg = vscode.workspace.getConfiguration('axis', doc.uri);
        const configuredPath = (cfg.get<string>('compilerPath') ?? '').trim();
        const useWsl = cfg.get<boolean>('useWsl', false);
        const extraArgs = cfg.get<string[]>('checkArgs', []) ?? [];
        const compiler = configuredPath || this.discoverCompiler(doc);
        const workspaceFolder = vscode.workspace.getWorkspaceFolder(doc.uri);
        const cwd = workspaceFolder?.uri.fsPath;

        if (useWsl) {
            const wslTarget = toWslPath(target);
            const wslCompiler = toWslPath(compiler);
            return {
                command: 'wsl',
                args: [wslCompiler, 'check', ...extraArgs, wslTarget],
                cwd,
            };
        }

        return {
            command: compiler,
            args: ['check', ...extraArgs, target],
            cwd,
        };
    }

    private discoverCompiler(doc: vscode.TextDocument): string {
        const exe = process.platform === 'win32' ? 'axis.exe' : 'axis';
        const folder = vscode.workspace.getWorkspaceFolder(doc.uri);
        if (folder) {
            const candidates = [
                path.join(folder.uri.fsPath, 'axcc', exe),
                path.join(folder.uri.fsPath, exe),
            ];
            for (const c of candidates) {
                if (fs.existsSync(c)) {
                    return c;
                }
            }
        }
        return exe;
    }

    private cleanupTemp(tempFile: string | undefined): void {
        if (!tempFile) {
            return;
        }
        try {
            fs.unlinkSync(tempFile);
        } catch {
            // ignore
        }
    }

    private reportLauncherFailure(
        doc: vscode.TextDocument,
        err: unknown,
    ): void {
        const msg = err instanceof Error ? err.message : String(err);
        const range = new vscode.Range(0, 0, 0, 0);
        const diag = new vscode.Diagnostic(
            range,
            `axcc could not be launched: ${msg}. Set "axis.compilerPath" in settings.`,
            vscode.DiagnosticSeverity.Warning,
        );
        diag.source = 'axcc';
        this.collection.set(doc.uri, [diag]);
    }

    /**
     * Parse axcc's human-readable diagnostic output.
     *
     * Expected blocks look like:
     *
     *   File "<path>", line N
     *       <source line>
     *       <spaces>^
     *     Error: <message>
     *
     * Multiple blocks are separated by blank lines. A trailing summary line
     * ("N errors found in <path>") is ignored.
     */
    private parse(
        text: string,
        target: string,
        doc: vscode.TextDocument,
    ): vscode.Diagnostic[] {
        const diags: vscode.Diagnostic[] = [];
        const lines = text.split(/\r?\n/);
        const normalisedTarget = path.resolve(target);

        for (let i = 0; i < lines.length; i++) {
            const headerMatch = /^\s*File\s+"(.+?)",\s+line\s+(\d+)\s*$/.exec(
                lines[i],
            );
            if (!headerMatch) {
                continue;
            }
            const rawFile = headerMatch[1];
            const reportedFile = path.resolve(fromWslPath(rawFile));
            if (reportedFile !== normalisedTarget) {
                continue;
            }

            const line = parseInt(headerMatch[2], 10);
            const lookahead = Math.min(i + 8, lines.length);

            let col = 1;
            for (let j = i + 1; j < lookahead; j++) {
                const caretIdx = lines[j].indexOf('^');
                if (caretIdx >= 0 && lines[j].trimStart().startsWith('^')) {
                    // Source lines are printed with a 4-space indent; the
                    // caret line uses the same indent. Column is 1-based.
                    col = Math.max(1, caretIdx - 3);
                    break;
                }
            }

            let severity = vscode.DiagnosticSeverity.Error;
            let message = '';
            for (let j = i + 1; j < lookahead; j++) {
                const sevMatch = /^\s*(Error|Warning|Note|Hint):\s*(.+)$/.exec(
                    lines[j],
                );
                if (sevMatch) {
                    const label = sevMatch[1].toLowerCase();
                    severity =
                        label === 'warning'
                            ? vscode.DiagnosticSeverity.Warning
                            : label === 'note' || label === 'hint'
                              ? vscode.DiagnosticSeverity.Information
                              : vscode.DiagnosticSeverity.Error;
                    message = sevMatch[2].trim();
                    break;
                }
            }
            if (!message) {
                continue;
            }

            diags.push(this.makeDiagnostic(doc, line, col, severity, message));
        }

        return diags;
    }

    private makeDiagnostic(
        doc: vscode.TextDocument,
        line: number,
        col: number,
        severity: vscode.DiagnosticSeverity,
        message: string,
    ): vscode.Diagnostic {
        const lineIdx = Math.max(0, Math.min(line - 1, doc.lineCount - 1));
        const lineText = doc.lineAt(lineIdx).text;
        const startCol = Math.max(0, Math.min(col - 1, lineText.length));
        let endCol = startCol;
        while (endCol < lineText.length && /[A-Za-z0-9_]/.test(lineText[endCol])) {
            endCol++;
        }
        if (endCol === startCol) {
            endCol = Math.min(startCol + 1, Math.max(lineText.length, startCol + 1));
        }
        const range = new vscode.Range(lineIdx, startCol, lineIdx, endCol);
        const diag = new vscode.Diagnostic(range, message, severity);
        diag.source = 'axcc';
        return diag;
    }
}

function toWslPath(p: string): string {
    if (!p) {
        return p;
    }
    if (process.platform !== 'win32') {
        return p;
    }
    const driveMatch = /^([A-Za-z]):[\\/](.*)$/.exec(p);
    if (driveMatch) {
        const drive = driveMatch[1].toLowerCase();
        const rest = driveMatch[2].replace(/\\/g, '/');
        return `/mnt/${drive}/${rest}`;
    }
    return p.replace(/\\/g, '/');
}

function fromWslPath(p: string): string {
    if (!p) {
        return p;
    }
    if (process.platform !== 'win32') {
        return p;
    }
    const match = /^\/mnt\/([a-zA-Z])\/(.*)$/.exec(p);
    if (match) {
        return `${match[1].toUpperCase()}:\\${match[2].replace(/\//g, '\\')}`;
    }
    return p;
}
