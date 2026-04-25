# AXIS Language Support for VS Code

Syntax highlighting, snippets, IntelliSense and a built-in linter for the
[AXIS programming language](https://github.com/AGDNoob/axis-lang).

> Requires the `axis` compiler on `PATH` (or set `axis.compilerPath`).
> Get it from the [AXIS releases page](https://github.com/AGDNoob/axis-lang/releases).

## Features

### Syntax Highlighting

- **Keywords** — `func`, `return`, `when`, `else`, `while`, `loop`, `repeat`, `break`, `stop`, `continue`, `skip`, `match`, `for`, `in`, `syscall`
- **Labels** — `@label` on loops, plus `stop @label` / `skip @label`
- **Type definitions** — `field`, `enum` with type name + underlying type coloring
- **Modifiers** — `update`, `copy`, `const`
- **Cast operator** — `as` (e.g. `x as i64`)
- **Logical operators** — `and`, `or`, `not`
- **Built-in types** — `i8`–`i64`, `u8`–`u64`, `bool`, `str`, `ptr`, `void`
- **Array types** — `(i32; 5)`, `(Vec2; 3)` etc.
- **Mode declarations** — `mode script`, `mode compile`
- **Copy expressions** — `copy.runtime`, `copy.compile`
- **Built-in functions** — `write`, `writeln`, `input`, `range`, and the `{var}_input_failed()` helper
- **Variable declarations** — `name: type` colored (both built-in and user-defined types)
- **Function definitions** — `func name(params) -> type:` with parameter highlighting
- **Numbers** — Decimal, Hex (`0xFF`), Binary (`0b1010`), `_` separators
- **Strings** — Double-quoted with escape sequences
- **Comments** — `//` and `#` line comments

### Editing Support

- Indentation-based folding (Python-style)
- Auto-indent after colon-terminated lines
- Bracket matching with colorised pairs
- Auto-closing brackets and quotes
- Toggle line comments with `Ctrl+/`

### Snippets

Type a prefix and press `Tab`:

| Prefix | Expands to |
| ------ | ---------- |
| `func` | Function definition |
| `main` | `func main() -> i32:` template |
| `var` | Variable declaration with type |
| `when` / `whene` | Conditional / if-else block |
| `while` / `repeat` / `for` | Loops |
| `match` | Match with arms + wildcard |
| `field` / `enum` | Type definitions |
| `wl` / `wr` | `writeln(...)` / `write(...)` |
| `arr` | Array declaration |
| `compile` / `script` | Full mode template |
| `input` | `input(...)` with cast |
| `lbl` | Labeled loop (`@outer`) |

### Linter

Runs `axis check` in the background and surfaces errors inline, in the
**Problems** panel, and as squiggles. Triggers on open, save, and (debounced)
while typing.

Commands: **AXIS: Check current file**, **AXIS: Clear all diagnostics**.

### IntelliSense

- Completion for keywords, primitive types, built-ins (`write`, `writeln`,
  `input`, `range`, `syscall`) and block snippets.
- Hover documentation with code examples.

## Configuration

| Setting | Default | Purpose |
| ------- | ------- | ------- |
| `axis.compilerPath` | `""` (auto-detect) | Path to `axis` / `axis.exe`. When empty, looks for `<workspace>/axcc/axis` first, then `PATH`. |
| `axis.useWsl` | `false` | On Windows, invoke the compiler through `wsl`. Paths are translated automatically. |
| `axis.enableLinter` | `true` | Turn the linter on/off. |
| `axis.lintOnType` | `true` | Re-lint while typing. Disable to lint only on save. |
| `axis.lintDelay` | `500` | Debounce interval (ms) for lint-on-type. |
| `axis.checkArgs` | `[]` | Extra arguments appended after `check`. |

## License

MIT — see the [AXIS repository](https://github.com/AGDNoob/axis-lang) for source and issues.
# AXIS Language Support for VS Code

VS Code extension for the AXIS programming language (v1.3.0).

## Features

### Syntax Highlighting

- **Keywords** — `func`, `return`, `when`, `else`, `while`, `loop`, `repeat`, `break`, `stop`, `continue`, `skip`, `match`, `for`, `in`, `syscall`
- **Labels** — `@label` on loops, plus `stop @label` / `skip @label`
- **Type definitions** — `field`, `enum` with type name + underlying type coloring
- **Modifiers** — `update`, `copy`, `const`
- **Cast operator** — `as` (e.g. `x as i64`)
- **Logical operators** — `and`, `or`, `not`
- **Built-in types** — `i8`–`i64`, `u8`–`u64`, `bool`, `str`, `ptr`, `void`
- **Array types** — `(i32; 5)`, `(Vec2; 3)` etc.
- **Mode declarations** — `mode script`, `mode compile`
- **Copy expressions** — `copy.runtime`, `copy.compile`
- **Built-in functions** — `write`, `writeln`, `input`, `range`, and the `{var}_input_failed()` helper
- **Variable declarations** — `name: type` colored (both built-in and user-defined types)
- **Function definitions** — `func name(params) -> type:` with parameter highlighting
- **Function parameters** — `param: type`, `update param: type`, `copy param: type`
- **Return type annotations** — `-> i32`, `-> MyType`
- **Enum access** — `Color::Red` and `Color.Red`
- **Member access** — `point.x`, `r.origin.x`
- **Numbers** — Decimal, Hex (`0xFF`), Binary (`0b1010`), `_` separators
- **Strings** — Double-quoted with escape sequences (`\n`, `\t`, `\\`, `\"`, `\0`, `\r`)
- **Comments** — `//` and `#` line comments
- **Operators** — Full set including `->`, `..`, compound assignment (`+=`, `-=`, etc.)
- **Punctuation** — Commas, colons, semicolons, brackets all scoped

### Editing Support

- **Indentation-based folding** — Collapse blocks automatically (like Python)
- **Auto-indent** — After colon-terminated lines (functions, loops, conditionals, fields, enums)
- **Bracket matching** — `()`, `[]` with colorized bracket pairs
- **Auto-closing** — Brackets, quotes, string-aware
- **Smart commenting** — Toggle line comments with `Ctrl+/`

### Snippets

Type a prefix and press `Tab`:

| Prefix | Expands to |
| ------ | ---------- |
| `func` | Function definition |
| `main` | `func main() -> i32:` template |
| `var` | Variable declaration with type |
| `when` | Conditional block |
| `whene` | When/else block |
| `while` | While loop |
| `repeat` | Infinite loop with stop |
| `for` | For-in-range loop |
| `match` | Match with arms + wildcard |
| `field` | Field (struct) definition |
| `enum` | Enum definition |
| `wl` | `writeln(...)` |
| `wr` | `write(...)` |
| `arr` | Array declaration |
| `compile` | Full compile mode template |
| `script` | Full script mode template |
| `return`| `return ...` value |
| `const` | Immutable constant |
| `input` | `input(...)` with cast |
| `lbl`   | Labeled loop (`@outer`) |

### Linter

The extension runs `axis check` in the background and shows any errors it
reports inline, in the **Problems** panel, and as squiggles on the affected
lines. It runs:

- when a `.axis` file is opened,
- after a save,
- and (debounced) while typing, so unsaved edits are checked against the
  in-memory buffer.

Use the command palette entry **AXIS: Check current file** to trigger a run
manually, or **AXIS: Clear all diagnostics** to wipe the markers.

### IntelliSense

- **Completion** for keywords, primitive types, booleans, built-in functions
  (`write`, `writeln`, `input`, `range`, `syscall`), and a handful of block
  snippets (`main`, `whenelse`, `forrange`, `matchblock`).
- **Hover** documentation for keywords, primitive types, and the built-ins
  above, including a short code example for each function.

### Configuration

| Setting | Default | Purpose |
| ------- | ------- | ------- |
| `axis.compilerPath`  | `""` (auto-detect) | Path to `axis` / `axis.exe`. When empty, the extension looks for `<workspace>/axcc/axis` first, then falls back to the `PATH`. |
| `axis.useWsl`        | `false`            | On Windows, invoke the compiler through `wsl` (useful when `axcc` is built for Linux only). Paths are translated automatically. |
| `axis.enableLinter`  | `true`             | Turn the linter on or off without disabling the extension. |
| `axis.lintOnType`    | `true`             | Re-lint while typing. Disable to lint only on save. |
| `axis.lintDelay`     | `500`              | Debounce interval (ms) for lint-on-type. |
| `axis.checkArgs`     | `[]`               | Extra arguments appended after `check`, e.g. `["--all"]`. |

## Installation (Local)

### Option 1: Extension Host (Development Mode)

1. Open VS Code in the `axis-vscode` folder:

   ```bash
   cd axis-vscode
   code .
   ```

2. Press `F5` → starts Extension Development Host

3. Open a `.axis` file in the new window

4. Syntax highlighting is active!

### Option 2: Manual Installation

1. Copy the `axis-vscode` folder to:
   - **Windows:** `%USERPROFILE%\.vscode\extensions\`
   - **macOS/Linux:** `~/.vscode/extensions/`

2. Rename to: `axis-language-0.5.0`

3. Restart VS Code

4. Open a `.axis` file

## Build Integration (Optional)

To compile AXIS files with `Ctrl+Shift+B`:

1. In your **main workspace** (not in axis-vscode!), create `.vscode/tasks.json`:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Build AXIS",
      "type": "shell",
      "command": "axis",
      "args": [
        "build",
        "${file}",
        "-o",
        "${fileDirname}/${fileBasenameNoExtension}"
      ],
      "group": {
        "kind": "build",
        "isDefault": true
      },
      "presentation": {
        "reveal": "always",
        "panel": "shared"
      },
      "problemMatcher": []
    },
    {
      "label": "Run AXIS Script",
      "type": "shell",
      "command": "axis",
      "args": [
        "run",
        "${file}"
      ],
      "group": "test",
      "presentation": {
        "reveal": "always",
        "panel": "shared"
      },
      "problemMatcher": []
    }
  ]
}
```

1. Open a `.axis` file
2. Press `Ctrl+Shift+B` → select "Build AXIS"

## Usage

```axis
// test.axis
func main() i32:
    x: i32 = 42
    return x
```

**Compile:**

```bash
axis build test.axis -o test
./test
echo $?  # 42
```

## Structure

```text
axis-vscode/
├── package.json                    # Extension manifest
├── language-configuration.json     # Brackets, comments, folding, indentation
├── syntaxes/
│   └── axis.tmLanguage.json       # TextMate grammar (full coverage)
├── snippets/
│   └── axis.json                  # Code snippets
└── README.md
```

## Roadmap

- [ ] LSP (Language Server Protocol)
- [ ] IntelliSense / Autocomplete
- [ ] Error Diagnostics
- [ ] Go to Definition
- [ ] Hover Info (Type Display)

## License

MIT
