@echo off
chcp 65001 >nul
cd /d "%~dp0"
if exist "%~dp0download.ps1" (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0download.ps1"
) else (
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0下载固件.ps1"
)
echo.
pause
