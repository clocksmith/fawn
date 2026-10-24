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
if not "%errorlevel%"=="0" exit /b 1
pushd "%~dp0"
if not "%errorlevel%"=="0" exit /b 1
set "BASE=/nologo /MD /O2 /WX /clang:-Wall /clang:-Wextra /clang:-Wpedantic /Iinclude"
"%CLANG_CL%" %BASE% /std:c++17 /clang:-fno-exceptions /clang:-fno-rtti /fp:strict /imsvc"%DXC_ROOT%\inc" /c src/windows_shader.cpp /Fo"%OUT%\windows_shader.obj"
if not "%errorlevel%"=="0" goto fail
"%CLANG_CL%" %BASE% /clang:-std=c11 /fp:strict /D_CRT_SECURE_NO_WARNINGS /c tests/native_shader.c /Fo"%OUT%\native_shader.obj"
if not "%errorlevel%"=="0" goto fail
"%CLANG_CL%" /nologo /MD "%OUT%\native_shader.obj" "%OUT%\windows_shader.obj" /Fe"%OUT%\native_shader.exe"
if not "%errorlevel%"=="0" goto fail
rem Framework methods avoid inherited PowerShell module-path dependencies.
powershell.exe -NoProfile -Command "$ErrorActionPreference='Stop'; $p=[IO.Path]::Combine($env:DXC_ROOT,'bin/x64/dxcompiler.dll'); $v=[Diagnostics.FileVersionInfo]::GetVersionInfo($p); 'FileVersion : '+$v.FileVersion; 'ProductVersion : '+$v.ProductVersion; 'FileName : '+$v.FileName; foreach ($file in (@($p,[IO.Path]::Combine($env:DXC_ROOT,'inc/dxcapi.h'))+[IO.Directory]::GetFiles([IO.Path]::GetFullPath('../../compiler/include'),'*.h'))) { $algorithm=[Security.Cryptography.SHA256]::Create(); try { $stream=[IO.File]::OpenRead($file); try { $bytes=$algorithm.ComputeHash($stream) } finally { $stream.Dispose() } } finally { $algorithm.Dispose() }; 'SHA256 '+[BitConverter]::ToString($bytes).Replace('-','').ToLowerInvariant()+'  '+$file }"
if not "%errorlevel%"=="0" goto fail
> "%OUT%\negative-include.hlsl" echo #define DOE_INCLUDE_VALUE 7
"%OUT%\native_shader.exe" "%DXC_ROOT%\bin\x64\dxcompiler.dll" "%OUT%\negative-include.hlsl"
if not "%errorlevel%"=="0" goto fail
rem Additive typed VS projection uses the same official DXC/host-state owner.
"%CLANG_CL%" %BASE% /clang:-std=c11 /fp:strict /D_CRT_SECURE_NO_WARNINGS /c tests/native_shader_projection.c /Fo"%OUT%\native_shader_projection.obj"
if not "%errorlevel%"=="0" goto fail
"%CLANG_CL%" /nologo /MD "%OUT%\native_shader_projection.obj" "%OUT%\windows_shader.obj" /Fe"%OUT%\native_shader_projection.exe"
if not "%errorlevel%"=="0" goto fail
"%OUT%\native_shader_projection.exe" "%DXC_ROOT%\bin\x64\dxcompiler.dll"
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
