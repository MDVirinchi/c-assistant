@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
title C Assistant Native — Build
color 0A

echo.
echo  ============================================
echo   C Assistant Native — Build
echo  ============================================
echo.

REM ── Find GCC — bundled first, then system ────────────────────
set GCC=

if exist tools\w64devkit\bin\gcc.exe (
    set GCC=tools\w64devkit\bin\gcc.exe
    REM ── Add bundled bin to PATH so gcc can find as.exe, ld.exe etc.
    set PATH=%~dp0tools\w64devkit\bin;!PATH!
    echo  Using bundled compiler: tools\w64devkit\bin\gcc.exe
) else (
    where gcc >nul 2>&1
    if not errorlevel 1 (
        set GCC=gcc
        echo  Using system compiler: gcc
    )
)

REM ── No compiler found → offer to download ────────────────────
if "!GCC!"=="" (
    echo  [!] No compiler found.
    echo.
    echo  Options:
    echo    1. Run SETUP_COMPILER.bat to download it automatically
    echo    2. Install MinGW manually from https://www.mingw-w64.org
    echo.
    set /p CHOICE="  Download compiler now? (Y/N): "
    if /i "!CHOICE!"=="Y" (
        call SETUP_COMPILER.bat
        if exist tools\w64devkit\bin\gcc.exe (
            set GCC=tools\w64devkit\bin\gcc.exe
        ) else (
            echo  Setup failed. Cannot build.
            pause
            exit /b 1
        )
    ) else (
        pause
        exit /b 1
    )
)

echo.

REM ── Create output folder ─────────────────────────────────────
if not exist dist mkdir dist

REM ── Compile all source files ──────────────────────────────────
echo  Compiling...
echo.

!GCC! src\main.c src\popup.c src\groq.c src\config.c ^
    -I include ^
    -o dist\c-assistant.exe ^
    -mwindows ^
    -lwinhttp ^
    -lshell32 ^
    -lgdi32 ^
    -luser32 ^
    -O2 ^
    -Wall

if errorlevel 1 (
    echo.
    echo  [ERROR] Build failed. See errors above.
    pause
    exit /b 1
)

echo  [OK] dist\c-assistant.exe built successfully.
echo.

REM ── Copy .env ─────────────────────────────────────────────────
if exist .env (
    copy /y .env dist\.env >nul
    echo  [OK] .env copied to dist\
) else if not exist dist\.env (
    echo GROQ_API_KEY=your_groq_api_key_here > dist\.env
    echo  [!!] Add your Groq API key to dist\.env
    echo       Get a free key: https://console.groq.com/keys
)

echo.
echo  ============================================
echo   Done. Run START.bat to launch.
echo  ============================================
echo.
pause
