@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX tools\toolchain_probe.cpp /Fe:".audit-build\toolchain-probe.exe" /Fo:".audit-build\toolchain-probe.obj" /link ws2_32.lib iphlpapi.lib crypt32.lib bcrypt.lib
if errorlevel 1 exit /b 1
".audit-build\toolchain-probe.exe"
