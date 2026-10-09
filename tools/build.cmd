@echo off
setlocal
chcp 65001 >nul
set "VSLANG=1033"
cd /d "%~dp0.."
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "JLU_VS=%%i"
if not defined JLU_VS exit /b 1
call "%JLU_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PATH=%JLU_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%JLU_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
if "%1"=="core" goto core
set "JLU_QT=%CD%\.deps\Qt\6.8.3\msvc2022_64"
if defined QT_ROOT set "JLU_QT=%QT_ROOT%"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%JLU_QT%"
if errorlevel 1 exit /b 1
cmake --build build
if errorlevel 1 exit /b 1
pwsh -NoProfile -File "%CD%\tools\repair-release-integrity.ps1" -BuildOnly
if errorlevel 1 exit /b 1
if "%1"=="compile" exit /b 0
ctest --test-dir build --output-on-failure
if errorlevel 1 exit /b 1
exit /b 0
:core
cmake --fresh -S . -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Debug -DJLU_BUILD_UI=OFF "-DCMAKE_CXX_FLAGS_DEBUG=/Od /Ob0 /Z7 /RTC1"
if errorlevel 1 exit /b 1
cmake --build build-core
if errorlevel 1 exit /b 1
ctest --test-dir build-core --output-on-failure
if errorlevel 1 exit /b 1
exit /b 0
