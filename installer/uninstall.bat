@echo off
REM AXIS Language Uninstaller for Windows
REM Version: 1.0.2-beta

setlocal enabledelayedexpansion

echo.
echo ===================================================
echo   AXIS Language Uninstaller (Windows)
echo ===================================================
echo.

REM Check common installation locations
set "FOUND=0"

REM Check user AppData location
set "USER_DIR=%LOCALAPPDATA%\AXIS"
if exist "%USER_DIR%" (
    set "FOUND=1"
    echo Found installation at: %USER_DIR%
    echo Removing...
    rmdir /s /q "%USER_DIR%"
    echo   Done.
    echo.
)

REM Check if axis is in PATH (user profile)
set "USER_BIN=%USERPROFILE%\.local\bin\axis.bat"
if exist "%USER_BIN%" (
    set "FOUND=1"
    echo Found: %USER_BIN%
    del /q "%USER_BIN%"
    echo   Removed.
    echo.
)

REM Check user lib directory
set "USER_LIB=%USERPROFILE%\.local\lib\axis"
if exist "%USER_LIB%" (
    set "FOUND=1"
    echo Found: %USER_LIB%
    rmdir /s /q "%USER_LIB%"
    echo   Removed.
    echo.
)

REM Check Program Files
set "PROGRAM_DIR=%ProgramFiles%\AXIS"
if exist "%PROGRAM_DIR%" (
    set "FOUND=1"
    echo Found installation at: %PROGRAM_DIR%
    echo Removing (may require admin)...
    rmdir /s /q "%PROGRAM_DIR%" 2>nul
    if exist "%PROGRAM_DIR%" (
        echo   ERROR: Could not remove. Run as Administrator.
    ) else (
        echo   Done.
    )
    echo.
)

REM Check current directory for development install
if exist "%~dp0..\compilation_pipeline.py" (
    echo.
    echo Note: Development installation detected in %~dp0..
    echo       This is a git clone, not a system install.
    echo       To remove, simply delete the axis-lang folder.
    echo.
)

if "%FOUND%"=="0" (
    echo No AXIS installation found in standard locations.
    echo.
    echo If you installed AXIS by cloning the repository,
    echo simply delete the axis-lang folder.
    echo.
) else (
    echo ===================================================
    echo   AXIS has been uninstalled
    echo ===================================================
    echo.
)

pause
