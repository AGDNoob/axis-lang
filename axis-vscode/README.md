# AXIS Language Support for VS Code

VS Code extension for AXIS syntax highlighting.

## Features

- ✅ Syntax highlighting for `.axis` files
- ✅ Keywords: `func`, `give`, `return`, `when`, `else`, `while`, `loop`, `repeat`, `break`, `stop`, `continue`, `skip`, `match`, `for`, `in`
- ✅ Type keywords: `field`, `enum`
- ✅ Modifiers: `update`, `copy`
- ✅ Logical operators: `and`, `or`, `not`
- ✅ Types: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `bool`, `str`
- ✅ Mode: `mode script`, `mode compile`
- ✅ Copy modes: `copy.runtime`, `copy.compile`
- ✅ I/O: `write`, `writeln`, `read`, `readln`, `readchar`, `read_failed`
- ✅ Built-in functions: `range`
- ✅ Numbers: Decimal, Hex (`0xFF`), Binary (`0b1010`)
- ✅ Comments: `//` and `#`
- ✅ Operators: `+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `=`, `->`, `&`, `|`, `^`, `<<`, `>>`
- ✅ Compound Assignment: `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`
- ✅ Auto-Closing Brackets: `{}`, `[]`, `()`
- ✅ Wildcard Pattern: `_` (for match statements)
- ✅ Auto-Indentation for `for` loops

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

2. Rename to: `axis-language-0.4.0`

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
      "label": "Build AXIS (ELF64)",
      "type": "shell",
      "command": "python",
      "args": [
        "${workspaceFolder}/compilation_pipeline.py",
        "${file}",
        "-o",
        "${fileDirname}/${fileBasenameNoExtension}",
        "--elf"
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
      "label": "Build AXIS (Raw Binary)",
      "type": "shell",
      "command": "python",
      "args": [
        "${workspaceFolder}/compilation_pipeline.py",
        "${file}",
        "-o",
        "${fileDirname}/${fileBasenameNoExtension}.bin"
      ],
      "group": "build",
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
python ../compilation_pipeline.py test.axis -o test --elf
chmod +x test
./test
echo $?  # 42
```

## Structure

```text
axis-vscode/
├── package.json                    # Extension manifest
├── language-configuration.json     # Brackets, comments
├── syntaxes/
│   └── axis.tmLanguage.json       # TextMate grammar
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
