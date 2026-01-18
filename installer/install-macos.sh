#!/bin/bash
# AXIS Language Installer for macOS
# Version 1.0.2-beta
# GUI-based installer using AppleScript dialogs

set -e

# ============================================================================
# CONFIGURATION
# ============================================================================

AXIS_VERSION="1.0.2-beta"
GITHUB_RAW="https://raw.githubusercontent.com/AGDNoob/axis-lang/main"
MIN_PYTHON_VERSION="3.7"
INSTALL_DIR="$HOME/.local/lib/axis"
BIN_DIR="$HOME/.local/bin"

FILES_TO_DOWNLOAD=(
    "compilation_pipeline.py"
    "tokenization_engine.py"
    "syntactic_analyzer.py"
    "semantic_analyzer.py"
    "code_generator.py"
    "executable_format_generator.py"
)

# ============================================================================
# GUI FUNCTIONS (AppleScript)
# ============================================================================

show_info() {
    local title="$1"
    local message="$2"
    
    osascript -e "display dialog \"$message\" with title \"$title\" buttons {\"OK\"} default button \"OK\" with icon note"
}

show_error() {
    local title="$1"
    local message="$2"
    
    osascript -e "display dialog \"$message\" with title \"$title\" buttons {\"OK\"} default button \"OK\" with icon stop"
}

show_question() {
    local title="$1"
    local message="$2"
    
    result=$(osascript -e "display dialog \"$message\" with title \"$title\" buttons {\"No\", \"Yes\"} default button \"Yes\"" 2>/dev/null)
    if echo "$result" | grep -q "Yes"; then
        return 0
    else
        return 1
    fi
}

show_progress() {
    local message="$1"
    
    # macOS doesn't have a simple progress dialog, so we'll use notifications
    osascript -e "display notification \"$message\" with title \"AXIS Installer\""
}

show_checklist() {
    local title="$1"
    local message="$2"
    
    result=$(osascript -e "display dialog \"$message\" with title \"$title\" buttons {\"Skip\", \"Install Extension\"} default button \"Install Extension\"" 2>/dev/null)
    if echo "$result" | grep -q "Install Extension"; then
        echo "vscode"
    fi
}

# ============================================================================
# HELPER FUNCTIONS
# ============================================================================

version_ge() {
    printf '%s\n%s\n' "$2" "$1" | sort -V -C
}

get_python_version() {
    local python_cmd=""
    local version=""
    
    for cmd in python3 python; do
        if command -v $cmd &> /dev/null; then
            version=$($cmd --version 2>&1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
            if [ -n "$version" ] && version_ge "$version" "$MIN_PYTHON_VERSION"; then
                python_cmd=$cmd
                break
            fi
        fi
    done
    
    echo "$python_cmd:$version"
}

install_python() {
    # Check for Homebrew
    if ! command -v brew &> /dev/null; then
        show_info "Installing Homebrew" "Homebrew is required to install Python.\nA terminal will open to install Homebrew.\nPlease follow the instructions."
        
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
        
        # Add Homebrew to PATH for this session
        if [ -f "/opt/homebrew/bin/brew" ]; then
            eval "$(/opt/homebrew/bin/brew shellenv)"
        elif [ -f "/usr/local/bin/brew" ]; then
            eval "$(/usr/local/bin/brew shellenv)"
        fi
    fi
    
    show_progress "Installing Python via Homebrew..."
    brew install python
    
    return 0
}

download_file() {
    local url="$1"
    local dest="$2"
    
    curl -fsSL "$url" -o "$dest"
}

uninstall_axis() {
    show_progress "Removing AXIS files..."
    rm -rf "$INSTALL_DIR"
    
    show_progress "Removing axis command..."
    rm -f "$BIN_DIR/axis"
    
    show_progress "Cleaning up shell configs..."
    for rc in "$HOME/.zshrc" "$HOME/.bash_profile" "$HOME/.profile"; do
        if [ -f "$rc" ]; then
            sed -i '' '/# AXIS Language/d' "$rc"
            sed -i '' '/\.local\/bin/d' "$rc"
        fi
    done
    
    show_progress "Uninstalling VS Code extension..."
    if command -v code &> /dev/null; then
        code --uninstall-extension AGDNoob.axis-lang 2>/dev/null || true
    fi
    
    show_info "Uninstall Complete" "AXIS has been removed.

Restart your terminal to complete the process."
}

# ============================================================================
# MAIN INSTALLATION
# ============================================================================

main() {
    # Check if already installed
    local is_installed=false
    if [ -d "$INSTALL_DIR" ] && [ -f "$BIN_DIR/axis" ]; then
        is_installed=true
    fi
    
    # Show action menu if installed
    if [ "$is_installed" = true ]; then
        local action
        action=$(osascript -e 'display dialog "AXIS is already installed. What would you like to do?" with title "AXIS Installer" buttons {"Cancel", "Uninstall", "Reinstall"} default button "Reinstall"' 2>/dev/null)
        
        if echo "$action" | grep -q "Uninstall"; then
            local confirm
            confirm=$(osascript -e 'display dialog "Are you sure you want to uninstall AXIS?" with title "Confirm Uninstall" buttons {"Cancel", "Uninstall"} default button "Cancel"' 2>/dev/null)
            if echo "$confirm" | grep -q "Uninstall"; then
                uninstall_axis
                exit 0
            else
                exit 0
            fi
        elif echo "$action" | grep -q "Cancel"; then
            exit 0
        fi
    fi
    
    # Welcome message
    local python_info=$(get_python_version)
    local python_cmd=$(echo "$python_info" | cut -d: -f1)
    local python_version=$(echo "$python_info" | cut -d: -f2)
    
    local status_text="AXIS Language Installer v$AXIS_VERSION

"
    
    if [ -n "$python_cmd" ]; then
        status_text+="✓ Python $python_version found
"
    else
        status_text+="✗ Python 3.7+ not found (will be installed via Homebrew)
"
    fi
    
    status_text+="
Install location: $INSTALL_DIR

Click OK to continue..."
    
    show_info "AXIS Installer" "$status_text"
    
    # Options selection
    local options=$(show_checklist "VS Code Extension" "Would you like to install the VS Code extension for AXIS syntax highlighting?")
    local install_vscode=false
    
    if echo "$options" | grep -qi "vscode"; then
        install_vscode=true
    fi
    
    # Start installation
    show_progress "Starting installation..."
    
    # Install Python if needed
    if [ -z "$python_cmd" ]; then
        show_progress "Installing Python..."
        if ! install_python; then
            show_error "Installation Failed" "Failed to install Python. Please install Python 3.7+ manually."
            exit 1
        fi
        python_cmd="python3"
    fi
    
    show_progress "Creating directories..."
    mkdir -p "$INSTALL_DIR"
    mkdir -p "$BIN_DIR"
    
    show_progress "Downloading AXIS files..."
    
    for file in "${FILES_TO_DOWNLOAD[@]}"; do
        show_progress "Downloading $file..."
        
        if ! download_file "$GITHUB_RAW/$file" "$INSTALL_DIR/$file"; then
            show_error "Download Failed" "Failed to download $file"
            exit 1
        fi
    done
    
    show_progress "Creating axis command..."
    
    # Create the axis wrapper script
    cat > "$BIN_DIR/axis" << 'AXIS_SCRIPT'
#!/bin/bash
# AXIS Language CLI
# Version: AXIS_VERSION_PLACEHOLDER

AXIS_DIR="INSTALL_DIR_PLACEHOLDER"
PYTHON_CMD="PYTHON_CMD_PLACEHOLDER"

show_help() {
    echo "AXIS Language vAXIS_VERSION_PLACEHOLDER"
    echo ""
    echo "Usage: axis <command> [options]"
    echo ""
    echo "Commands:"
    echo "  run <file.axis>     Run an AXIS script (script mode)"
    echo "  build <file.axis>   Compile to ELF64 binary (Linux only)"
    echo "  check <file.axis>   Check syntax without running"
    echo "  info                Show installation info"
    echo "  version             Show version"
    echo "  help                Show this help message"
    echo ""
    echo "Examples:"
    echo "  axis run hello.axis"
    echo "  axis check myprogram.axis"
    echo ""
    echo "Note: Build mode (ELF64) is only available on Linux."
}

case "$1" in
    run)
        if [ -z "$2" ]; then
            echo "Error: No script file specified"
            echo "Usage: axis run script.axis"
            exit 1
        fi
        "$PYTHON_CMD" "$AXIS_DIR/compilation_pipeline.py" "$2" --run
        ;;
    build)
        echo "Error: Build mode (ELF64) is only available on Linux."
        echo "Use 'axis run' to run scripts on macOS."
        exit 1
        ;;
    check)
        if [ -z "$2" ]; then
            echo "Error: No script file specified"
            echo "Usage: axis check script.axis"
            exit 1
        fi
        "$PYTHON_CMD" "$AXIS_DIR/compilation_pipeline.py" "$2" --check
        ;;
    info)
        echo "AXIS Language vAXIS_VERSION_PLACEHOLDER"
        echo ""
        echo "Platform: macOS (Script Mode Only)"
        echo "Installation: $AXIS_DIR"
        echo "Python: $PYTHON_CMD"
        echo "Python Version: $("$PYTHON_CMD" --version 2>&1)"
        ;;
    version|--version|-v)
        echo "AXIS vAXIS_VERSION_PLACEHOLDER"
        ;;
    help|--help|-h|"")
        show_help
        ;;
    *)
        echo "Unknown command: $1"
        show_help
        exit 1
        ;;
esac
AXIS_SCRIPT
    
    # Replace placeholders
    sed -i '' "s|AXIS_VERSION_PLACEHOLDER|$AXIS_VERSION|g" "$BIN_DIR/axis"
    sed -i '' "s|INSTALL_DIR_PLACEHOLDER|$INSTALL_DIR|g" "$BIN_DIR/axis"
    sed -i '' "s|PYTHON_CMD_PLACEHOLDER|$python_cmd|g" "$BIN_DIR/axis"
    
    chmod +x "$BIN_DIR/axis"
    
    show_progress "Updating PATH..."
    
    # Add to PATH if not already there
    if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
        # Add to shell config
        for rc in "$HOME/.zshrc" "$HOME/.bash_profile" "$HOME/.profile"; do
            if [ -f "$rc" ] || [ "$rc" = "$HOME/.zshrc" ]; then
                touch "$rc"
                if ! grep -q "AXIS" "$rc"; then
                    echo "" >> "$rc"
                    echo "# AXIS Language" >> "$rc"
                    echo "export PATH=\"\$PATH:$BIN_DIR\"" >> "$rc"
                fi
            fi
        done
    fi
    
    if [ "$install_vscode" = true ]; then
        show_progress "Installing VS Code extension..."
        
        if command -v code &> /dev/null; then
            code --install-extension AGDNoob.axis-lang 2>/dev/null || true
        fi
    fi
    
    show_info "Installation Complete" "AXIS has been installed successfully!

Restart your terminal and run:
  axis help

Or try:
  axis run yourscript.axis"
}

# Run main
main "$@"
