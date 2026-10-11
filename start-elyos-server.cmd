@echo off
rem Starts the gateway (lobby, 13700) and the gameplay server (13328) for the Elyos server entry.
rem Usage: start-elyos-server.cmd [advertised host, default 127.0.0.1]
call "%~dp0scripts\run-server.cmd" elyos %1
