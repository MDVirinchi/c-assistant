@echo off
cd /d "%~dp0"

if not exist dist\c-assistant.exe (
    echo  c-assistant.exe not found — building first...
    echo.
    call BUILD.bat
    if errorlevel 1 exit /b 1
)

if not exist dist\.env (
    echo  [!!] dist\.env is missing.
    echo       Create it with:  GROQ_API_KEY=your_key_here
    echo       Get a free key at: https://console.groq.com/keys
    pause
    exit /b 1
)

start "" "dist\c-assistant.exe"
