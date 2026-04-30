@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
title C Assistant - Compiler Setup
color 0A

echo.
echo  ============================================
echo   C Assistant - Portable Compiler Setup
echo  ============================================
echo.

REM ── Already installed? ───────────────────────────────────────
if exist tools\w64devkit\bin\gcc.exe (
    echo  [OK] Compiler already installed in tools\w64devkit\
    echo.
    tools\w64devkit\bin\gcc.exe --version
    echo.
    echo  Run BUILD.bat to compile the app.
    pause
    exit /b 0
)

if not exist tools mkdir tools

echo  Finding download URL...
echo.

REM ── Use PowerShell to find the latest release that has a .zip ─
REM    (newer releases switched to .7z.exe — we want .zip for simplicity)
powershell -NoProfile -Command ^
  "$ErrorActionPreference = 'Stop';" ^
  "try {" ^
  "  $h = @{'User-Agent'='C-Assistant-Setup'};" ^
  "  $releases = Invoke-RestMethod -Uri 'https://api.github.com/repos/skeeto/w64devkit/releases?per_page=15' -Headers $h;" ^
  "  $found = $null;" ^
  "  foreach ($rel in $releases) {" ^
  "    $a = $rel.assets | Where-Object { $_.name -like 'w64devkit-*.zip' -and $_.name -notlike '*fortran*' -and $_.name -notlike '*i686*' };" ^
  "    if ($a) { $found = $a; break }" ^
  "  };" ^
  "  if (-not $found) { throw 'No .zip release found' };" ^
  "  $found.browser_download_url | Out-File -Encoding ASCII 'tools\_url.tmp';" ^
  "  Write-Host ('  Found: ' + $found.name);" ^
  "} catch { Write-Host ('  ERROR: ' + $_.Exception.Message); exit 1 }"

if errorlevel 1 (
    echo.
    echo  [ERROR] Could not find a download URL.
    echo  Check your internet connection and try again.
    pause
    exit /b 1
)

REM ── Read URL and download ─────────────────────────────────────
set /p DLURL=<tools\_url.tmp
del tools\_url.tmp

echo  Downloading (~65MB)... this may take a minute.
echo.

powershell -NoProfile -Command ^
  "$ErrorActionPreference = 'Stop';" ^
  "try {" ^
  "  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;" ^
  "  Invoke-WebRequest -Uri '%DLURL%' -OutFile 'tools\w64devkit.zip' -UseBasicParsing;" ^
  "  Write-Host '  Download complete.';" ^
  "} catch { Write-Host ('  ERROR: ' + $_.Exception.Message); exit 1 }"

if errorlevel 1 (
    if exist tools\w64devkit.zip del tools\w64devkit.zip
    echo.
    echo  [ERROR] Download failed. Check your internet connection.
    pause
    exit /b 1
)

REM ── Extract ───────────────────────────────────────────────────
echo  Extracting...

powershell -NoProfile -Command ^
  "$ErrorActionPreference = 'Stop';" ^
  "try {" ^
  "  Expand-Archive -Path 'tools\w64devkit.zip' -DestinationPath 'tools' -Force;" ^
  "  Write-Host '  Done.';" ^
  "} catch { Write-Host ('  ERROR: ' + $_.Exception.Message); exit 1 }"

del tools\w64devkit.zip 2>nul

REM ── Verify ────────────────────────────────────────────────────
if not exist tools\w64devkit\bin\gcc.exe (
    echo.
    echo  [ERROR] gcc.exe not found after extraction.
    echo  Contents of tools\:
    dir tools /b
    pause
    exit /b 1
)

echo.
echo  [OK] Compiler ready:
tools\w64devkit\bin\gcc.exe --version
echo.
echo  ============================================
echo   Done. Run BUILD.bat to compile the app.
echo  ============================================
echo.
pause
