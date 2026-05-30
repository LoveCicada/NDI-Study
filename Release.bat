@echo off
chcp 65001 >nul
title NDI-Study Release Build

call "%~dp0scripts\build_common.bat" Release
set "ERR=%ERRORLEVEL%"

echo.
if %ERR% neq 0 (
    echo Build failed with error code %ERR%.
) else (
    echo Release build completed.
)

pause
exit /b %ERR%
