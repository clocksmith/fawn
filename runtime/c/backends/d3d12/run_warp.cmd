@echo off
setlocal
rem Explicit hosted CPU WARP control only; call after build_windows.cmd.
rem Root workflow owns the outer process-tree timeout and tool identity capture.
if "%OUT%"=="" (
    echo Set OUT to the existing SDK build directory >&2
    exit /b 2
)
if "%WARP_EVIDENCE%"=="" (
    echo Set WARP_EVIDENCE to a dedicated evidence directory >&2
    exit /b 2
)
if "%WindowsSdkVerBinPath%"=="" (
    echo WindowsSdkVerBinPath is missing; call VsDevCmd first >&2
    exit /b 2
)
if not exist "%OUT%\native_compute.exe" (
    echo Missing built client: "%OUT%\native_compute.exe" >&2
    exit /b 2
)
if not exist "%WindowsSdkVerBinPath%x64\fxc.exe" (
    echo Missing exact SDK compiler: "%WindowsSdkVerBinPath%x64\fxc.exe" >&2
    exit /b 2
)
if not exist "%WARP_EVIDENCE%\" (
    mkdir "%WARP_EVIDENCE%"
    if errorlevel 1 exit /b 1
)
if exist "%WARP_EVIDENCE%\shader.dxbc" exit /b 2
if exist "%WARP_EVIDENCE%\shader-build.log" exit /b 2
if exist "%WARP_EVIDENCE%\compute.log" exit /b 2
if exist "%WARP_EVIDENCE%\fxc-path.txt" exit /b 2
for %%F in ("%WindowsSdkVerBinPath%x64\fxc.exe") do set "WARP_FXC=%%~fF"
>"%WARP_EVIDENCE%\fxc-path.txt" echo %WARP_FXC%
if not exist "%WARP_EVIDENCE%\fxc-path.txt" exit /b 1
"%WARP_FXC%" /nologo /Ges /WX /T cs_5_1 /E main /Fo "%WARP_EVIDENCE%\shader.dxbc" tests\native_compute.hlsl >"%WARP_EVIDENCE%\shader-build.log" 2>&1
set "WARP_SHADER_STATUS=%ERRORLEVEL%"
type "%WARP_EVIDENCE%\shader-build.log"
if not "%WARP_SHADER_STATUS%"=="0" exit /b %WARP_SHADER_STATUS%
"%OUT%\native_compute.exe" "%WARP_EVIDENCE%\shader.dxbc" --warp >"%WARP_EVIDENCE%\compute.log" 2>&1
set "WARP_RUN_STATUS=%ERRORLEVEL%"
type "%WARP_EVIDENCE%\compute.log"
exit /b %WARP_RUN_STATUS%
