#!/usr/bin/env bash
#
# AXIS Language Uninstaller for Linux
# Version: 1.0.1-beta
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Detect installation mode
SYSTEM_BIN="/usr/local/bin/axis"
SYSTEM_LIB="/usr/local/lib/axis"
USER_BIN="$HOME/.local/bin/axis"
USER_LIB="$HOME/.local/lib/axis"

FOUND_SYSTEM=0
FOUND_USER=0

if [ -f "$SYSTEM_BIN" ] || [ -d "$SYSTEM_LIB" ]; then
    FOUND_SYSTEM=1
fi

if [ -f "$USER_BIN" ] || [ -d "$USER_LIB" ]; then
    FOUND_USER=1
fi

# Check if anything to uninstall
if [ $FOUND_SYSTEM -eq 0 ] && [ $FOUND_USER -eq 0 ]; then
    echo -e "${YELLOW}No AXIS installation found${NC}"
    exit 0
fi

echo -e "${GREEN}═══════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  AXIS Language Uninstaller${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════════${NC}"
echo ""

# Uninstall user installation
if [ $FOUND_USER -eq 1 ]; then
    echo "Found user installation"
    echo -n "Removing $USER_BIN... "
    rm -f "$USER_BIN"
    echo -e "${GREEN}OK${NC}"
    
    echo -n "Removing $USER_LIB... "
    rm -rf "$USER_LIB"
    echo -e "${GREEN}OK${NC}"
    echo ""
fi

# Uninstall system installation
if [ $FOUND_SYSTEM -eq 1 ]; then
    echo "Found system installation"
    
    if [ "$EUID" -ne 0 ]; then
        echo -e "${RED}Error: System uninstallation requires root privileges${NC}"
        echo "Please run: sudo $0"
        exit 1
    fi
    
    echo -n "Removing $SYSTEM_BIN... "
    rm -f "$SYSTEM_BIN"
    echo -e "${GREEN}OK${NC}"
    
    echo -n "Removing $SYSTEM_LIB... "
    rm -rf "$SYSTEM_LIB"
    echo -e "${GREEN}OK${NC}"
    echo ""
fi

echo -e "${GREEN}═══════════════════════════════════════════════════${NC}"
echo -e "${GREEN}  AXIS has been uninstalled${NC}"
echo -e "${GREEN}═══════════════════════════════════════════════════${NC}"
echo ""
