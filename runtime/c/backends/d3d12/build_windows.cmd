@echo off
setlocal
rem Actual Windows SDK / MSVC developer command prompt; builds, never runs.
rem OUT must be a dedicated build directory, not a source directory.
if "%OUT%"=="" set "OUT=%TEMP%\doe-c-d3d12"
if not exist "%OUT%" mkdir "%OUT%"
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /Iinclude /DDOE_DX_REQUIRE_WINDOWS /c ^
    src\command_storage.c src\context.c src\resources.c src\commands.c ^
    src\compute.c src\pipeline.c src\textures.c ^
    src\texture_transfer.c src\bindings.c src\graphics.c ^
    src\timestamps.c src\windows_context.c src\windows_caps.c src\windows_adapter.c src\windows_resources.c ^
    src\windows_commands.c src\windows_pipeline.c src\windows_bindings.c ^
    src\windows_textures.c src\windows_graphics.c src\windows_timestamps.c ^
    src\vertex_layout.c src\geometry.c src\windows_geometry.c ^
    /Fo"%OUT%\\"
if errorlevel 1 exit /b 1
lib /nologo /OUT:"%OUT%\doe_d3d12.lib" ^
    "%OUT%\command_storage.obj" "%OUT%\context.obj" "%OUT%\resources.obj" "%OUT%\commands.obj" ^
    "%OUT%\compute.obj" "%OUT%\pipeline.obj" "%OUT%\textures.obj" ^
    "%OUT%\texture_transfer.obj" "%OUT%\bindings.obj" "%OUT%\graphics.obj" ^
    "%OUT%\timestamps.obj" "%OUT%\windows_context.obj" "%OUT%\windows_caps.obj" "%OUT%\windows_adapter.obj" "%OUT%\windows_resources.obj" ^
    "%OUT%\windows_commands.obj" "%OUT%\windows_pipeline.obj" "%OUT%\windows_bindings.obj" ^
    "%OUT%\windows_textures.obj" "%OUT%\windows_graphics.obj" "%OUT%\windows_timestamps.obj" ^
    "%OUT%\vertex_layout.obj" "%OUT%\geometry.obj" "%OUT%\windows_geometry.obj"
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /Iinclude ^
    tests\native_compute.c "%OUT%\doe_d3d12.lib" d3d12.lib dxgi.lib dxguid.lib ^
    /Fo"%OUT%\native_compute.obj" /Fe"%OUT%\native_compute.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /Iinclude ^
    tests\native_texture.c "%OUT%\doe_d3d12.lib" d3d12.lib dxgi.lib dxguid.lib ^
    /Fo"%OUT%\native_texture.obj" /Fe"%OUT%\native_texture.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /Iinclude ^
    tests\native_graphics.c "%OUT%\doe_d3d12.lib" d3d12.lib dxgi.lib dxguid.lib ^
    /Fo"%OUT%\native_graphics.obj" /Fe"%OUT%\native_graphics.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /Iinclude ^
    tests\native_mesh.c "%OUT%\doe_d3d12.lib" d3d12.lib dxgi.lib dxguid.lib ^
    /Fo"%OUT%\native_mesh.obj" /Fe"%OUT%\native_mesh.exe"
if errorlevel 1 exit /b 1
rem SDK-typed CPU call controls; root workflow runs this explicitly, never a GPU.
cl /nologo /std:c11 /W4 /WX /Iinclude ^
    tests\test_windows_caps.c "%OUT%\doe_d3d12.lib" d3d12.lib dxgi.lib dxguid.lib ^
    /Fo"%OUT%\test_windows_caps.obj" /Fe"%OUT%\test_windows_caps.exe"
if errorlevel 1 exit /b 1
exit /b 0
