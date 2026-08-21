$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

function Read-Config {
    param([string]$Path)
    $cfg = @{}
    Get-Content -LiteralPath $Path -Encoding UTF8 | ForEach-Object {
        $line = $_.Trim()
        if ($line -eq "" -or $line.StartsWith("#")) { return }
        $kv = $line.Split("=", 2)
        if ($kv.Count -eq 2) { $cfg[$kv[0].Trim()] = $kv[1].Trim() }
    }
    return $cfg
}

$cfgPath = Join-Path $ScriptDir "config.txt"
if (-not (Test-Path -LiteralPath $cfgPath)) {
    Write-Host "找不到 config.txt，请把它和本脚本放在同一目录。" -ForegroundColor Red
    exit 1
}

$cfg = Read-Config $cfgPath
$LocalDir = $cfg["LOCAL_DIR"]
$HostName = $cfg["SERVER_HOST"]
$UserName = $cfg["SERVER_USER"]
$RemoteDir = $cfg["REMOTE_DIR"].TrimEnd("/")
$ComPort = $cfg["COM"]

if ([string]::IsNullOrWhiteSpace($LocalDir)) { $LocalDir = $ScriptDir }

New-Item -ItemType Directory -Force -Path $LocalDir | Out-Null
Set-Location $LocalDir

$scp = Join-Path $env:SystemRoot "System32\OpenSSH\scp.exe"
if (-not (Test-Path -LiteralPath $scp)) {
    $scpCmd = Get-Command scp -ErrorAction SilentlyContinue
    if ($scpCmd) {
        $scp = $scpCmd.Source
    } else {
        Write-Host "本机没有 scp。请先安装 OpenSSH 客户端：" -ForegroundColor Red
        Write-Host "  设置 -> 应用 -> 可选功能 -> 添加 OpenSSH 客户端" -ForegroundColor Yellow
        exit 1
    }
}

$files = @(
    "dldtool.exe",
    "programmer2003.bin",
    "nuttx_bl.bin",
    "nuttx_ota.bin",
    "nuttx_a7.bin",
    "nuttx_ap.bin"
)

Write-Host ""
Write-Host "从服务器拉取烧录工具和固件" -ForegroundColor Cyan
Write-Host "  服务器 : ${UserName}@${HostName}"
Write-Host "  远端   : $RemoteDir"
Write-Host "  本机   : $LocalDir"
Write-Host "  串口   : COM$ComPort"
Write-Host "如果弹出密码，输入服务器登录密码即可。"
Write-Host ""

$remote = "${UserName}@${HostName}:${RemoteDir}"
$scpArgsBase = @(
    "-o", "StrictHostKeyChecking=accept-new",
    "-o", "UserKnownHostsFile=$env:USERPROFILE\.ssh\known_hosts"
)

foreach ($name in $files) {
    Write-Host "下载 $name ..."
    & $scp @scpArgsBase "$remote/$name" (Join-Path $LocalDir $name)
    if ($LASTEXITCODE -ne 0) {
        Write-Host "下载失败: $name" -ForegroundColor Red
        exit 1
    }
}

Set-Content -LiteralPath (Join-Path $LocalDir "com.txt") -Value $ComPort -Encoding ASCII

$flashBat = @'
@echo off
chcp 65001 >nul
cd /d "%~dp0"
set COM=__COM__
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
'@
$flashBat = $flashBat.Replace('__COM__', $ComPort)

$flashApBat = @'
@echo off
chcp 65001 >nul
cd /d "%~dp0"
set COM=__COM__
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
'@
$flashApBat = $flashApBat.Replace('__COM__', $ComPort)

Set-Content -LiteralPath (Join-Path $LocalDir "烧录.bat") -Value $flashBat -Encoding UTF8
Set-Content -LiteralPath (Join-Path $LocalDir "烧录-只烧AP.bat") -Value $flashApBat -Encoding UTF8

Write-Host ""
Write-Host "下载完成。本机文件：" -ForegroundColor Green
Get-ChildItem -LiteralPath $LocalDir -File | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
Write-Host "下一步：改好 config.txt 里的 COM 后，双击 烧录.bat" -ForegroundColor Green
