@echo off
chcp 65001 >nul
setlocal EnableExtensions

rem Usage (windeployqt-style, Release /MD):
rem   ReleaseVcDeploy.bat --dir NDI .\build\bin\Release\NDIReceiver.exe
rem   windeployqt --dir NDI .\NDIReceiver.exe
rem   ReleaseVcDeploy.bat --dir NDI .\NDISender.exe .\NDIReceiver.exe

if /i "%~1"=="--help" goto :show_help
if /i "%~1"=="-h" goto :show_help
if /i "%~1"=="/?" goto :show_help

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0ReleaseVcDeploy.ps1" %*
set "ERR=%ERRORLEVEL%"
exit /b %ERR%

:show_help
set "RELEASE_VCDEPLOY_SHOW_HELP=1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0ReleaseVcDeploy.ps1"
set "RELEASE_VCDEPLOY_SHOW_HELP="
exit /b 0
