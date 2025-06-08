@echo off
setlocal

taskkill /IM Gothic2.exe /F 2>nul
timeout /t 1 /nobreak >nul
start "" /D "C:\Games v3\Gothic 2\system" "Gothic2.exe"
timeout /t 2 /nobreak >nul
powershell -ExecutionPolicy Bypass -File "%~dp0AttachVS.ps1"

endlocal