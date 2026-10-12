@echo off
setlocal
cd /d "%~dp0.."
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "BRAND_VS=%%i"
call "%BRAND_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "BRAND_QT=%CD%\.deps\Qt\6.8.3\msvc2022_64"
if defined QT_ROOT set "BRAND_QT=%QT_ROOT%"
cl /nologo /EHsc /std:c++20 /Zc:__cplusplus /utf-8 /MD /I"%BRAND_QT%\include" /I"%BRAND_QT%\include\QtCore" /I"%BRAND_QT%\include\QtGui" /I"%BRAND_QT%\include\QtSvg" tools\render-brand.cpp /Fo.deps\render-brand.obj /Fe.deps\render-brand.exe /link /LIBPATH:"%BRAND_QT%\lib" Qt6Core.lib Qt6Gui.lib Qt6Svg.lib
if errorlevel 1 exit /b 1
set "PATH=%BRAND_QT%\bin;%PATH%"
set "QT_QPA_PLATFORM=offscreen"
.deps\render-brand.exe
