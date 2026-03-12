#!/usr/bin/env bash
# ============================================================
# AXIS Language — Linux/macOS Installer
# ============================================================
set -euo pipefail

VERSION="1.1.0"
REPO="AGDNoob/axis-lang"
INSTALL_DIR="${AXIS_INSTALL_DIR:-$HOME/.local/bin}"

# ── Colors ───────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
RESET='\033[0m'

# ── ASCII Art ────────────────────────────────────────────────
show_banner() {
    echo ""
    echo -e "${CYAN}"
    echo "     █████  ██   ██ ██ ███████"
    echo "    ██   ██  ██ ██  ██ ██     "
    echo "    ███████   ███   ██ ███████"
    echo "    ██   ██  ██ ██  ██      ██"
    echo "    ██   ██ ██   ██ ██ ███████"
    echo ""
    echo -e "${RESET}"
    echo -e "  ${BOLD}AXIS Language v${VERSION} Installer${RESET}"
    echo ""
}

# ── Helpers ──────────────────────────────────────────────────
info()    { echo -e "${CYAN}[INFO]${RESET}  $*"; }
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
fail()    { echo -e "${RED}[ERROR]${RESET} $*"; exit 1; }

check_cmd() {
    command -v "$1" >/dev/null 2>&1
}

# ── Detect platform ─────────────────────────────────────────
detect_platform() {
    local os arch
    os="$(uname -s)"
    arch="$(uname -m)"

    case "$os" in
        Linux*)  OS="linux" ;;
        Darwin*) OS="macos" ;;
        *)       fail "Unsupported OS: $os" ;;
    esac

    case "$arch" in
        x86_64|amd64) ARCH="x86_64" ;;
        aarch64|arm64) ARCH="aarch64" ;;
        *)             fail "Unsupported architecture: $arch" ;;
    esac

    info "Detected: ${OS} ${ARCH}"
}

# ── Install build tools ──────────────────────────────────────
install_build_tools() {
    echo ""
    warn "gcc, make, or git not found — these are required to compile AXIS."
    echo ""

    # Detect package manager
    local pkg_mgr=""
    local install_cmd=""
    if check_cmd apt-get; then
        pkg_mgr="apt"
        install_cmd="sudo apt-get update && sudo apt-get install -y build-essential git"
    elif check_cmd dnf; then
        pkg_mgr="dnf"
        install_cmd="sudo dnf install -y gcc make git"
    elif check_cmd yum; then
        pkg_mgr="yum"
        install_cmd="sudo yum install -y gcc make git"
    elif check_cmd pacman; then
        pkg_mgr="pacman"
        install_cmd="sudo pacman -S --noconfirm base-devel git"
    elif check_cmd brew; then
        pkg_mgr="brew"
        install_cmd="brew install gcc make git"
    elif check_cmd zypper; then
        pkg_mgr="zypper"
        install_cmd="sudo zypper install -y gcc make git"
    fi

    if [ -z "$pkg_mgr" ]; then
        fail "No supported package manager found. Please install gcc, make, and git manually."
    fi

    info "Detected package manager: ${pkg_mgr}"
    echo -e "  Will run: ${BOLD}${install_cmd}${RESET}"
    echo ""
    read -rp "  Install now? [Y/n] " answer
    answer="${answer:-Y}"

    if [[ "$answer" =~ ^[Yy]$ ]]; then
        info "Installing build tools..."
        eval "$install_cmd"
        if ! check_cmd gcc && ! check_cmd cc; then
            fail "Installation failed — gcc still not found."
        fi
        success "Build tools installed"
    else
        fail "Cannot continue without gcc, make, and git."
    fi
}

# ── Build from source ───────────────────────────────────────
build_from_source() {
    info "Building AXIS from source..."

    # Check prerequisites — offer to install if missing
    if ! check_cmd gcc && ! check_cmd cc || ! check_cmd make || ! check_cmd git; then
        install_build_tools
    fi

    # Clone or update
    local tmpdir
    tmpdir="$(mktemp -d)"
    trap 'rm -rf "$tmpdir"' EXIT

    info "Cloning repository..."
    git clone --depth 1 "https://github.com/${REPO}.git" "$tmpdir/axis-lang" 2>&1 | tail -1

    info "Compiling..."
    cd "$tmpdir/axis-lang/axcc"
    make CC="${CC:-gcc}" 2>&1 | tail -5

    if [ ! -f axis ]; then
        fail "Build failed — axis binary not found"
    fi
    success "Build successful"

    # Install
    mkdir -p "$INSTALL_DIR"
    cp axis "$INSTALL_DIR/axis"
    chmod +x "$INSTALL_DIR/axis"
    ln -sf "$INSTALL_DIR/axis" "$INSTALL_DIR/ax"
    success "Installed to ${INSTALL_DIR}/"
}

# ── Check PATH ───────────────────────────────────────────────
check_path() {
    if [[ ":$PATH:" != *":${INSTALL_DIR}:"* ]]; then
        echo ""
        warn "${INSTALL_DIR} is not in your PATH."
        echo ""

        local shell_name rc_file
        shell_name="$(basename "${SHELL:-/bin/bash}")"

        case "$shell_name" in
            zsh)  rc_file="$HOME/.zshrc" ;;
            fish) rc_file="$HOME/.config/fish/config.fish" ;;
            *)    rc_file="$HOME/.bashrc" ;;
        esac

        echo -e "  Add it by running:"
        echo ""

        if [ "$shell_name" = "fish" ]; then
            echo -e "    ${BOLD}fish_add_path ${INSTALL_DIR}${RESET}"
        else
            echo -e "    ${BOLD}echo 'export PATH=\"${INSTALL_DIR}:\$PATH\"' >> ${rc_file}${RESET}"
            echo -e "    ${BOLD}source ${rc_file}${RESET}"
        fi
        echo ""
    else
        success "PATH already includes ${INSTALL_DIR}"
    fi
}

# ── Success message ──────────────────────────────────────────
show_success() {
    echo ""
    echo -e "${GREEN}════════════════════════════════════════${RESET}"
    echo -e "${GREEN}  Installation complete!${RESET}"
    echo -e "${GREEN}════════════════════════════════════════${RESET}"
    echo ""
    echo -e "  ${YELLOW}IMPORTANT NOTICE${RESET}"
    echo ""
    echo "  AXCC (the AXIS Compiler) does not use GCC, LLVM, or NASM"
    echo "  as a backend. The entire compiler -- lexer, parser, semantic"
    echo "  analysis, x86-64 code generation, PE/ELF emission -- is 100%"
    echo "  handwritten by a single person."
    echo ""
    echo "  While AXCC has been extensively tested (26 test cases covering"
    echo "  all language features), you may still encounter bugs."
    echo ""
    echo -e "  If you find a bug, please open an issue on GitHub:"
    echo -e "  ${CYAN}https://github.com/${REPO}/issues${RESET}"
    echo ""
    echo -e "  ${YELLOW}════════════════════════════════════════${RESET}"
    echo ""
    read -rp "  Did you understand? [Y/n] " understood
    echo ""
    echo -e "  Quick start:"
    echo -e "    ${BOLD}axis hello.axis -o hello${RESET}"
    echo -e "    ${BOLD}./hello${RESET}"
    echo ""
    read -rp "  Would you like to download the AXIS Guide? [Y/n] " openguide
    openguide="${openguide:-Y}"
    if [[ "$openguide" =~ ^[Yy]$ ]]; then
        local url="https://github.com/${REPO}/tree/main/docs/guide"
        if check_cmd xdg-open; then
            xdg-open "$url" 2>/dev/null &
        elif check_cmd open; then
            open "$url" 2>/dev/null &
        else
            echo -e "  Open manually: ${CYAN}${url}${RESET}"
        fi
    fi
    echo ""
    echo -e "  ${GREEN}Happy coding!${RESET}"
    echo ""
}

# ── Main ─────────────────────────────────────────────────────
main() {
    show_banner
    detect_platform
    build_from_source
    check_path
    show_success
}

main "$@"
