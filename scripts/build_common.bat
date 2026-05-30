@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem 用法: build_common.bat <Debug|Release>
set "BUILD_CONFIG=%~1"
if "%BUILD_CONFIG%"=="" (
    echo Usage: build_common.bat Debug^|Release
    exit /b 1
)

set "PROJECT_ROOT=%~dp0.."
cd /d "%PROJECT_ROOT%"

rem ---- 用户本地配置（可选）----
if exist "%PROJECT_ROOT%\build_env.bat" (
    call "%PROJECT_ROOT%\build_env.bat"
)

rem ---- SDK 路径 ----
set "NDI_SDK_DIR=%PROJECT_ROOT%\third_party\NDI 6 Advanced SDK"
set "SDL2_DIR=%PROJECT_ROOT%\third_party\SDL2"

if not exist "%NDI_SDK_DIR%\Include\Processing.NDI.Lib.h" (
    echo [ERROR] NDI SDK not found: "%NDI_SDK_DIR%"
    exit /b 1
)
if not exist "%SDL2_DIR%\include\SDL.h" (
    echo [ERROR] SDL2 not found: "%SDL2_DIR%"
    exit /b 1
)

rem ---- Qt 路径 ----
rem build_env.bat 可设置 CMAKE_PREFIX_PATH；系统 QT_DIR 若指向 bin 目录也会自动转换
if not defined CMAKE_PREFIX_PATH (
    if defined QT_DIR (
        set "CMAKE_PREFIX_PATH=%QT_DIR%"
        if /i "!CMAKE_PREFIX_PATH:~-4!"=="\bin" set "CMAKE_PREFIX_PATH=!CMAKE_PREFIX_PATH:~0,-4!"
    )
)
if not defined CMAKE_PREFIX_PATH (
    if exist "D:\soft\qt5152\5.15.2\msvc2019_64\bin\qmake.exe" set "CMAKE_PREFIX_PATH=D:\soft\qt5152\5.15.2\msvc2019_64"
)
if not defined CMAKE_PREFIX_PATH (
    if exist "C:\Qt\5.15.2\msvc2019_64\bin\qmake.exe" set "CMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019_64"
)
if not defined CMAKE_PREFIX_PATH (
    echo [ERROR] Qt 5.15.2 not found. Set QT_DIR in system env or configure build_env.bat.
    exit /b 1
)

if not exist "%CMAKE_PREFIX_PATH%\bin\qmake.exe" (
    echo [ERROR] qmake not found: "%CMAKE_PREFIX_PATH%\bin\qmake.exe"
    exit /b 1
)

rem ---- CMake 可执行文件 ----
set "CMAKE_CMD=cmake"
if defined CMAKE_BIN set "PATH=%CMAKE_BIN%;%PATH%"

where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake not found in PATH. Install CMake or set CMAKE_BIN in build_env.bat.
    exit /b 1
)

rem ---- Visual Studio 生成器 ----
if not defined VS_GENERATOR (
    where "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe" >nul 2>&1
    if not errorlevel 1 set "VS_GENERATOR=Visual Studio 17 2022"

    if not defined VS_GENERATOR (
        where "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\devenv.exe" >nul 2>&1
        if not errorlevel 1 set "VS_GENERATOR=Visual Studio 17 2022"
    )
    if not defined VS_GENERATOR (
        where "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\devenv.exe" >nul 2>&1
        if not errorlevel 1 set "VS_GENERATOR=Visual Studio 16 2019"
    )
)
if not defined VS_GENERATOR set "VS_GENERATOR=Visual Studio 17 2022"

set "BUILD_DIR=%PROJECT_ROOT%\build"

echo.
echo ========================================
echo  NDI-Study Build - %BUILD_CONFIG%
echo ========================================
echo  NDI SDK : %NDI_SDK_DIR%
echo  SDL2    : %SDL2_DIR%
echo  Qt      : %CMAKE_PREFIX_PATH%
echo  VS Gen  : %VS_GENERATOR%
echo  Output  : %BUILD_DIR%\bin\%BUILD_CONFIG%
echo ========================================
echo.

echo [1/2] CMake configure...
cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" ^
    -G "%VS_GENERATOR%" -A x64 ^
    -DNDI_SDK_DIR="%NDI_SDK_DIR%" ^
    -DSDL2_DIR="%SDL2_DIR%" ^
    -DCMAKE_PREFIX_PATH="%CMAKE_PREFIX_PATH%"

if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

echo.
echo [2/2] CMake build (%BUILD_CONFIG%)...
cmake --build "%BUILD_DIR%" --config %BUILD_CONFIG% --parallel

if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo [OK] Build succeeded.
echo      NDISender  : %BUILD_DIR%\bin\%BUILD_CONFIG%\NDISender.exe
echo      NDIReceiver: %BUILD_DIR%\bin\%BUILD_CONFIG%\NDIReceiver.exe
echo.

endlocal
exit /b 0
