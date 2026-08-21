@echo off
chcp 65001 >nul
cd /d "%~dp0"
set COM=6
if exist com.txt set /p COM=<com.txt
echo 即将用 COM%COM% 全量烧录 BES2600
echo 请先：USB 接下载口，板子上电，必要时按一下复位
pause
if not exist dldtool.exe (
  echo 找不到 dldtool.exe，请先双击 下载固件.bat
  pause
  exit /b 1
)
dldtool.exe %COM% programmer2003.bin -M nuttx_bl.bin --addr 0x2C040000 nuttx_ota.bin --addr 0x2C0C0000 nuttx_ota.bin --addr 0x2C850000 nuttx_a7.bin -M nuttx_ap.bin --pgm-rate 2000000
echo.
echo 烧录结束。失败时：1) 改 config.txt 里的 COM  2) 再按复位  3) 确认接的是下载串口
pause
