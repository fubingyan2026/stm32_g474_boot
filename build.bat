@echo off
setlocal enabledelayedexpansion

REM ================================================
REM Windows STM32 Build Script
REM Usage: build.bat [project_dir] [-t Debug|Release]
REM ================================================

set "BUILD_TYPE=Release"
set "PROJ_DIR="

:parse
if "%~1"=="" goto :run
if "%~1"=="-t" set "BUILD_TYPE=%~2"& shift /1& shift /1& goto :parse
if "%~1"=="--type" set "BUILD_TYPE=%~2"& shift /1& shift /1& goto :parse
set "PROJ_DIR=%~f1"& shift /1& goto :parse

:run
if "%PROJ_DIR%"=="" set "PROJ_DIR=%CD%"
if not exist "%PROJ_DIR%\CMakeLists.txt" (
    echo [ERROR] No CMakeLists.txt found in %PROJ_DIR%
    exit /b 1
)
cd /d "%PROJ_DIR%" || exit /b 1

echo [INFO] Project: %PROJ_DIR%
echo [INFO] Build type: %BUILD_TYPE%

REM --- find ARM toolchain ---
set "TC="
if exist "build\%BUILD_TYPE%\CMakeCache.txt" (
    for /f "usebackq tokens=2 delims==" %%a in (`findstr "CMAKE_C_COMPILER:FILEPATH" "build\%BUILD_TYPE%\CMakeCache.txt" 2^>nul`) do (
        if exist "%%a" set "TC=%%~dpa"
    )
)
if not defined TC (
    for /d %%d in ("%LOCALAPPDATA%\stm32cube\bundles\gnu-tools-for-stm32\*") do (
        if exist "%%d\bin\arm-none-eabi-gcc.exe" set "TC=%%d\bin\"
    )
)
if not defined TC (
    echo [ERROR] arm-none-eabi-gcc.exe not found
    exit /b 1
)
set "PATH=%TC%;%PATH%"
echo [INFO] Toolchain: %TC%arm-none-eabi-gcc.exe

REM --- find cmake ---
set "CMAKE=cmake.exe"
where cmake.exe >nul 2>&1
if errorlevel 1 (
    set "CMAKE="
    for /d %%d in ("%LOCALAPPDATA%\stm32cube\bundles\cmake\*") do (
        if exist "%%d\bin\cmake.exe" set "CMAKE=%%d\bin\cmake.exe"
    )
)
if not defined CMAKE (
    echo [ERROR] cmake.exe not found
    exit /b 1
)
echo [INFO] CMake: !CMAKE!

REM --- find ninja ---
set "NINJA="
where ninja.exe >nul 2>&1
if errorlevel 1 (
    for /d %%d in ("%LOCALAPPDATA%\stm32cube\bundles\ninja\*") do (
        if exist "%%d\bin\ninja.exe" (
            set "NINJA=%%d\bin\ninja.exe"
            set "PATH=%%d\bin\;%PATH%"
        )
    )
) else (
    set "NINJA=ninja.exe"
)

set "BUILD_DIR=build\%BUILD_TYPE%"
if not exist "%BUILD_DIR%" (
    echo [INFO] Configuring...
    !CMAKE! -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE="%BUILD_TYPE%" -DCMAKE_TOOLCHAIN_FILE="cmake/gcc-arm-none-eabi.cmake" -GNinja
    if errorlevel 1 (
        echo [ERROR] CMake configure failed
        exit /b 1
    )
)

echo ========================================
echo [INFO] Building...
echo ========================================
ninja -C "%BUILD_DIR%"
if errorlevel 1 (
    echo [ERROR] Build failed
    exit /b 1
)

echo ========================================
echo [SUCCESS] Build OK
echo [INFO] Output: %CD%\%BUILD_DIR%
for %%f in ("%BUILD_DIR%\*.elf") do echo [INFO]   ELF: %%f
for %%f in ("%BUILD_DIR%\*.hex") do echo [INFO]   HEX: %%f
for %%f in ("%BUILD_DIR%\*.bin") do echo [INFO]   BIN: %%f
endlocal
