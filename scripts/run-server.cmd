@echo off
setlocal
cd /d "%~dp0.."
set "FACTION=%~1"
set "ADVERTISE=%~2"
if not defined ADVERTISE set "ADVERTISE=127.0.0.1"
set "EXE=bin\x64\Release\Sema Local.exe"
if not exist "%EXE%" (
    echo Build Sema_Local.sln ^(Release^|x64^) first: "%EXE%" is missing.
    pause & exit /b 1
)
if not exist "config\database.ini" (
    echo Copy config\database.example.ini to config\database.ini and fill in your connection.
    pause & exit /b 1
)
if not exist "logs" mkdir "logs"

rem Recorded tutorial phases are optional; pass them when scenarios\ holds your own recordings.
set "SCENARIO_ARGS="
if exist "scenarios\tutorial-bootstrap.json" set "SCENARIO_ARGS=--scenario scenarios\tutorial-bootstrap.json"
if exist "scenarios\tutorial-cutscene-handoff.json" set "SCENARIO_ARGS=%SCENARIO_ARGS% --cutscene-phase scenarios\tutorial-cutscene-handoff.json"

"%EXE%" --host 0.0.0.0 --port 13328 --gateway --gateway-port 13700 --handshake --config "config\sema-local-%FACTION%.ini" %SCENARIO_ARGS% --skills "config\tutorial-skills.csv" --data-dir "config\game-data" --database-config "config\database.ini" --advertise-host %ADVERTISE% 2>&1 | powershell.exe -NoProfile -Command "$input | Tee-Object -FilePath 'logs\sema-local-%FACTION%.log' -Append"

echo.
echo Server stopped. Press any key to close this window.
pause >nul
