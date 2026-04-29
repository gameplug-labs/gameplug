@echo off
setlocal

:: ========================================
:: CONFIGURATION: Set your game EXE here
:: Example: set GAME_EXE=C:\Games\Doom\Doom.exe
:: ========================================
set GAME_EXE=

:: ========================================
:: GamePlug Pro Launcher
:: ========================================
echo ========================================
echo GamePlug Pro Launcher
echo ========================================

:: If GAME_EXE is not set, use the first argument
if "%GAME_EXE%"=="" (
    set "GAME_EXE=%~1"
    shift
)

if "%GAME_EXE%"=="" (
    echo [ERROR] No game executable provided.
    echo.
    echo Usage: 
    echo   1. Edit this .bat file and set GAME_EXE=your_game.exe
    echo   2. Drag and drop your game .exe onto this file
    echo   3. Run from terminal: run_game.bat "C:\Path\To\Game.exe"
    echo.
    pause
    exit /b 1
)

echo [GamePlug] Preparing to launch: %GAME_EXE%
echo [GamePlug] Arguments: %*

"%~dp0GamePlug.exe" "%GAME_EXE%" %*

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] GamePlug.exe exited with code %ERRORLEVEL%
    pause
)
