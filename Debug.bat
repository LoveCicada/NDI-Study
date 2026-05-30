@echo off
chcp 65001 >nul
title NDI-Study Debug Build

call "%~dp0scripts\build_common.bat" Debug
set "ERR=%ERRORLEVEL%"

echo.
if %ERR% neq 0 (
    echo Build failed with error code %ERR%.
) else (
    echo Debug build completed.
)

pause
exit /b %ERR%
