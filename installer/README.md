# AXIS Installer

One-click GUI installers for the AXIS programming language.

---

## 📦 Download & Install

Choose the installer for your operating system:

### Windows

1. Download [install-windows.ps1](https://github.com/AGDNoob/axis-lang/raw/main/installer/install-windows.ps1)
2. Right-click → **Run with PowerShell**
3. If prompted about execution policy, click "Yes" or "Run Anyway"

**Alternative:** Open PowerShell and run:
```powershell
irm https://raw.githubusercontent.com/AGDNoob/axis-lang/main/installer/install-windows.ps1 | iex
```

### Linux

1. Download [install-linux.sh](https://github.com/AGDNoob/axis-lang/raw/main/installer/install-linux.sh)
2. Make it executable: `chmod +x install-linux.sh`
3. Run it: `./install-linux.sh`

**One-liner:**
```bash
curl -fsSL https://raw.githubusercontent.com/AGDNoob/axis-lang/main/installer/install-linux.sh | bash
```

### macOS

1. Download [install-macos.sh](https://github.com/AGDNoob/axis-lang/raw/main/installer/install-macos.sh)
2. Make it executable: `chmod +x install-macos.sh`
3. Run it: `./install-macos.sh`

**One-liner:**
```bash
curl -fsSL https://raw.githubusercontent.com/AGDNoob/axis-lang/main/installer/install-macos.sh | bash
```

---

## 🎯 What Each Installer Does

1. **Checks for Python 3.7+** - Installs if not found
2. **Downloads AXIS files** - From the GitHub repository
3. **Creates the `axis` command** - Ready to use in terminal
4. **Adds to PATH** - Works immediately after restart
5. **VS Code Extension (optional)** - Syntax highlighting

---

## ✅ Verify Installation

After installation, restart your terminal and run:

```bash
axis version
```

Expected output:
```
AXIS v1.0.2-beta
```

---

## 🚀 Usage

### Run a Script
```bash
axis run script.axis
```

### Check Syntax
```bash
axis check script.axis
```

### Build to Binary (Linux only)
```bash
axis build program.axis -o program --elf
```

### Show Help
```bash
axis help
```

### Show Installation Info
```bash
axis info
```

---

## 🗑️ Uninstall

Each installer includes a built-in uninstaller. Simply run the same installer again:

### Windows
Run `install-windows.ps1` again → Click the **Uninstall** button

### Linux
Run `./install-linux.sh` again → Select **Uninstall** from the menu

### macOS
Run `./install-macos.sh` again → Click **Uninstall** in the dialog

The uninstaller will:
- Remove all AXIS files
- Remove the `axis` command
- Clean up PATH entries
- Uninstall the VS Code extension

---

## 📋 System Requirements

| OS | Requirements |
|---|---|
| Windows | Windows 10/11, PowerShell 5.1+ |
| Linux | Any distro with GUI (zenity/kdialog), Python 3.7+ or package manager |
| macOS | macOS 10.15+, Homebrew (auto-installed if needed) |

---

## 🆘 Troubleshooting

### "command not found: axis"

Restart your terminal, or add manually:

**Windows:** Add `%LOCALAPPDATA%\AXIS\bin` to your PATH

**Linux/macOS:** Add this to `~/.bashrc` or `~/.zshrc`:
```bash
export PATH="$HOME/.local/bin:$PATH"
```

### Windows: "Script cannot be executed"

Run this in PowerShell as Administrator:
```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

### Linux: "zenity not found"

The installer will try to install it automatically. If that fails:
```bash
# Ubuntu/Debian
sudo apt-get install zenity

# Fedora
sudo dnf install zenity

# Arch
sudo pacman -S zenity
```

---

## 📄 License

MIT License - See [LICENSE](../LICENSE)
