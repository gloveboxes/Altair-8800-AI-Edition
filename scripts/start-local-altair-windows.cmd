@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"
pushd "%REPO_ROOT%"
if errorlevel 1 exit /b %errorlevel%

if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" (
    set "EXEPATH=altair_local\build-msvc-arm64\Release\altair-local.exe"
) else (
    set "EXEPATH=altair_local\build-msvc\Release\altair-local.exe"
)

if not exist "%EXEPATH%" (
    echo altair-local.exe was not found at "%EXEPATH%".
    echo.
    echo Build it first by running:
    echo   scripts\build-host-tools-windows.cmd
    popd
    exit /b 1
)

"%EXEPATH%"
set "APP_EXIT=%errorlevel%"
popd
exit /b %APP_EXIT%