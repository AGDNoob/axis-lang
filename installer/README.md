# AXIS Linux Installer

This directory contains installation scripts for the AXIS programming language on Linux systems.

---

## 📦 Installation

### User Installation (Recommended)

Installs AXIS to your home directory (`~/.local/bin` and `~/.local/lib/axis`):

```bash
cd installer/
chmod +x install.sh
./install.sh --user
```

**No root privileges required.**

### System-Wide Installation

Installs AXIS for all users (`/usr/local/bin` and `/usr/local/lib/axis`):

```bash
cd installer/
chmod +x install.sh
sudo ./install.sh --system
```

**Requires root/sudo privileges.**

---

## ✅ Post-Installation

### Verify Installation

```bash
axis --version
```

Expected output:
```
AXIS Language
Version: 1.0.2-beta
Modes: script (interpreted), compile (native ELF64)
Platform: Linux x86-64 (compile), Any (script)
Python: Python 3.x.x
```

### Configure PATH (if needed)

If you see `command not found`, add this to your `~/.bashrc` or `~/.zshrc`:

```bash
export PATH="$HOME/.local/bin:$PATH"
```

Then reload:
```bash
source ~/.bashrc  # or source ~/.zshrc
```

---

## 🚀 Usage

### Script Mode (Interpreted)

```bash
axis run script.axis
```

### Compile Mode (Native Binary)

```bash
axis build program.axis -o program
chmod +x program
./program
```

This compiles to a temporary file and shows the exit code.

### Show Help

```bash
axis --help
```

---

## 🗑️ Uninstallation

### Linux/macOS

```bash
cd installer/
chmod +x uninstall.sh
./uninstall.sh          # User installation
sudo ./uninstall.sh     # System installation
```

### Windows

```batch
cd installer
uninstall.bat
```

Or simply delete the `axis-lang` folder if you installed via git clone.

---

## 📋 Requirements

- **OS:** Linux, macOS, or Windows (script mode on all; compile mode on Linux x86-64 only)
- **Python:** 3.7 or higher
- **Permissions:** User installation = none, System installation = root/sudo (Linux/macOS)

---

## 🛠️ What Gets Installed

### User Installation (`--user`)

```
~/.local/bin/axis                    # CLI command
~/.local/lib/axis/                   # Compiler files
    ├── tokenization_engine.py
    ├── syntactic_analyzer.py
    ├── semantic_analyzer.py
    ├── code_generator.py
    ├── executable_format_generator.py
    ├── compilation_pipeline.py
    ├── transpiler.py
    └── assembler.py
```

### System Installation (`--system`)

```
/usr/local/bin/axis                  # CLI command
/usr/local/lib/axis/                 # Compiler files
    └── (same as above)
```

---

## 🔧 Troubleshooting

### `axis: command not found`

**Problem:** `~/.local/bin` is not in your `PATH`.

**Solution:**
```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### `Python 3.7+ required`

**Problem:** Python version too old.

**Solution:**
```bash
sudo apt install python3.9  # Ubuntu/Debian
sudo dnf install python39   # Fedora
```

### `Permission denied`

**Problem:** Trying system installation without root.

**Solution:**
```bash
sudo ./install.sh --system
```

### Test Installation Manually

```bash
# Check if binary exists
ls -l ~/.local/bin/axis

# Check if library exists
ls -l ~/.local/lib/axis/

# Test directly
~/.local/bin/axis --version
```

---

## 📝 Installation Script Details

### What `install.sh` Does

1. Checks Python 3.7+ is installed
2. Verifies all compiler files exist
3. Creates installation directories
4. Copies compiler files to library directory
5. Installs `axis` wrapper command
6. Configures library paths
7. Checks PATH configuration

### What `uninstall.sh` Does

1. Detects user and/or system installations
2. Removes `axis` command
3. Removes library directory
4. Cleans up completely

---

## 🧪 Testing the Installer (For Developers)

If you want to test the installer in a VM or container:

```bash
# Create test file
cat > test.axis << 'EOF'
fn main() -> i32 {
    return 42;
}
EOF

# Install AXIS
./install.sh --user

# Verify
axis --version
axis build test.axis -o test
./test
echo $?  # Should output: 42

# Cleanup
./uninstall.sh
```

---

## 📦 Distribution

When distributing AXIS, include:
- This `installer/` directory
- All compiler `.py` files in the root
- Main `README.md` with language documentation

Users only need to:
1. Download/clone repository
2. Run `cd installer && ./install.sh --user`
3. Start coding with `axis`

---

## 🐧 Supported Linux Distributions

Tested on:
- Ubuntu 20.04+
- Debian 10+
- Fedora 33+
- Arch Linux
- openSUSE Leap 15+

**Should work on:** Any Linux x86-64 with Python 3.7+

**Not supported:** 
- macOS (different executable format)
- Windows (different executable format)
- ARM/ARM64 (different architecture)

---

## ⚠️ Important Notes

- **No sudo for user install** – Keeps everything in your home directory
- **Portable** – User installation doesn't affect other users
- **Clean uninstall** – Removes everything completely
- **PATH issues** – Most common problem; see troubleshooting above
- **Python required** – AXIS compiler is written in Python

---

**For more information, see the main [README.md](../README.md) in the repository root.**

**Repository:** https://github.com/AGDNoob/axis-lang
