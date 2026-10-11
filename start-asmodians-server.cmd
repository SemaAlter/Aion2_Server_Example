@echo off
rem Starts the gateway (lobby, 13700) and the gameplay server (13328) for the Asmodians server entry.
rem Usage: start-asmodians-server.cmd [advertised host, default 127.0.0.1]
call "%~dp0scripts\run-server.cmd" asmodians %1
