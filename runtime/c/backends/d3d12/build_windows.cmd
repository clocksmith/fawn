@echo off
setlocal
rem Run in a Windows SDK / MSVC developer command prompt. Builds, never runs.
rem OUT must be a dedicated build directory, not a source directory.
if "%OUT%"=="" set "OUT=%TEMP%\doe-c-d3d12"
if not exist "%OUT%" mkdir "%OUT%"
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /Iinclude /DDOE_DX_REQUIRE_WINDOWS /c src\context.c src\resources.c src\commands.c src\compute.c src\windows_context.c src\windows_resources.c src\windows_commands.c /Fo"%OUT%\\"
if errorlevel 1 exit /b 1
lib /nologo /OUT:"%OUT%\doe_d3d12.lib" "%OUT%\context.obj" "%OUT%\resources.obj" "%OUT%\commands.obj" "%OUT%\compute.obj" "%OUT%\windows_context.obj" "%OUT%\windows_resources.obj" "%OUT%\windows_commands.obj"
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /Iinclude tests\native_compute.c "%OUT%\doe_d3d12.lib" d3d12.lib dxgi.lib dxguid.lib /Fo"%OUT%\native_compute.obj" /Fe"%OUT%\native_compute.exe"
exit /b %errorlevel%
