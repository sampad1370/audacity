@setlocal

@if exist "%VS150COMNTOOLS%VsDevCmd.bat" goto skipwhere

@set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

@if not exist %VSWHERE% set VSWHERE="%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

@if not exist %VSWHERE% goto errorexit

@set pre=Microsoft.VisualStudio.Product.
@set ids=%pre%Community %pre%Professional %pre%Enterprise %pre%BuildTools

@for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products %ids% -requires Microsoft.Component.MSBuild -requires Microsoft.VisualStudio.Workload.NativeDesktop -property installationPath`) do @(
  @set VS150COMNTOOLS=%%i\Common7\Tools\
)

@if not exist "%VS150COMNTOOLS%VsDevCmd.bat" goto errorexit

:skipwhere

md "%~dp0build\"

@setlocal

call "%VS150COMNTOOLS%VsDevCmd.bat"
@if %errorlevel% neq 0 goto errorexit

call :dobuild Win32 v141_xp
@if %errorlevel% neq 0 goto errorexit

@endlocal
@setlocal

call "%VS150COMNTOOLS%VsDevCmd.bat" -arch=x64 -host_arch=x64
@if %errorlevel% neq 0 goto errorexit

call :dobuild x64 v141
@if %errorlevel% neq 0 goto errorexit

@endlocal
@exit /b 0

:dobuild

cd "%~dp0"
@if %errorlevel% neq 0 goto errorexit

call :run_msbuild "Debug" %1 %2
@if %errorlevel% neq 0 goto errorexit

call :run_msbuild "Static Debug" %1 %2
@if %errorlevel% neq 0 goto errorexit

call :run_msbuild "Release" %1 %2
@if %errorlevel% neq 0 goto errorexit

call :run_msbuild "Static Release" %1 %2
@if %errorlevel% neq 0 goto errorexit

@endlocal
@exit /b 0

:run_msbuild

@echo +++++++++++++++++++++++++ %~1 %~2 %~3
msbuild /m "/p:Configuration=%~1;Platform=%~2;PlatformToolset=%~3;wxCompilerPrefix=%~3%~4" /t:Build /consoleLoggerParameters:Summary;PerformanceSummary /verbosity:minimal /fileLogger "/fileLoggerParameters:Summary;PerformanceSummary;Append;Verbosity=normal;LogFile=%~dp0build\audacity_%~1_%~2_%~3.log" audacity.sln
@if %errorlevel% neq 0 goto errorexit
@echo ------------------------- %~1 %~2 %~3

@exit /b 0

:errorexit
@endlocal
@echo ***** BUILD FAILED ***** %0 %*
@exit /b 1
