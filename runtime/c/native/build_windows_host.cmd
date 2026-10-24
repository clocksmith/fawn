@echo off
setlocal
rem Actual Windows SDK / clang-cl CPU host controls, no GPU or native provider.
rem OUT is a dedicated evidence directory outside the source checkout.
if "%OUT%"=="" exit /b 1
if not defined CLANG_CL set "CLANG_CL=clang-cl"
if not exist "%OUT%" mkdir "%OUT%"
if not "%errorlevel%"=="0" exit /b 1
pushd "%~dp0"
if not "%errorlevel%"=="0" exit /b 1
set "BASE=/nologo /MD /O2 /WX /clang:-Wall /clang:-Wextra /clang:-Wpedantic /I../platform/include"
set "STRICT=%BASE% /clang:-std=c11"
"%CLANG_CL%" %STRICT% /c tests/provider_pin_fixture.c /Fo"%OUT%\provider_pin_fixture.obj"
if not "%errorlevel%"=="0" goto fail
"%CLANG_CL%" %STRICT% /c src/provider_pin.c /Fo"%OUT%\provider_pin.obj"
if not "%errorlevel%"=="0" goto fail
"%CLANG_CL%" /nologo /LD "%OUT%\provider_pin_fixture.obj" "%OUT%\provider_pin.obj" /Fe"%OUT%\provider_pin_fixture.dll" /link /implib:"%OUT%\provider_pin_fixture.lib"
if not "%errorlevel%"=="0" goto fail
"%CLANG_CL%" %STRICT% /c tests/provider_pin.c /Fo"%OUT%\provider_pin_control.obj"
if not "%errorlevel%"=="0" goto fail
"%CLANG_CL%" /nologo "%OUT%\provider_pin_control.obj" "%OUT%\provider_pin.obj" /Fe"%OUT%\provider_pin.exe" /link /stack:8388608
if not "%errorlevel%"=="0" goto fail
"%CLANG_CL%" %STRICT% tests/provider_pin_win32_failure.c /Fo"%OUT%\provider_pin_failure.obj" /Fe"%OUT%\provider_pin_failure.exe"
if not "%errorlevel%"=="0" goto fail
"%CLANG_CL%" %STRICT% ../platform/tests/test_primitives.c /Fo"%OUT%\test_primitives.obj" /Fe"%OUT%\test_primitives.exe"
if not "%errorlevel%"=="0" goto fail
"%CLANG_CL%" %BASE% /std:c++17 /clang:-fno-exceptions /clang:-fno-rtti ../platform/tests/test_cpp.cpp /Fo"%OUT%\test_cpp.obj" /Fe"%OUT%\test_cpp.exe"
if not "%errorlevel%"=="0" goto fail
"%OUT%\test_primitives.exe"
if not "%errorlevel%"=="0" goto fail
"%OUT%\test_cpp.exe"
if not "%errorlevel%"=="0" goto fail
"%OUT%\provider_pin_failure.exe"
if not "%errorlevel%"=="0" goto fail
"%OUT%\provider_pin.exe" "%OUT%\provider_pin_fixture.dll"
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
