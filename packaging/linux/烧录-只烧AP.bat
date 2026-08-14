@echo off
chcp 65001 >nul
REM 板子已经能开机、只改了应用固件时，可以只烧 AP（更快）
set COM=6

echo 即将用 COM%COM% 只烧 nuttx_ap.bin
pause

dldtool.exe %COM% programmer2003.bin -M nuttx_ap.bin --pgm-rate 2000000

echo.
echo 烧录结束。
pause
