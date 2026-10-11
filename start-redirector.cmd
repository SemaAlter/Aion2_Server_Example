@echo off
rem Routes the client's lobby connection to the local server (WinDivert, needs Administrator).
rem Usage: start-redirector.cmd [server IPv4, default 127.0.0.1 for a server on this PC]
setlocal
net session >nul 2>&1
if not "%ERRORLEVEL%"=="0" (
    powershell.exe -NoProfile -Command "Start-Process -FilePath 'cmd.exe' -ArgumentList '/k','\"%~f0\" %~1' -Verb RunAs"
    exit /b
)
set "EXE=%~dp0bin\x64\Release\Sema.Redirector.exe"
if not exist "%EXE%" (
    echo Build Sema_Local.sln ^(Release^|x64^) first: "%EXE%" is missing.
    pause & exit /b 1
)
set "TARGET=%~1"
if not defined TARGET set "TARGET=127.0.0.1"
"%EXE%" --config "%~dp0config\regions.ini" --target "%TARGET%"
