@echo off
setlocal
rem Actual Microsoft SDK and pinned official portable DXC headers; CPU only.
rem DXC_ROOT is the extracted SHA256-verified1.9.2609.5 release, never PATH lookup.
if "%OUT%"=="" exit /b 1
if "%DXC_ROOT%"=="" exit /b 1
if not exist "%DXC_ROOT%\inc\dxcapi.h" exit /b 1
if not exist "%DXC_ROOT%\bin\x64\dxcompiler.dll" exit /b 1
if not defined CLANG_CL set "CLANG_CL=clang-cl"
if not exist "%OUT%" mkdir "%OUT%"
if errorlevel 1 exit /b 1
pushd "%~dp0"
if errorlevel 1 exit /b 1
set "BASE=/nologo /MD /O2 /WX /clang:-Wall /clang:-Wextra /clang:-Wpedantic /Iinclude"
"%CLANG_CL%" %BASE% /std:c++17 /clang:-fno-exceptions /clang:-fno-rtti /fp:strict /I"%DXC_ROOT%\inc" /c src/windows_shader.cpp /Fo"%OUT%\windows_shader.obj"
if errorlevel 1 goto fail
"%CLANG_CL%" %BASE% /clang:-std=c11 /fp:strict /D_CRT_SECURE_NO_WARNINGS /c tests/native_shader.c /Fo"%OUT%\native_shader.obj"
if errorlevel 1 goto fail
"%CLANG_CL%" /nologo /MD "%OUT%\native_shader.obj" "%OUT%\windows_shader.obj" /Fe"%OUT%\native_shader.exe"
if errorlevel 1 goto fail
powershell.exe -NoProfile -Command "$ErrorActionPreference='Stop'; $p=Join-Path $env:DXC_ROOT 'bin/x64/dxcompiler.dll'; (Get-Item $p).VersionInfo | Format-List FileVersion,ProductVersion,FileName; Get-FileHash -Algorithm SHA256 $p; Get-FileHash -Algorithm SHA256 (Join-Path $env:DXC_ROOT 'inc/dxcapi.h')"
if errorlevel 1 goto fail
> "%OUT%\negative-include.hlsl" echo #define DOE_INCLUDE_VALUE 7
"%OUT%\native_shader.exe" "%DXC_ROOT%\bin\x64\dxcompiler.dll" "%OUT%\negative-include.hlsl"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
