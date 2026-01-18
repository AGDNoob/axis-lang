#!/usr/bin/env bash
#
# AXIS Language Installer for Linux
# Version: 1.0.2-beta
#
# Usage: ./install.sh [--user|--system]
#

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default installation mode
INSTALL_MODE="user"

# Parse arguments
if [ "$1" = "--system" ]; then
    INSTALL_MODE="system"
elif [ "$1" = "--user" ]; then
    INSTALL_MODE="user"
elif [ -n "$1" ]; then
    echo -e "${RED}Error: Unknown option '$1'${NC}"
    echo "Usage: $0 [--user|--system]"
    exit 1
fi

# Determine installation paths
if [ "$INSTALL_MODE" = "system" ]; then
    BIN_DIR="/usr/local/bin"
    LIB_DIR="/usr/local/lib/axis"
    
    # Check for root privileges
    if [ "$EUID" -ne 0 ]; then
        echo -e "${RED}Error: System installation requires root privileges${NC}"
        echo "Please run: sudo $0 --system"
        exit 1
    fi
else
    BIN_DIR="$HOME/.local/bin"
    LIB_DIR="$HOME/.local/lib/axis"
    
    # Create directories if they don't exist
    mkdir -p "$BIN_DIR"
    mkdir -p "$LIB_DIR"
fi

echo -e "${GREEN}═══════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  AXIS Language Installer (v1.0.2-beta)${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════════${NC}"
echo ""
echo "Installation mode: $INSTALL_MODE"
echo "Binary directory:  $BIN_DIR"
echo "Library directory: $LIB_DIR"
echo ""

# Check Python version
echo -n "Checking Python version... "
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}FAILED${NC}"
    echo -e "${RED}Error: Python 3 is not installed${NC}"
    echo "Please install Python 3.7 or higher"
    exit 1
fi

PYTHON_VERSION=$(python3 --version 2>&1 | awk '{print $2}')
PYTHON_MAJOR=$(echo "$PYTHON_VERSION" | cut -d. -f1)
PYTHON_MINOR=$(echo "$PYTHON_VERSION" | cut -d. -f2)

if [ "$PYTHON_MAJOR" -lt 3 ] || { [ "$PYTHON_MAJOR" -eq 3 ] && [ "$PYTHON_MINOR" -lt 7 ]; }; then
    echo -e "${RED}FAILED${NC}"
    echo -e "${RED}Error: Python 3.7+ required, found $PYTHON_VERSION${NC}"
    exit 1
fi
echo -e "${GREEN}OK${NC} (Python $PYTHON_VERSION)"

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Check if compiler files exist
echo -n "Checking compiler files... "
REQUIRED_FILES=(
    "tokenization_engine.py"
    "syntactic_analyzer.py"
    "semantic_analyzer.py"
    "code_generator.py"
    "executable_format_generator.py"
    "compilation_pipeline.py"
    "transpiler.py"
    "assembler.py"
)

for file in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$PROJECT_ROOT/$file" ]; then
        echo -e "${RED}FAILED${NC}"
        echo -e "${RED}Error: Missing compiler file: $file${NC}"
        exit 1
    fi
done
echo -e "${GREEN}OK${NC}"

# Create library directory
echo -n "Creating library directory... "
if [ "$INSTALL_MODE" = "system" ]; then
    mkdir -p "$LIB_DIR"
fi
echo -e "${GREEN}OK${NC}"

# Copy compiler files
echo -n "Installing compiler files... "
for file in "${REQUIRED_FILES[@]}"; do
    cp "$PROJECT_ROOT/$file" "$LIB_DIR/"
done
echo -e "${GREEN}OK${NC}"

# Install wrapper script
echo -n "Installing AXIS command... "
cp "$SCRIPT_DIR/axis" "$BIN_DIR/axis"
chmod +x "$BIN_DIR/axis"

# Update shebang to use correct library path
if [ "$INSTALL_MODE" = "system" ]; then
    sed -i "s|^AXIS_LIB_DIR=.*|AXIS_LIB_DIR=\"/usr/local/lib/axis\"|" "$BIN_DIR/axis"
else
    sed -i "s|^AXIS_LIB_DIR=.*|AXIS_LIB_DIR=\"\$HOME/.local/lib/axis\"|" "$BIN_DIR/axis"
fi
echo -e "${GREEN}OK${NC}"

echo ""
echo -e "${GREEN}═══════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  Installation completed successfully!${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════════${NC}"
echo ""

# Check if BIN_DIR is in PATH
if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
    echo -e "${YELLOW}⚠ Warning: $BIN_DIR is not in your PATH${NC}"
    echo ""
    echo "Add this line to your ~/.bashrc or ~/.zshrc:"
    echo ""
    echo "    export PATH=\"$BIN_DIR:\$PATH\""
    echo ""
    echo "Then reload your shell:"
    echo "    source ~/.bashrc  # or source ~/.zshrc"
    echo ""
fi

# Test installation
if command -v axis &> /dev/null || [ -x "$BIN_DIR/axis" ]; then
    echo "Quick test:"
    echo "    axis --version"
    echo ""
    echo "Example usage:"
    echo "    axis build program.axis -o program"
    echo "    axis run program.axis"
    echo ""
else
    echo -e "${YELLOW}⚠ Warning: 'axis' command not immediately available${NC}"
    echo "You may need to restart your terminal or run: hash -r"
    echo ""
fi

echo "Documentation: https://github.com/AGDNoob/axis-lang"
echo ""
