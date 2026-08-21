@echo off
chcp 65001 >nul
cd /d "%~dp0"
set COM=6
if exist com.txt set /p COM=<com.txt
echo 即将用 COM%COM% 只烧 nuttx_ap.bin
pause
if not exist dldtool.exe (
  echo 找不到 dldtool.exe，请先双击 下载固件.bat
  pause
  exit /b 1
)
dldtool.exe %COM% programmer2003.bin -M nuttx_ap.bin --pgm-rate 2000000
echo.
echo 烧录结束。
pause
