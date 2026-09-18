@echo off
title Victron LoRaBLE Remote - First installation
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0First-Install.ps1" -RequestElevation
echo.
pause
