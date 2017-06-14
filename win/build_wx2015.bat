@setlocal

@if exist "%~dp0build\" goto skipmd

md "%~dp0build\"
@if %errorlevel% neq 0 goto errorexit

:skipmd

@if NOT "%WXWIN_VS2015%"=="" (
   set WXWIN=%WXWIN_VS2015%
)

@if "%WXWIN%"=="" (
   set WXWIN=%~dp0..\wxWidgets
)

@if exist "%WXWIN%\include\wx\msw\setup.h" goto skipcopy

copy "%WXWIN%\include\wx\msw\setup0.h" "%WXWIN%\include\wx\msw\setup.h"
@if %errorlevel% neq 0 goto errorexit

:skipcopy

@setlocal

call "%VS140COMNTOOLS%VsDevCmd.bat"
@if %errorlevel% neq 0 goto errorexit

call :dobuild Win32 v140_xp
@if %errorlevel% neq 0 goto errorexit

@endlocal
@setlocal

call "%VS140COMNTOOLS%VsDevCmd.bat" -arch=x64 -host_arch=x64
@if %errorlevel% neq 0 goto errorexit

call :dobuild x64 v140
@if %errorlevel% neq 0 goto errorexit

@endlocal
@exit /b 0

:dobuild

cd "%WXWIN%\build\msw\"
@if %errorlevel% neq 0 goto errorexit

call :run_msbuild Debug %1 %2
@if %errorlevel% neq 0 goto errorexit

call :run_msbuild "DLL Debug" %1 %2
@if %errorlevel% neq 0 goto errorexit

call :run_msbuild "Release" %1 %2 ";WholeProgramOptimization=true"
@if %errorlevel% neq 0 goto errorexit

call :run_msbuild "DLL Release" %1 %2 ";WholeProgramOptimization=true"
@if %errorlevel% neq 0 goto errorexit

@exit /b 0

:run_msbuild

@echo +++++++++++++++++++++++++ %~1 %~2 %~3
msbuild /m "/p:Configuration=%~1;Platform=%~2;PlatformToolset=%~3;wxCompilerPrefix=%~3%~4" /t:Build /consoleLoggerParameters:Summary /verbosity:minimal /fileLogger "/fileLoggerParameters:Summary;Append;Verbosity=normal;LogFile=%~dp0build\wx_%~1_%~2_%~3.log" wx_vc12.sln
@if %errorlevel% neq 0 goto errorexit
@echo ------------------------- %~1 %~2 %~3

@exit /b 0

:errorexit
@endlocal
@echo ***** BUILD FAILED ***** %0 %*
@exit /b 1
