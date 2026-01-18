@echo off
REM AXIS Language CLI - Windows Wrapper
REM Version: 1.0.2-beta - Dual Mode (Script + Compile)

setlocal enabledelayedexpansion

REM Find Python
where python >nul 2>&1
if %errorlevel% equ 0 (
    set "PYTHON_CMD=python"
) else (
    where python3 >nul 2>&1
    if %errorlevel% equ 0 (
        set "PYTHON_CMD=python3"
    ) else (
        echo Error: Python not found
        exit /b 1
    )
)

REM Get script directory (where axis.bat is located)
set "AXIS_DIR=%~dp0"
set "AXIS_DIR=%AXIS_DIR:~0,-1%"

REM Check for compilation_pipeline.py in parent or current
if exist "%AXIS_DIR%\..\compilation_pipeline.py" (
    set "AXIS_LIB_DIR=%AXIS_DIR%\.."
) else if exist "%AXIS_DIR%\compilation_pipeline.py" (
    set "AXIS_LIB_DIR=%AXIS_DIR%"
) else (
    echo Error: AXIS library not found
    exit /b 1
)

REM Handle commands
if "%~1"=="" (
    echo Error: No command specified
    echo Run 'axis --help' for usage information
    exit /b 1
)

if "%~1"=="--help" goto :show_help
if "%~1"=="-h" goto :show_help
if "%~1"=="help" goto :show_help
if "%~1"=="--version" goto :show_version
if "%~1"=="-V" goto :show_version
if "%~1"=="build" goto :cmd_build
if "%~1"=="run" goto :cmd_run
if "%~1"=="check" goto :cmd_check
if "%~1"=="info" goto :cmd_info
if "%~1"=="update" goto :cmd_update

REM Auto mode - file specified directly
if not exist "%~1" (
    echo Error: File not found: %~1
    exit /b 1
)

REM Detect mode and run
%PYTHON_CMD% "%AXIS_LIB_DIR%\compilation_pipeline.py" "%~1" %2 %3 %4 %5 %6 %7 %8 %9
exit /b %errorlevel%

:show_help
echo AXIS Language (1.0.2-beta) - Dual Mode: Script ^& Compile
echo.
echo Usage:
echo     axis ^<file.axis^>                     Auto-detect mode and run/compile
echo     axis run ^<file.axis^>                 Force interpret (script mode)
echo     axis build ^<file.axis^> [options]     Force compile (compile mode)
echo     axis check ^<file.axis^>               Check syntax without running
echo     axis info                            Show system and environment info
echo     axis update                          Update AXIS from GitHub
echo     axis --version                       Show version information
echo     axis --help                          Show this help message
echo.
echo Mode Detection:
echo     If file starts with 'mode script'  - Interpreted (cross-platform)
echo     If file starts with 'mode compile' - Compiled to ELF64 (Linux only)
echo     Default (no mode declaration)      - Compile mode
echo.
echo Examples:
echo     axis script.axis                     # Runs if 'mode script'
echo     axis run program.axis                # Force run as script
echo     axis build program.axis              # Compile to ./program
echo     axis check program.axis              # Validate syntax only
exit /b 0

:show_version
echo AXIS Language
echo Version: 1.0.2-beta
echo Modes: script (interpreted), compile (native ELF64)
echo Platform: Windows (script mode only), Linux (both modes)
exit /b 0

:cmd_build
shift
if "%~1"=="" (
    echo Error: No input file specified
    exit /b 1
)
if not exist "%~1" (
    echo Error: File not found: %~1
    exit /b 1
)
%PYTHON_CMD% "%AXIS_LIB_DIR%\compilation_pipeline.py" build %1 %2 %3 %4 %5 %6 %7 %8 %9
exit /b %errorlevel%

:cmd_run
shift
if "%~1"=="" (
    echo Error: No input file specified
    exit /b 1
)
if not exist "%~1" (
    echo Error: File not found: %~1
    exit /b 1
)
%PYTHON_CMD% "%AXIS_LIB_DIR%\compilation_pipeline.py" run "%~1"
exit /b %errorlevel%

:cmd_check
shift
if "%~1"=="" (
    echo Error: No input file specified
    exit /b 1
)
if not exist "%~1" (
    echo Error: File not found: %~1
    exit /b 1
)
%PYTHON_CMD% "%AXIS_LIB_DIR%\compilation_pipeline.py" check "%~1"
exit /b %errorlevel%

:cmd_info
echo.
echo AXIS Language - System Information
echo ===================================
echo.
echo Version:    1.0.2-beta
echo Platform:   Windows
echo Modes:      script (interpreted)
echo.
echo Python:
for /f "tokens=*" %%i in ('%PYTHON_CMD% --version 2^>^&1') do echo     %%i
echo.
echo AXIS Location: %AXIS_LIB_DIR%
echo.
echo Note: Compile mode (native ELF64) is only available on Linux.
echo       Script mode works on all platforms.
exit /b 0

:cmd_update
echo.
echo AXIS Update
echo ===========
echo.
echo Updating from GitHub...
echo.
pushd "%AXIS_LIB_DIR%"
git pull origin main
if %errorlevel% equ 0 (
    echo.
    echo Update complete!
) else (
    echo.
    echo Update failed. Make sure git is installed and you have internet access.
    echo You can also manually update by downloading from:
    echo https://github.com/AGDNoob/axis-lang
)
popd
exit /b 0
