@setlocal

md "%~dp0build\"

call "%VS150COMNTOOLS%VsDevCmd.bat"
@if %errorlevel% neq 0 goto errorexit

cd "%WXWIN_VS2017%\build\msw\"

msbuild /m /p:Configuration="DLL Release",Platform=x64 /t:Build /consoleLoggerParameters:Summary /verbosity:minimal /fileLogger /fileLoggerParameters:Summary;Append;Verbosity=normal;LogFile=%~dp0build\wx_x64_dll_2017.log wx_vc15.sln
@if %errorlevel% neq 0 goto errorexit

msbuild /m /p:Configuration="DLL Release",Platform=Win32 /t:Build /consoleLoggerParameters:Summary /verbosity:minimal /fileLogger /fileLoggerParameters:Summary;Append;Verbosity=normal;LogFile=%~dp0build\wx_win32_dll_2017.log wx_vc15.sln
@if %errorlevel% neq 0 goto errorexit

msbuild /m /p:Configuration="Release",Platform=x64 /t:Build /consoleLoggerParameters:Summary /verbosity:minimal /fileLogger /fileLoggerParameters:Summary;Append;Verbosity=normal;LogFile=%~dp0build\wx_x64_2017.log wx_vc15.sln
@if %errorlevel% neq 0 goto errorexit

msbuild /m /p:Configuration="Release",Platform=Win32 /t:Build /consoleLoggerParameters:Summary /verbosity:minimal /fileLogger /fileLoggerParameters:Summary;Append;Verbosity=normal;LogFile=%~dp0build\wx_win32_2017.log wx_vc15.sln
@if %errorlevel% neq 0 goto errorexit

msbuild /m /p:Configuration="DLL Debug",Platform=x64 /t:Build /consoleLoggerParameters:Summary /verbosity:minimal /fileLogger /fileLoggerParameters:Summary;Append;Verbosity=normal;LogFile=%~dp0build\wxd_x64_dll_2017.log wx_vc15.sln
@if %errorlevel% neq 0 goto errorexit

msbuild /m /p:Configuration="DLL Debug",Platform=Win32 /t:Build /consoleLoggerParameters:Summary /verbosity:minimal /fileLogger /fileLoggerParameters:Summary;Append;Verbosity=normal;LogFile=%~dp0build\wxd_win32_dll_2017.log wx_vc15.sln
@if %errorlevel% neq 0 goto errorexit

msbuild /m /p:Configuration="Debug",Platform=x64 /t:Build /consoleLoggerParameters:Summary /verbosity:minimal /fileLogger /fileLoggerParameters:Summary;Append;Verbosity=normal;LogFile=%~dp0build\wxd_x64_2017.log wx_vc15.sln
@if %errorlevel% neq 0 goto errorexit

msbuild /m /p:Configuration="Debug",Platform=Win32 /t:Build /consoleLoggerParameters:Summary /verbosity:minimal /fileLogger /fileLoggerParameters:Summary;Append;Verbosity=normal;LogFile=%~dp0build\wxd_win32_2017.log wx_vc15.sln
@if %errorlevel% neq 0 goto errorexit

@endlocal
@exit /b 0

:errorexit
@endlocal
@exit /b 1
