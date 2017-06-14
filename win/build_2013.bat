@setlocal

md "%~dp0build\"

@setlocal

call "%VS120COMNTOOLS%VsDevCmd.bat"
@if %errorlevel% neq 0 goto errorexit

call :dobuild Win32 v120_xp
@if %errorlevel% neq 0 goto errorexit

@endlocal
@setlocal

call "%VS120COMNTOOLS%VsDevCmd.bat" -arch=x64 -host_arch=x64
@if %errorlevel% neq 0 goto errorexit

call :dobuild x64 v120
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
msbuild /m "/p:Configuration=%~1;Platform=%~2;PlatformToolset=%~3;wxCompilerPrefix=%~3%~4" /t:Build /consoleLoggerParameters:Summary /verbosity:minimal /fileLogger "/fileLoggerParameters:Summary;Append;Verbosity=normal;LogFile=%~dp0build\audacity_%~1_%~2_%~3.log" audacity.sln
@if %errorlevel% neq 0 goto errorexit
@echo ------------------------- %~1 %~2 %~3

@exit /b 0

:errorexit
@endlocal
@echo ***** BUILD FAILED ***** %0 %*
@exit /b 1
