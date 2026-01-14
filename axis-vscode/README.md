# AXIS Language Support for VS Code

Minimale VS Code Extension für AXIS-Syntax-Highlighting.

## Features

- ✅ Syntax Highlighting für `.axis` Dateien
- ✅ Keywords: `fn`, `let`, `mut`, `return`, `if`, `else`, `while`, `break`, `continue`
- ✅ Typen: `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `bool`, `ptr`
- ✅ Zahlen: Dezimal, Hex (`0xFF`), Binär (`0b1010`)
- ✅ Kommentare: `//`
- ✅ Operatoren: `+`, `-`, `*`, `/`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `=`, `->`
- ✅ Auto-Closing Brackets: `{}`, `[]`, `()`

## Installation (Lokal)

### Option 1: Extension Host (Development Mode)

1. Öffne VS Code im `axis-vscode` Ordner:
   ```bash
   cd axis-vscode
   code .
   ```

2. Drücke `F5` → startet Extension Development Host

3. Öffne eine `.axis` Datei im neuen Fenster

4. Syntax Highlighting ist aktiv!

### Option 2: Manuell installieren

1. Kopiere den `axis-vscode` Ordner nach:
   - **Windows:** `%USERPROFILE%\.vscode\extensions\`
   - **macOS/Linux:** `~/.vscode/extensions/`

2. Benenne um zu: `axis-language-0.1.0`

3. Starte VS Code neu

4. Öffne eine `.axis` Datei

## Build-Integration (Optional)

Um AXIS-Dateien mit `Ctrl+Shift+B` zu kompilieren:

1. Im **Haupt-Workspace** (nicht in axis-vscode!) erstelle `.vscode/tasks.json`:

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

2. Öffne eine `.axis` Datei

3. Drücke `Ctrl+Shift+B` → wähle "Build AXIS"

## Verwendung

```axis
// test.axis
fn main() -> i32 {
    let x: i32 = 42;
    return x;
}
```

**Kompilieren:**
```bash
python ../compilation_pipeline.py test.axis -o test --elf
chmod +x test
./test
echo $?  # 42
```

## Struktur

```
axis-vscode/
├── package.json                    # Extension Manifest
├── language-configuration.json     # Brackets, Comments
├── syntaxes/
│   └── axis.tmLanguage.json       # TextMate Grammar
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
