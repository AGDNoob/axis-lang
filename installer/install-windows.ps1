# AXIS Language Installer for Windows
# Version 1.0.2-beta
# GUI-based installer with Python check and VS Code extension support

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# ============================================================================
# CONFIGURATION
# ============================================================================

$AXIS_VERSION = "1.0.2-beta"
$GITHUB_RAW = "https://raw.githubusercontent.com/AGDNoob/axis-lang/main"
$MIN_PYTHON_VERSION = [Version]"3.7"
$INSTALL_DIR = "$env:LOCALAPPDATA\AXIS"
$BIN_DIR = "$env:LOCALAPPDATA\AXIS\bin"

# Available Python versions for installation
$PYTHON_VERSIONS = @(
    @{ Version = "3.13.1"; Label = "Python 3.13 (Latest)" },
    @{ Version = "3.12.8"; Label = "Python 3.12 (Recommended)" },
    @{ Version = "3.11.11"; Label = "Python 3.11" },
    @{ Version = "3.10.16"; Label = "Python 3.10" },
    @{ Version = "3.9.21"; Label = "Python 3.9" },
    @{ Version = "3.8.20"; Label = "Python 3.8" }
)

$FILES_TO_DOWNLOAD = @(
    "compilation_pipeline.py",
    "tokenization_engine.py",
    "syntactic_analyzer.py",
    "semantic_analyzer.py",
    "code_generator.py",
    "executable_format_generator.py",
    "assembler.py",
    "transpiler.py",
    "requirements.txt"
)

# ============================================================================
# HELPER FUNCTIONS
# ============================================================================

function Get-PythonVersion {
    try {
        $pythonPath = (Get-Command python -ErrorAction SilentlyContinue).Source
        if ($pythonPath) {
            $versionOutput = & python --version 2>&1
            if ($versionOutput -match "Python (\d+\.\d+\.\d+)") {
                return @{
                    Path = $pythonPath
                    Version = [Version]$Matches[1]
                }
            }
        }
    } catch {}
    
    # Try python3
    try {
        $pythonPath = (Get-Command python3 -ErrorAction SilentlyContinue).Source
        if ($pythonPath) {
            $versionOutput = & python3 --version 2>&1
            if ($versionOutput -match "Python (\d+\.\d+\.\d+)") {
                return @{
                    Path = $pythonPath
                    Version = [Version]$Matches[1]
                }
            }
        }
    } catch {}
    
    return $null
}

function Install-Python {
    param(
        [System.Windows.Forms.Label]$StatusLabel,
        [string]$SelectedVersion = "3.12.8"
    )
    
    $StatusLabel.Text = "Downloading Python $SelectedVersion installer..."
    $StatusLabel.Refresh()
    
    $pythonUrl = "https://www.python.org/ftp/python/$SelectedVersion/python-$SelectedVersion-amd64.exe"
    $installerPath = "$env:TEMP\python-installer.exe"
    
    try {
        Invoke-WebRequest -Uri $pythonUrl -OutFile $installerPath -UseBasicParsing
        
        $StatusLabel.Text = "Installing Python $SelectedVersion (this may take a minute)..."
        $StatusLabel.Refresh()
        
        # Install Python with PATH option
        Start-Process -FilePath $installerPath -ArgumentList "/quiet", "InstallAllUsers=0", "PrependPath=1", "Include_test=0" -Wait
        
        Remove-Item $installerPath -Force -ErrorAction SilentlyContinue
        
        # Refresh environment
        $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "User") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "Machine")
        
        return $true
    } catch {
        return $false
    }
}

function Install-Dependencies {
    param([System.Windows.Forms.Label]$StatusLabel)
    
    $StatusLabel.Text = "Installing dependencies (keystone-engine)..."
    $StatusLabel.Refresh()
    
    try {
        # Install from requirements.txt
        $reqFile = "$INSTALL_DIR\requirements.txt"
        if (Test-Path $reqFile) {
            $result = Start-Process -FilePath "python" -ArgumentList "-m", "pip", "install", "-r", $reqFile, "--quiet" -Wait -PassThru -NoNewWindow
            return ($result.ExitCode -eq 0)
        } else {
            # Fallback: install keystone directly
            $result = Start-Process -FilePath "python" -ArgumentList "-m", "pip", "install", "keystone-engine", "--quiet" -Wait -PassThru -NoNewWindow
            return ($result.ExitCode -eq 0)
        }
    } catch {
        # Dependencies are optional - fallback assembler will be used
        return $true
    }
}

function Get-AxisFiles {
    param([System.Windows.Forms.Label]$StatusLabel, [System.Windows.Forms.ProgressBar]$ProgressBar)
    
    # Create directories
    New-Item -ItemType Directory -Path $INSTALL_DIR -Force | Out-Null
    New-Item -ItemType Directory -Path $BIN_DIR -Force | Out-Null
    
    $total = $FILES_TO_DOWNLOAD.Count
    $current = 0
    
    foreach ($file in $FILES_TO_DOWNLOAD) {
        $current++
        $percent = [int](($current / $total) * 100)
        $ProgressBar.Value = $percent
        $StatusLabel.Text = "Downloading $file..."
        $StatusLabel.Refresh()
        
        try {
            $url = "$GITHUB_RAW/$file"
            $dest = "$INSTALL_DIR\$file"
            Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
        } catch {
            return $false
        }
    }
    
    return $true
}

function New-AxisCommand {
    param([System.Windows.Forms.Label]$StatusLabel, [string]$PythonPath)
    
    $StatusLabel.Text = "Creating axis command..."
    $StatusLabel.Refresh()
    
    # Create the axis.cmd wrapper
    $axisCmd = @"
@echo off
setlocal enabledelayedexpansion

set "AXIS_DIR=$INSTALL_DIR"
set "PYTHON=$PythonPath"
set "AXIS_VERSION=$AXIS_VERSION"

if "%~1"=="" goto :help
if "%~1"=="help" goto :help
if "%~1"=="--help" goto :help
if "%~1"=="-h" goto :help

if "%~1"=="run" (
    if "%~2"=="" (
        echo Error: No script file specified
        echo Usage: axis run script.axis
        exit /b 1
    )
    "%PYTHON%" "%AXIS_DIR%\compilation_pipeline.py" "%~2" --run
    exit /b !errorlevel!
)

if "%~1"=="build" (
    if "%~2"=="" (
        echo Error: No script file specified
        echo Usage: axis build script.axis [-o output]
        exit /b 1
    )
    shift
    "%PYTHON%" "%AXIS_DIR%\compilation_pipeline.py" %*
    exit /b !errorlevel!
)

if "%~1"=="check" (
    if "%~2"=="" (
        echo Error: No script file specified
        echo Usage: axis check script.axis
        exit /b 1
    )
    "%PYTHON%" "%AXIS_DIR%\compilation_pipeline.py" "%~2" --check
    exit /b !errorlevel!
)

if "%~1"=="info" (
    echo AXIS Language v%AXIS_VERSION%
    echo.
    echo Installation: %AXIS_DIR%
    echo Python: %PYTHON%
    for /f "tokens=2" %%v in ('"%PYTHON%" --version 2^>^&1') do echo Python Version: %%v
    exit /b 0
)

if "%~1"=="version" (
    echo AXIS v%AXIS_VERSION%
    exit /b 0
)

if "%~1"=="--version" (
    echo AXIS v%AXIS_VERSION%
    exit /b 0
)

if "%~1"=="-v" (
    echo AXIS v%AXIS_VERSION%
    exit /b 0
)

echo Unknown command: %~1
goto :help

:help
echo AXIS Language v%AXIS_VERSION%
echo.
echo Usage: axis ^<command^> [options]
echo.
echo Commands:
echo   run ^<file.axis^>     Run an AXIS script (script mode)
echo   build ^<file.axis^>   Compile to ELF64 binary (Linux only)
echo   check ^<file.axis^>   Check syntax without running
echo   info                Show installation info
echo   version             Show version
echo   help                Show this help message
echo.
echo Examples:
echo   axis run hello.axis
echo   axis check myprogram.axis
echo   axis build program.axis -o program --elf
exit /b 0
"@
    
    $axisCmd | Out-File -FilePath "$BIN_DIR\axis.cmd" -Encoding ASCII
    
    return $true
}

function Add-ToPath {
    param([System.Windows.Forms.Label]$StatusLabel)
    
    $StatusLabel.Text = "Adding AXIS to PATH..."
    $StatusLabel.Refresh()
    
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if ($userPath -notlike "*$BIN_DIR*") {
        [Environment]::SetEnvironmentVariable("Path", "$userPath;$BIN_DIR", "User")
    }
    
    # Update current session
    $env:Path = "$env:Path;$BIN_DIR"
    
    return $true
}

function Install-VSCodeExtension {
    param([System.Windows.Forms.Label]$StatusLabel)
    
    $StatusLabel.Text = "Installing VS Code extension..."
    $StatusLabel.Refresh()
    
    try {
        $code = Get-Command code -ErrorAction SilentlyContinue
        if ($code) {
            & code --install-extension AGDNoob.axis-lang 2>&1 | Out-Null
            return $true
        }
    } catch {}
    
    return $false
}

function Uninstall-AXIS {
    param([System.Windows.Forms.Label]$StatusLabel, [System.Windows.Forms.ProgressBar]$ProgressBar)
    
    $ProgressBar.Value = 20
    $StatusLabel.Text = "Removing AXIS files..."
    $StatusLabel.Refresh()
    
    # Remove installation directory
    if (Test-Path $INSTALL_DIR) {
        Remove-Item -Recurse -Force $INSTALL_DIR
    }
    
    $ProgressBar.Value = 50
    $StatusLabel.Text = "Removing from PATH..."
    $StatusLabel.Refresh()
    
    # Remove from PATH
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $newPath = ($userPath -split ';' | Where-Object { $_ -ne $BIN_DIR }) -join ';'
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    
    $ProgressBar.Value = 80
    $StatusLabel.Text = "Uninstalling VS Code extension..."
    $StatusLabel.Refresh()
    
    # Try to uninstall VS Code extension
    try {
        $code = Get-Command code -ErrorAction SilentlyContinue
        if ($code) {
            & code --uninstall-extension AGDNoob.axis-lang 2>&1 | Out-Null
        }
    } catch {}
    
    $ProgressBar.Value = 100
    return $true
}

# ============================================================================
# GUI SETUP
# ============================================================================

$form = New-Object System.Windows.Forms.Form
$form.Text = "AXIS Language Installer"
$form.Size = New-Object System.Drawing.Size(500, 400)
$form.StartPosition = "CenterScreen"
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox = $false
$form.BackColor = [System.Drawing.Color]::FromArgb(30, 30, 30)
$form.ForeColor = [System.Drawing.Color]::White
$form.Font = New-Object System.Drawing.Font("Segoe UI", 10)

# Title
$titleLabel = New-Object System.Windows.Forms.Label
$titleLabel.Text = "AXIS Language Installer"
$titleLabel.Font = New-Object System.Drawing.Font("Segoe UI", 18, [System.Drawing.FontStyle]::Bold)
$titleLabel.Location = New-Object System.Drawing.Point(20, 20)
$titleLabel.Size = New-Object System.Drawing.Size(450, 40)
$titleLabel.ForeColor = [System.Drawing.Color]::FromArgb(0, 150, 255)
$form.Controls.Add($titleLabel)

# Version
$versionLabel = New-Object System.Windows.Forms.Label
$versionLabel.Text = "Version $AXIS_VERSION"
$versionLabel.Location = New-Object System.Drawing.Point(20, 60)
$versionLabel.Size = New-Object System.Drawing.Size(200, 25)
$versionLabel.ForeColor = [System.Drawing.Color]::Gray
$form.Controls.Add($versionLabel)

# Python Status
$pythonLabel = New-Object System.Windows.Forms.Label
$pythonLabel.Location = New-Object System.Drawing.Point(20, 100)
$pythonLabel.Size = New-Object System.Drawing.Size(450, 25)
$form.Controls.Add($pythonLabel)

# Check Python
$pythonInfo = Get-PythonVersion
if ($pythonInfo -and $pythonInfo.Version -ge $MIN_PYTHON_VERSION) {
    $pythonLabel.Text = "[OK] Python $($pythonInfo.Version) found"
    $pythonLabel.ForeColor = [System.Drawing.Color]::LightGreen
    $pythonReady = $true
    $pythonPath = $pythonInfo.Path
} else {
    $pythonLabel.Text = "[X] Python 3.7+ required (select version below)"
    $pythonLabel.ForeColor = [System.Drawing.Color]::Orange
    $pythonReady = $false
    $pythonPath = "python"
}

# Python Version Selector (only shown when Python not installed)
$pythonVersionLabel = New-Object System.Windows.Forms.Label
$pythonVersionLabel.Text = "Python version to install:"
$pythonVersionLabel.Location = New-Object System.Drawing.Point(20, 130)
$pythonVersionLabel.Size = New-Object System.Drawing.Size(180, 25)
$pythonVersionLabel.ForeColor = [System.Drawing.Color]::LightGray
$form.Controls.Add($pythonVersionLabel)

$pythonVersionCombo = New-Object System.Windows.Forms.ComboBox
$pythonVersionCombo.Location = New-Object System.Drawing.Point(200, 127)
$pythonVersionCombo.Size = New-Object System.Drawing.Size(260, 25)
$pythonVersionCombo.DropDownStyle = "DropDownList"
$pythonVersionCombo.BackColor = [System.Drawing.Color]::FromArgb(50, 50, 50)
$pythonVersionCombo.ForeColor = [System.Drawing.Color]::White
$pythonVersionCombo.FlatStyle = "Flat"
foreach ($pv in $PYTHON_VERSIONS) {
    $pythonVersionCombo.Items.Add($pv.Label) | Out-Null
}
$pythonVersionCombo.SelectedIndex = 1  # Default to "Recommended" (3.12)
$form.Controls.Add($pythonVersionCombo)

# Hide version selector if Python is already installed
if ($pythonReady) {
    $pythonVersionLabel.Visible = $false
    $pythonVersionCombo.Visible = $false
}

# Install Location
$locationLabel = New-Object System.Windows.Forms.Label
$locationLabel.Text = "Install to: $INSTALL_DIR"
$locationLabel.Location = New-Object System.Drawing.Point(20, 165)
$locationLabel.Size = New-Object System.Drawing.Size(450, 25)
$locationLabel.ForeColor = [System.Drawing.Color]::LightGray
$form.Controls.Add($locationLabel)

# VS Code Extension Checkbox
$vscodeCheckbox = New-Object System.Windows.Forms.CheckBox
$vscodeCheckbox.Text = "Install VS Code Extension (syntax highlighting)"
$vscodeCheckbox.Location = New-Object System.Drawing.Point(20, 195)
$vscodeCheckbox.Size = New-Object System.Drawing.Size(400, 25)
$vscodeCheckbox.Checked = $true
$vscodeCheckbox.ForeColor = [System.Drawing.Color]::White
$vscodeCheckbox.FlatStyle = "Flat"
$form.Controls.Add($vscodeCheckbox)

# Progress Bar
$progressBar = New-Object System.Windows.Forms.ProgressBar
$progressBar.Location = New-Object System.Drawing.Point(20, 240)
$progressBar.Size = New-Object System.Drawing.Size(440, 25)
$progressBar.Style = "Continuous"
$form.Controls.Add($progressBar)

# Status Label
$statusLabel = New-Object System.Windows.Forms.Label
$statusLabel.Text = "Ready to install"
$statusLabel.Location = New-Object System.Drawing.Point(20, 270)
$statusLabel.Size = New-Object System.Drawing.Size(450, 25)
$statusLabel.ForeColor = [System.Drawing.Color]::LightGray
$form.Controls.Add($statusLabel)

# Install Button
$installButton = New-Object System.Windows.Forms.Button
$installButton.Text = "Install AXIS"
$installButton.Location = New-Object System.Drawing.Point(50, 310)
$installButton.Size = New-Object System.Drawing.Size(180, 40)
$installButton.BackColor = [System.Drawing.Color]::FromArgb(0, 120, 215)
$installButton.ForeColor = [System.Drawing.Color]::White
$installButton.FlatStyle = "Flat"
$installButton.Font = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)
$form.Controls.Add($installButton)

# Uninstall Button
$uninstallButton = New-Object System.Windows.Forms.Button
$uninstallButton.Text = "Uninstall"
$uninstallButton.Location = New-Object System.Drawing.Point(250, 310)
$uninstallButton.Size = New-Object System.Drawing.Size(180, 40)
$uninstallButton.BackColor = [System.Drawing.Color]::FromArgb(150, 50, 50)
$uninstallButton.ForeColor = [System.Drawing.Color]::White
$uninstallButton.FlatStyle = "Flat"
$uninstallButton.Font = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)
# Only enable if AXIS is installed
if (-not (Test-Path $INSTALL_DIR)) {
    $uninstallButton.Enabled = $false
    $uninstallButton.BackColor = [System.Drawing.Color]::FromArgb(80, 80, 80)
}
$form.Controls.Add($uninstallButton)

# ============================================================================
# INSTALL LOGIC
# ============================================================================

$installButton.Add_Click({
    $installButton.Enabled = $false
    $vscodeCheckbox.Enabled = $false
    $pythonVersionCombo.Enabled = $false
    
    try {
        # Step 1: Install Python if needed
        if (-not $pythonReady) {
            $progressBar.Value = 10
            # Get selected version
            $selectedIndex = $pythonVersionCombo.SelectedIndex
            $selectedVersion = $PYTHON_VERSIONS[$selectedIndex].Version
            
            if (-not (Install-Python -StatusLabel $statusLabel -SelectedVersion $selectedVersion)) {
                $statusLabel.Text = "Failed to install Python"
                $statusLabel.ForeColor = [System.Drawing.Color]::Red
                $installButton.Enabled = $true
                return
            }
            $pythonLabel.Text = "[OK] Python $selectedVersion installed"
            $pythonLabel.ForeColor = [System.Drawing.Color]::LightGreen
            $script:pythonPath = "python"
        }
        
        # Step 2: Download AXIS files
        $progressBar.Value = 30
        if (-not (Get-AxisFiles -StatusLabel $statusLabel -ProgressBar $progressBar)) {
            $statusLabel.Text = "Failed to download AXIS files"
            $statusLabel.ForeColor = [System.Drawing.Color]::Red
            $installButton.Enabled = $true
            return
        }
        
        # Step 3: Install Python dependencies (keystone-engine)
        $progressBar.Value = 55
        if (-not (Install-Dependencies -StatusLabel $statusLabel)) {
            # Not fatal - legacy assembler will be used
            $statusLabel.Text = "Note: Dependencies install failed, using fallback assembler"
            $statusLabel.ForeColor = [System.Drawing.Color]::Yellow
        }
        
        # Step 4: Create axis command
        $progressBar.Value = 70
        if (-not (New-AxisCommand -StatusLabel $statusLabel -PythonPath $pythonPath)) {
            $statusLabel.Text = "Failed to create axis command"
            $statusLabel.ForeColor = [System.Drawing.Color]::Red
            $installButton.Enabled = $true
            return
        }
        
        # Step 5: Add to PATH
        $progressBar.Value = 80
        if (-not (Add-ToPath -StatusLabel $statusLabel)) {
            $statusLabel.Text = "Failed to add to PATH"
            $statusLabel.ForeColor = [System.Drawing.Color]::Red
            $installButton.Enabled = $true
            return
        }
        
        # Step 6: Install VS Code extension (optional)
        if ($vscodeCheckbox.Checked) {
            $progressBar.Value = 90
            Install-VSCodeExtension -StatusLabel $statusLabel | Out-Null
        }
        
        # Done!
        $progressBar.Value = 100
        $statusLabel.Text = "Installation complete! Restart your terminal to use 'axis'"
        $statusLabel.ForeColor = [System.Drawing.Color]::LightGreen
        
        $installButton.Text = "Done!"
        $installButton.BackColor = [System.Drawing.Color]::FromArgb(0, 150, 0)
        
    } catch {
        $statusLabel.Text = "Error: $_"
        $statusLabel.ForeColor = [System.Drawing.Color]::Red
        $installButton.Enabled = $true
    }
})

# Uninstall button click handler
$uninstallButton.Add_Click({
    $result = [System.Windows.Forms.MessageBox]::Show(
        "Are you sure you want to uninstall AXIS?",
        "Confirm Uninstall",
        [System.Windows.Forms.MessageBoxButtons]::YesNo,
        [System.Windows.Forms.MessageBoxIcon]::Question
    )
    
    if ($result -eq [System.Windows.Forms.DialogResult]::Yes) {
        $installButton.Enabled = $false
        $uninstallButton.Enabled = $false
        $vscodeCheckbox.Enabled = $false
        
        if (Uninstall-AXIS -StatusLabel $statusLabel -ProgressBar $progressBar) {
            $statusLabel.Text = "AXIS has been uninstalled. Restart your terminal."
            $statusLabel.ForeColor = [System.Drawing.Color]::LightGreen
            $uninstallButton.Text = "Done!"
            $uninstallButton.BackColor = [System.Drawing.Color]::FromArgb(0, 150, 0)
        } else {
            $statusLabel.Text = "Uninstall failed"
            $statusLabel.ForeColor = [System.Drawing.Color]::Red
            $uninstallButton.Enabled = $true
        }
    }
})

# Show form
[void]$form.ShowDialog()
