#!/bin/bash
# AXIS Language Installer for Linux
# Version 1.0.2-beta
# GUI-based installer with Python check and VS Code extension support

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
# GUI DETECTION
# ============================================================================

GUI_TOOL=""

detect_gui() {
    if command -v zenity &> /dev/null; then
        GUI_TOOL="zenity"
    elif command -v kdialog &> /dev/null; then
        GUI_TOOL="kdialog"
    elif command -v yad &> /dev/null; then
        GUI_TOOL="yad"
    else
        echo "No GUI tool found. Installing zenity..."
        install_zenity
    fi
}

install_zenity() {
    if command -v apt-get &> /dev/null; then
        sudo apt-get update && sudo apt-get install -y zenity
    elif command -v dnf &> /dev/null; then
        sudo dnf install -y zenity
    elif command -v pacman &> /dev/null; then
        sudo pacman -S --noconfirm zenity
    elif command -v zypper &> /dev/null; then
        sudo zypper install -y zenity
    else
        echo "Could not install zenity. Please install it manually."
        exit 1
    fi
    GUI_TOOL="zenity"
}

# ============================================================================
# GUI FUNCTIONS
# ============================================================================

show_info() {
    local title="$1"
    local message="$2"
    
    case $GUI_TOOL in
        zenity)
            zenity --info --title="$title" --text="$message" --width=400
            ;;
        kdialog)
            kdialog --title "$title" --msgbox "$message"
            ;;
        yad)
            yad --info --title="$title" --text="$message" --width=400
            ;;
    esac
}

show_error() {
    local title="$1"
    local message="$2"
    
    case $GUI_TOOL in
        zenity)
            zenity --error --title="$title" --text="$message" --width=400
            ;;
        kdialog)
            kdialog --title "$title" --error "$message"
            ;;
        yad)
            yad --error --title="$title" --text="$message" --width=400
            ;;
    esac
}

show_question() {
    local title="$1"
    local message="$2"
    
    case $GUI_TOOL in
        zenity)
            zenity --question --title="$title" --text="$message" --width=400
            return $?
            ;;
        kdialog)
            kdialog --title "$title" --yesno "$message"
            return $?
            ;;
        yad)
            yad --question --title="$title" --text="$message" --width=400
            return $?
            ;;
    esac
}

show_progress() {
    local title="$1"
    
    case $GUI_TOOL in
        zenity)
            zenity --progress --title="$title" --auto-close --auto-kill --width=400
            ;;
        kdialog)
            kdialog --title "$title" --progressbar "Installing..." 100
            ;;
        yad)
            yad --progress --title="$title" --auto-close --width=400
            ;;
    esac
}

show_checklist() {
    local title="$1"
    local text="$2"
    
    case $GUI_TOOL in
        zenity)
            zenity --list --checklist --title="$title" --text="$text" \
                --column="Select" --column="Option" \
                TRUE "Install VS Code Extension (syntax highlighting)" \
                --width=500 --height=200
            ;;
        kdialog)
            kdialog --title "$title" --checklist "$text" \
                1 "Install VS Code Extension" on
            ;;
        yad)
            yad --list --checklist --title="$title" --text="$text" \
                --column="Select" --column="Option" \
                TRUE "Install VS Code Extension (syntax highlighting)" \
                --width=500 --height=200
            ;;
    esac
}

# ============================================================================
# HELPER FUNCTIONS
# ============================================================================

version_ge() {
    # Returns 0 if $1 >= $2
    printf '%s\n%s\n' "$2" "$1" | sort -V -C
}

get_python_version() {
    local python_cmd=""
    local version=""
    
    for cmd in python3 python; do
        if command -v $cmd &> /dev/null; then
            version=$($cmd --version 2>&1 | grep -oP '\d+\.\d+\.\d+' | head -1)
            if [ -n "$version" ] && version_ge "$version" "$MIN_PYTHON_VERSION"; then
                python_cmd=$cmd
                break
            fi
        fi
    done
    
    echo "$python_cmd:$version"
}

install_python() {
    local distro=""
    
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        distro=$ID
    fi
    
    case $distro in
        ubuntu|debian|linuxmint|pop)
            sudo apt-get update
            sudo apt-get install -y python3 python3-pip
            ;;
        fedora|rhel|centos)
            sudo dnf install -y python3 python3-pip
            ;;
        arch|manjaro)
            sudo pacman -S --noconfirm python python-pip
            ;;
        opensuse*)
            sudo zypper install -y python3 python3-pip
            ;;
        *)
            return 1
            ;;
    esac
    
    return 0
}

download_file() {
    local url="$1"
    local dest="$2"
    
    if command -v curl &> /dev/null; then
        curl -fsSL "$url" -o "$dest"
    elif command -v wget &> /dev/null; then
        wget -q "$url" -O "$dest"
    else
        return 1
    fi
}

uninstall_axis() {
    (
        echo "20"
        echo "# Removing AXIS files..."
        rm -rf "$INSTALL_DIR"
        
        echo "50"
        echo "# Removing axis command..."
        rm -f "$BIN_DIR/axis"
        
        echo "70"
        echo "# Cleaning up shell configs..."
        for rc in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
            if [ -f "$rc" ]; then
                sed -i '/# AXIS Language/d' "$rc"
                sed -i '/\.local\/bin/d' "$rc"
            fi
        done
        
        echo "90"
        echo "# Uninstalling VS Code extension..."
        if command -v code &> /dev/null; then
            code --uninstall-extension AGDNoob.axis-lang 2>/dev/null || true
        fi
        
        echo "100"
        echo "# Uninstall complete!"
    ) | show_progress "Uninstalling AXIS"
    
    show_info "Uninstall Complete" "AXIS has been removed.\n\nRestart your terminal to complete the process."
}

# ============================================================================
# MAIN INSTALLATION
# ============================================================================

main() {
    detect_gui
    
    # Check if already installed
    local is_installed=false
    if [ -d "$INSTALL_DIR" ] && [ -f "$BIN_DIR/axis" ]; then
        is_installed=true
    fi
    
    # Show action menu if installed
    if [ "$is_installed" = true ]; then
        local action
        case $GUI_TOOL in
            zenity)
                action=$(zenity --list --title="AXIS Installer" --text="AXIS is already installed. What would you like to do?" \
                    --column="Action" "Reinstall" "Uninstall" --width=400 --height=250)
                ;;
            kdialog)
                action=$(kdialog --title "AXIS Installer" --menu "AXIS is already installed. What would you like to do?" \
                    1 "Reinstall" 2 "Uninstall")
                [ "$action" = "1" ] && action="Reinstall"
                [ "$action" = "2" ] && action="Uninstall"
                ;;
            yad)
                action=$(yad --list --title="AXIS Installer" --text="AXIS is already installed. What would you like to do?" \
                    --column="Action" "Reinstall" "Uninstall" --width=400 --height=250)
                ;;
        esac
        
        if [ "$action" = "Uninstall" ]; then
            if show_question "Confirm Uninstall" "Are you sure you want to uninstall AXIS?"; then
                uninstall_axis
                exit 0
            else
                exit 0
            fi
        elif [ -z "$action" ]; then
            exit 0
        fi
    fi
    
    # Welcome message
    local python_info=$(get_python_version)
    local python_cmd=$(echo "$python_info" | cut -d: -f1)
    local python_version=$(echo "$python_info" | cut -d: -f2)
    
    local status_text="AXIS Language Installer v$AXIS_VERSION\n\n"
    
    if [ -n "$python_cmd" ]; then
        status_text+="✓ Python $python_version found\n"
    else
        status_text+="✗ Python 3.7+ not found (will be installed)\n"
    fi
    
    status_text+="\nInstall location: $INSTALL_DIR\n"
    status_text+="\nClick OK to continue..."
    
    show_info "AXIS Installer" "$status_text"
    
    # Options selection
    local options=$(show_checklist "Installation Options" "Select additional options:")
    local install_vscode=false
    
    if echo "$options" | grep -qi "vscode\|extension"; then
        install_vscode=true
    fi
    
    # Start installation with progress
    (
        echo "10"
        echo "# Checking Python..."
        
        if [ -z "$python_cmd" ]; then
            echo "# Installing Python..."
            if ! install_python; then
                echo "# Failed to install Python"
                exit 1
            fi
            python_cmd="python3"
        fi
        
        echo "20"
        echo "# Creating directories..."
        mkdir -p "$INSTALL_DIR"
        mkdir -p "$BIN_DIR"
        
        echo "30"
        echo "# Downloading AXIS files..."
        
        local total=${#FILES_TO_DOWNLOAD[@]}
        local current=0
        
        for file in "${FILES_TO_DOWNLOAD[@]}"; do
            current=$((current + 1))
            percent=$((30 + (current * 40 / total)))
            echo "$percent"
            echo "# Downloading $file..."
            
            if ! download_file "$GITHUB_RAW/$file" "$INSTALL_DIR/$file"; then
                echo "# Failed to download $file"
                exit 1
            fi
        done
        
        echo "75"
        echo "# Creating axis command..."
        
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
    echo "  axis build program.axis -o program --elf"
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
        if [ -z "$2" ]; then
            echo "Error: No script file specified"
            echo "Usage: axis build script.axis [-o output]"
            exit 1
        fi
        shift
        "$PYTHON_CMD" "$AXIS_DIR/compilation_pipeline.py" "$@"
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
        sed -i "s|AXIS_VERSION_PLACEHOLDER|$AXIS_VERSION|g" "$BIN_DIR/axis"
        sed -i "s|INSTALL_DIR_PLACEHOLDER|$INSTALL_DIR|g" "$BIN_DIR/axis"
        sed -i "s|PYTHON_CMD_PLACEHOLDER|$python_cmd|g" "$BIN_DIR/axis"
        
        chmod +x "$BIN_DIR/axis"
        
        echo "85"
        echo "# Updating PATH..."
        
        # Add to PATH if not already there
        if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
            # Add to shell config
            for rc in "$HOME/.bashrc" "$HOME/.zshrc" "$HOME/.profile"; do
                if [ -f "$rc" ]; then
                    if ! grep -q "AXIS" "$rc"; then
                        echo "" >> "$rc"
                        echo "# AXIS Language" >> "$rc"
                        echo "export PATH=\"\$PATH:$BIN_DIR\"" >> "$rc"
                    fi
                fi
            done
        fi
        
        if [ "$install_vscode" = true ]; then
            echo "90"
            echo "# Installing VS Code extension..."
            
            if command -v code &> /dev/null; then
                code --install-extension AGDNoob.axis-lang 2>/dev/null || true
            fi
        fi
        
        echo "100"
        echo "# Installation complete!"
        
    ) | show_progress "Installing AXIS"
    
    show_info "Installation Complete" "AXIS has been installed successfully!\n\nRestart your terminal and run:\n  axis help\n\nOr try:\n  axis run yourscript.axis"
}

# Run main
main "$@"
