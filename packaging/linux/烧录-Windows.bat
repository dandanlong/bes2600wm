@echo off
chcp 65001 >nul
REM 把下面的 6 改成你电脑上的 COM 口号（设备管理器里看 USB 串口，COM3 就写 3）
set COM=6

echo 即将用 COM%COM% 全量烧录 BES2600
echo 请先：USB 接下载口，板子上电，必要时按一下复位
pause

dldtool.exe %COM% programmer2003.bin -M nuttx_bl.bin --addr 0x2C040000 nuttx_ota.bin --addr 0x2C0C0000 nuttx_ota.bin --addr 0x2C850000 nuttx_a7.bin -M nuttx_ap.bin --pgm-rate 2000000

echo.
echo 烧录结束。如果失败：1) 换 COM 号  2) 再按复位重试  3) 确认接的是下载串口
pause
