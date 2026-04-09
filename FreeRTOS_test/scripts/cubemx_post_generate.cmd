@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%cubemx_post_generate.ps1"
set "EXIT_CODE=%ERRORLEVEL%"

if not "%EXIT_CODE%"=="0" (
    echo CubeMX post-generate script failed with exit code %EXIT_CODE%.
)

exit /b %EXIT_CODE%
