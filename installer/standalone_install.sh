#!/usr/bin/env bash
#
# AXIS Language - Standalone Installer
# Version: 1.0.1-beta
#
# One-command installation: curl -fsSL https://raw.githubusercontent.com/AGDNoob/axis-lang/main/installer/standalone_install.sh | bash
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Configuration
AXIS_VERSION="1.0.1-beta"
GITHUB_REPO="https://github.com/AGDNoob/axis-lang"
GITHUB_RAW="https://raw.githubusercontent.com/AGDNoob/axis-lang/main"
MIN_PYTHON_MAJOR=3
MIN_PYTHON_MINOR=7

# Installation paths
BIN_DIR="$HOME/.local/bin"
LIB_DIR="$HOME/.local/lib/axis"
VSCODE_EXT_DIR="$HOME/.vscode/extensions"

# Temp directory
TEMP_DIR=$(mktemp -d)
trap "rm -rf '$TEMP_DIR'" EXIT

# ============================================================================
# Helper Functions
# ============================================================================

print_header() {
    clear
    echo -e "${CYAN}╔══════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║                                                          ║${NC}"
    echo -e "${CYAN}║         ${GREEN}AXIS Language Installer${CYAN} (Beta $AXIS_VERSION)        ║${NC}"
    echo -e "${CYAN}║                                                          ║${NC}"
    echo -e "${CYAN}╚══════════════════════════════════════════════════════════╝${NC}"
    echo ""
}

print_step() {
    echo -e "${BLUE}▶${NC} $1"
}

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

ask_yes_no() {
    local prompt="$1"
    local default="${2:-n}"
    
    if [ "$default" = "y" ]; then
        prompt="$prompt [Y/n]: "
    else
        prompt="$prompt [y/N]: "
    fi
    
    while true; do
        read -p "$(echo -e "${CYAN}?${NC} $prompt")" response
        response=${response:-$default}
        case "$response" in
            [Yy]|[Yy][Ee][Ss]) return 0 ;;
            [Nn]|[Nn][Oo]) return 1 ;;
            *) echo "Please answer yes or no." ;;
        esac
    done
}

detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        echo "$ID"
    else
        echo "unknown"
    fi
}

# ============================================================================
# Python Installation
# ============================================================================

check_python() {
    if command -v python3 &> /dev/null; then
        PYTHON_VERSION=$(python3 --version 2>&1 | awk '{print $2}')
        PYTHON_MAJOR=$(echo "$PYTHON_VERSION" | cut -d. -f1)
        PYTHON_MINOR=$(echo "$PYTHON_VERSION" | cut -d. -f2)
        
        # Support Python 3.7+ and any future major version (4.x, 5.x, etc.)
        if [ "$PYTHON_MAJOR" -gt $MIN_PYTHON_MAJOR ]; then
            return 0
        elif [ "$PYTHON_MAJOR" -eq $MIN_PYTHON_MAJOR ] && [ "$PYTHON_MINOR" -ge $MIN_PYTHON_MINOR ]; then
            return 0
        fi
    fi
    return 1
}

install_python() {
    local distro=$(detect_distro)
    
    print_step "Installing Python 3.11..."
    
    case "$distro" in
        ubuntu|debian|linuxmint|pop)
            sudo apt update
            sudo apt install -y python3.11 python3-pip
            ;;
        fedora)
            sudo dnf install -y python3.11 python3-pip
            ;;
        arch|manjaro)
            sudo pacman -S --noconfirm python python-pip
            ;;
        opensuse*)
            sudo zypper install -y python311 python311-pip
            ;;
        *)
            print_error "Unsupported distribution: $distro"
            echo "Please install Python 3.7+ manually and run this script again."
            exit 1
            ;;
    esac
    
    if check_python; then
        print_success "Python installed successfully"
    else
        print_error "Python installation failed"
        exit 1
    fi
}

prompt_python_installation() {
    print_header
    print_warning "Python 3.7+ is not installed or version is too old"
    echo ""
    
    if command -v python3 &> /dev/null; then
        CURRENT_VERSION=$(python3 --version 2>&1)
        echo "Current version: $CURRENT_VERSION"
        echo "Required: Python 3.7+"
    else
        echo "Python 3 not found on system"
    fi
    
    echo ""
    echo "Available Python versions to install:"
    echo "  1) Python 3.11 (Recommended)"
    echo "  2) Cancel installation"
    echo ""
    
    while true; do
        read -p "$(echo -e "${CYAN}?${NC} Select option [1-2]: ")" choice
        case "$choice" in
            1)
                if ask_yes_no "Install Python 3.11 now? (requires sudo)" "y"; then
                    install_python
                    return 0
                fi
                ;;
            2)
                print_error "Installation cancelled"
                exit 0
                ;;
            *)
                echo "Invalid option. Please select 1 or 2."
                ;;
        esac
    done
}

# ============================================================================
# AXIS Installation
# ============================================================================

download_files() {
    print_step "Downloading AXIS compiler files..."
    
    local files=(
        "tokenization_engine.py"
        "syntactic_analyzer.py"
        "semantic_analyzer.py"
        "code_generator.py"
        "executable_format_generator.py"
        "compilation_pipeline.py"
        "tets.py"
    )
    
    cd "$TEMP_DIR"
    for file in "${files[@]}"; do
        if ! curl -fsSL "$GITHUB_RAW/$file" -o "$file"; then
            print_error "Failed to download $file"
            exit 1
        fi
    done
    
    # Download CLI wrapper
    if ! curl -fsSL "$GITHUB_RAW/installer/axis" -o "axis"; then
        print_error "Failed to download axis CLI"
        exit 1
    fi
    
    print_success "All files downloaded"
}

install_axis() {
    print_step "Installing AXIS to $LIB_DIR..."
    
    # Create directories
    mkdir -p "$BIN_DIR"
    mkdir -p "$LIB_DIR"
    
    # Copy compiler files
    local files=(
        "tokenization_engine.py"
        "syntactic_analyzer.py"
        "semantic_analyzer.py"
        "code_generator.py"
        "executable_format_generator.py"
        "compilation_pipeline.py"
        "tets.py"
    )
    
    for file in "${files[@]}"; do
        cp "$TEMP_DIR/$file" "$LIB_DIR/"
    done
    
    # Install CLI wrapper
    cp "$TEMP_DIR/axis" "$BIN_DIR/axis"
    chmod +x "$BIN_DIR/axis"
    
    # Update library path in CLI (portable sed for Linux + macOS)
    if sed --version 2>&1 | grep -q GNU; then
        # GNU sed (Linux)
        sed -i "s|^AXIS_LIB_DIR=.*|AXIS_LIB_DIR=\"$HOME/.local/lib/axis\"|" "$BIN_DIR/axis"
    else
        # BSD sed (macOS)
        sed -i '' "s|^AXIS_LIB_DIR=.*|AXIS_LIB_DIR=\"$HOME/.local/lib/axis\"|" "$BIN_DIR/axis"
    fi
    
    print_success "AXIS installed successfully"
}

# ============================================================================
# VS Code Extension Installation
# ============================================================================

check_vscode() {
    if command -v code &> /dev/null; then
        return 0
    fi
    return 1
}

install_vscode_extension() {
    print_step "Installing VS Code extension..."
    
    mkdir -p "$TEMP_DIR/axis-vscode"
    cd "$TEMP_DIR/axis-vscode"
    
    # Download extension files
    local ext_files=(
        "package.json"
        "language-configuration.json"
        "syntaxes/axis.tmLanguage.json"
    )
    
    for file in "${ext_files[@]}"; do
        local dir=$(dirname "$file")
        mkdir -p "$dir"
        if ! curl -fsSL "$GITHUB_RAW/axis-vscode/$file" -o "$file"; then
            print_warning "Failed to download extension file: $file"
            return 1
        fi
    done
    
    # Install extension
    local ext_target="$VSCODE_EXT_DIR/axis-language-0.1.0"
    mkdir -p "$ext_target"
    cp -r * "$ext_target/"
    
    print_success "VS Code extension installed"
    return 0
}

prompt_vscode_installation() {
    echo ""
    echo -e "${CYAN}╔══════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║  Optional: VS Code Extension                            ║${NC}"
    echo -e "${CYAN}╚══════════════════════════════════════════════════════════╝${NC}"
    echo ""
    
    if ! check_vscode; then
        print_warning "VS Code is not installed or not in PATH"
        echo "You can install the extension later manually."
        echo ""
        return 1
    fi
    
    echo "VS Code detected: $(code --version | head -1)"
    echo ""
    echo "Install AXIS syntax highlighting extension?"
    echo "  • Syntax highlighting for .axis files"
    echo "  • Auto-closing brackets"
    echo "  • Comment toggling (Ctrl+/)"
    echo ""
    
    if ask_yes_no "Install VS Code extension?" "y"; then
        install_vscode_extension
        return $?
    fi
    
    return 1
}

# ============================================================================
# PATH Configuration
# ============================================================================

configure_path() {
    if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
        print_warning "$BIN_DIR is not in your PATH"
        echo ""
        
        # Detect shell
        local shell_rc=""
        if [ -n "$BASH_VERSION" ]; then
            shell_rc="$HOME/.bashrc"
        elif [ -n "$ZSH_VERSION" ]; then
            shell_rc="$HOME/.zshrc"
        else
            shell_rc="$HOME/.profile"
        fi
        
        if ask_yes_no "Add $BIN_DIR to PATH in $shell_rc?" "y"; then
            echo "" >> "$shell_rc"
            echo "# AXIS Language" >> "$shell_rc"
            echo "export PATH=\"$BIN_DIR:\$PATH\"" >> "$shell_rc"
            print_success "PATH updated in $shell_rc"
            echo ""
            print_warning "Please restart your terminal or run: source $shell_rc"
        else
            echo ""
            echo "Add this line to your shell config manually:"
            echo ""
            echo "    export PATH=\"$BIN_DIR:\$PATH\""
            echo ""
        fi
    fi
}

# ============================================================================
# Main Installation Flow
# ============================================================================

main() {
    print_header
    
    echo "This script will install:"
    echo "  • AXIS Language Compiler (Beta $AXIS_VERSION)"
    echo "  • CLI tools (axis command)"
    echo "  • Optional: VS Code extension"
    echo ""
    echo "Installation directory: $LIB_DIR"
    echo "Binary directory: $BIN_DIR"
    echo ""
    
    if ! ask_yes_no "Continue with installation?" "y"; then
        print_error "Installation cancelled"
        exit 0
    fi
    
    # Check Python
    print_header
    print_step "Checking Python installation..."
    
    if check_python; then
        PYTHON_VERSION=$(python3 --version 2>&1)
        print_success "Python found: $PYTHON_VERSION"
    else
        prompt_python_installation
    fi
    
    # Download and install AXIS
    print_header
    download_files
    install_axis
    
    # VS Code extension
    VSCODE_INSTALLED=0
    prompt_vscode_installation && VSCODE_INSTALLED=1
    
    # Configure PATH
    print_header
    configure_path
    
    # Final summary
    print_header
    echo -e "${GREEN}╔══════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║                                                          ║${NC}"
    echo -e "${GREEN}║           Installation completed successfully!          ║${NC}"
    echo -e "${GREEN}║                                                          ║${NC}"
    echo -e "${GREEN}╚══════════════════════════════════════════════════════════╝${NC}"
    echo ""
    
    print_success "AXIS $AXIS_VERSION installed"
    if [ $VSCODE_INSTALLED -eq 1 ]; then
        print_success "VS Code extension installed"
    fi
    
    echo ""
    echo -e "${CYAN}Quick Start:${NC}"
    echo ""
    echo "  1. Test installation:"
    echo "     $ axis --version"
    echo ""
    echo "  2. Create a program (hello.axis):"
    echo "     fn main() -> i32 {"
    echo "         return 42;"
    echo "     }"
    echo ""
    echo "  3. Compile and run:"
    echo "     $ axis build hello.axis -o hello"
    echo "     $ ./hello"
    echo "     $ echo \$?    # Output: 42"
    echo ""
    echo -e "${CYAN}Documentation:${NC} $GITHUB_REPO"
    echo ""
    
    if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
        print_warning "Remember to restart your terminal or run: source ~/.bashrc"
        echo ""
    fi
}

# ============================================================================
# Entry Point
# ============================================================================

main "$@"
