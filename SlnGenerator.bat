@echo off
setlocal
if /I "%~1"=="--no-pause" (
    call "%~dp0Build.bat" -NoPause
) else (
    call "%~dp0Build.bat" %*
)
exit /b %ERRORLEVEL%
