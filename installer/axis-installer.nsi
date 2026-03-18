; ============================================================
; AXIS Language Installer — NSIS Script
; ============================================================
; Installs axis.exe and ax.bat alias, optionally adds to PATH,
; shows ASCII art welcome in a terminal on finish.
; ============================================================

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
!include "WinMessages.nsh"

; ── Metadata ─────────────────────────────────────────────────
Name "AXIS Language"
OutFile "axis-installer.exe"
InstallDir "$LOCALAPPDATA\AXIS"
InstallDirRegKey HKCU "Software\AXIS" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma

; ── Version info ─────────────────────────────────────────────
VIProductVersion "1.2.1.0"
VIAddVersionKey "ProductName" "AXIS Language"
VIAddVersionKey "ProductVersion" "1.2.1"
VIAddVersionKey "FileDescription" "AXIS Language Installer"
VIAddVersionKey "LegalCopyright" "MIT License"

; ── Variables ────────────────────────────────────────────────
Var AddToPathCheckbox
Var AddToPathState

; ── MUI Settings ─────────────────────────────────────────────
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; ── Pages ────────────────────────────────────────────────────
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
Page custom OptionsPage OptionsPageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; ── Language ─────────────────────────────────────────────────
!insertmacro MUI_LANGUAGE "English"

; ── Options Page ─────────────────────────────────────────────
Function OptionsPage
    nsDialogs::Create 1018
    Pop $0

    ${NSD_CreateLabel} 0 0 100% 24u "Choose additional options:"
    Pop $0

    ${NSD_CreateCheckbox} 10u 30u 100% 12u "Add AXIS to PATH (recommended)"
    Pop $AddToPathCheckbox
    ${NSD_Check} $AddToPathCheckbox

    nsDialogs::Show
FunctionEnd

Function OptionsPageLeave
    ${NSD_GetState} $AddToPathCheckbox $AddToPathState
FunctionEnd

; ── Install Section ──────────────────────────────────────────
Section "AXIS Compiler" SecMain
    SectionIn RO
    SetOutPath "$INSTDIR"

    ; Copy compiler binary
    File "/oname=axis.exe" "..\axcc\axis.exe"

    ; Create ax alias (batch redirect)
    FileOpen $1 "$INSTDIR\ax.bat" w
    FileWrite $1 '@"%~dp0axis.exe" %*$\r$\n'
    FileClose $1

    ; Create welcome script for terminal
    FileOpen $0 "$INSTDIR\welcome.bat" w
    FileWrite $0 "@echo off$\r$\n"
    FileWrite $0 "chcp 65001 >nul 2>&1$\r$\n"
    FileWrite $0 "cls$\r$\n"
    FileWrite $0 "echo.$\r$\n"
    FileWrite $0 "echo      AAAAA  XX   XX II SSSSSSS$\r$\n"
    FileWrite $0 "echo     AA   AA  XX XX  II SS     $\r$\n"
    FileWrite $0 "echo     AAAAAAA   XXX   II SSSSSSS$\r$\n"
    FileWrite $0 "echo     AA   AA  XX XX  II      SS$\r$\n"
    FileWrite $0 "echo     AA   AA XX   XX II SSSSSSS$\r$\n"
    FileWrite $0 "echo.$\r$\n"
    FileWrite $0 "echo   AXIS Language v1.2.1$\r$\n"
    FileWrite $0 "echo   Installation complete!$\r$\n"
    FileWrite $0 "echo.$\r$\n"
    FileWrite $0 "echo   ============================================================$\r$\n"
    FileWrite $0 "echo   IMPORTANT NOTICE$\r$\n"
    FileWrite $0 "echo   ============================================================$\r$\n"
    FileWrite $0 "echo.$\r$\n"
    FileWrite $0 "echo   AXCC (the AXIS Compiler) does not use GCC, LLVM, or NASM$\r$\n"
    FileWrite $0 "echo   as a backend. The entire compiler -- lexer, parser, semantic$\r$\n"
    FileWrite $0 "echo   analysis, x86-64 code generation, PE/ELF emission -- is 100%%$\r$\n"
    FileWrite $0 "echo   handwritten by a single person.$\r$\n"
    FileWrite $0 "echo.$\r$\n"
    FileWrite $0 "echo   While AXCC has been extensively tested (26 test cases covering$\r$\n"
    FileWrite $0 "echo   all language features), you may still encounter bugs.$\r$\n"
    FileWrite $0 "echo.$\r$\n"
    FileWrite $0 "echo   If you find a bug, please open an issue on GitHub:$\r$\n"
    FileWrite $0 "echo   https://github.com/AGDNoob/axis-lang/issues$\r$\n"
    FileWrite $0 "echo.$\r$\n"
    FileWrite $0 "echo   ============================================================$\r$\n"
    FileWrite $0 "echo.$\r$\n"
    FileWrite $0 "set /p understood=  Did you understand? [Y/n] $\r$\n"
    FileWrite $0 "echo.$\r$\n"
    FileWrite $0 "echo   Quick start:$\r$\n"
    FileWrite $0 "echo     axis hello.axis -o hello.exe$\r$\n"
    FileWrite $0 "echo     hello.exe$\r$\n"
    FileWrite $0 "echo.$\r$\n"
    FileWrite $0 "set /p openguide=  Would you like to download the AXIS Guide? [Y/n] $\r$\n"
    FileWrite $0 "if /i $\"%%openguide%%$\" == $\"n$\" goto :done$\r$\n"
    FileWrite $0 "start https://github.com/AGDNoob/axis-lang/tree/main/docs/guide$\r$\n"
    FileWrite $0 ":done$\r$\n"
    FileWrite $0 "echo.$\r$\n"
    FileWrite $0 "echo   Happy coding!$\r$\n"
    FileWrite $0 "echo.$\r$\n"
    FileWrite $0 "pause$\r$\n"
    FileClose $0

    ; Write registry keys for uninstaller
    WriteRegStr HKCU "Software\AXIS" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AXIS" \
        "DisplayName" "AXIS Language"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AXIS" \
        "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AXIS" \
        "DisplayVersion" "1.2.1"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AXIS" \
        "Publisher" "AXIS Language"
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AXIS" \
        "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AXIS" \
        "NoRepair" 1

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; ── Add to PATH (if checked) ─────────────────────────────
    ${If} $AddToPathState == ${BST_CHECKED}
        ; Idempotent: only appends if $INSTDIR is not already in user PATH
        nsExec::ExecToLog 'powershell -NoProfile -Command "$$p = [Environment]::GetEnvironmentVariable(\"Path\",\"User\"); if(-not $$p){[Environment]::SetEnvironmentVariable(\"Path\",\"$INSTDIR\",\"User\")} elseif(-not ($$p -split \";\" | Where-Object {$$_ -eq \"$INSTDIR\"})){[Environment]::SetEnvironmentVariable(\"Path\",\"$$p;$INSTDIR\",\"User\")}"'
        ; Broadcast environment change
        SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000
    ${EndIf}

    ; ── Always open welcome terminal ─────────────────────────
    Exec '"cmd.exe" /K "$INSTDIR\welcome.bat"'

SectionEnd

; ── Uninstall Section ────────────────────────────────────────
Section "Uninstall"
    ; Remove files
    Delete "$INSTDIR\axis.exe"
    Delete "$INSTDIR\ax.bat"
    Delete "$INSTDIR\welcome.bat"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"

    ; Remove from PATH via PowerShell (reliable string replacement)
    nsExec::ExecToLog 'powershell -NoProfile -Command "$$p = [Environment]::GetEnvironmentVariable(\"Path\",\"User\"); if($$p) { $$n = ($$p -split \";\" | Where-Object { $$_ -ne \"$INSTDIR\" }) -join \";\"; [Environment]::SetEnvironmentVariable(\"Path\",$$n,\"User\") }"'
    SendMessage ${HWND_BROADCAST} ${WM_WININICHANGE} 0 "STR:Environment" /TIMEOUT=5000

    ; Remove registry keys
    DeleteRegKey HKCU "Software\AXIS"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AXIS"
SectionEnd
