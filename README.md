# AXIS

A programming language with Python-like syntax that compiles to native x86-64 machine code.

AXIS has two execution modes: **script mode** for quick scripting, and **compile mode** for native binaries. The compiler (AXCC) is written in C, has zero dependencies, and produces standalone Windows PE and Linux ELF64 executables.

## Installation

**Windows** — run the NSIS installer from [Releases](https://github.com/AGDNoob/axis-lang/releases).  
It installs `axis.exe` + `ax` alias and optionally adds AXIS to your PATH.

**Linux / macOS** — one-liner:
```bash
curl -fsSL https://raw.githubusercontent.com/AGDNoob/axis-lang/main/installer/install-linux.sh | bash
```
This clones the repo, compiles AXCC from source (installs `gcc`/`make` if needed), and places the binary in `~/.local/bin/`.

### Building from Source

If you prefer to build manually:

```bash
# Windows (MinGW)
cd axcc
mingw32-make CC=gcc

# Linux / macOS
cd axcc
make CC=gcc
```

This produces `axis.exe` (Windows) or `axis` (Linux).

## Usage

```bash
# Script mode — compile + run immediately
axis hello.axis

# Compile mode — produce a standalone binary
axis hello.axis -o hello            # default format for current OS
axis hello.axis -o hello --pe       # Windows PE
axis hello.axis -o hello --elf      # Linux ELF64

# Check mode — validate without compiling
axis check program.axis             # syntax + semantic errors
axis check program.axis --all       # + dead code + unused variable warnings
```

## Documentation

| Document | Description |
|----------|-------------|
| [Guide](docs/guide/) | Learn AXIS step by step |
| [Examples](code/examples/) | 20 example programs |
| [Technical](docs/technical/) | How AXCC works internally |
| [Benchmarks](docs/Benchmarks.md) | Performance measurements |
| [Release Notes](RELEASE_NOTES_v1.2.1.md) | v1.2.1 changes and design philosophy |
| [Changelog](docs/CHANGELOG.md) | Version history |

## License

MIT — see [LICENSE](LICENSE).
