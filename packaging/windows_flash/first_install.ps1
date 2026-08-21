# Run on Windows from the directory where firmware tools should live.
param(
    [string]$LocalDir = $PSScriptRoot,
    [string]$Server = "wang@192.168.1.7",
    [string]$RemoteDir = "projects/bes-wavetable/packaging/windows_flash"
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

if ([string]::IsNullOrWhiteSpace($LocalDir)) {
    $LocalDir = (Get-Location).Path
}

Write-Host "Local dir: $LocalDir" -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path $LocalDir | Out-Null

$scp = Join-Path $env:SystemRoot "System32\OpenSSH\scp.exe"
if (-not (Test-Path -LiteralPath $scp)) {
    $scpCmd = Get-Command scp -ErrorAction SilentlyContinue
    if ($scpCmd) { $scp = $scpCmd.Source } else {
        Write-Host "scp not found. Install OpenSSH Client: Settings -> Apps -> Optional features" -ForegroundColor Red
        exit 1
    }
}

$scriptFiles = @(
    "config.txt",
    "download.ps1",
    "download.bat",
    "flash.bat",
    "flash_ap_only.bat"
)

Write-Host "Copy scripts from server (password may be required)..."
foreach ($name in $scriptFiles) {
    & $scp -o StrictHostKeyChecking=accept-new "${Server}:${RemoteDir}/$name" (Join-Path $LocalDir $name)
    if ($LASTEXITCODE -ne 0) {
        Write-Host "copy failed: $name" -ForegroundColor Red
        exit 1
    }
}

Copy-Item -Force (Join-Path $LocalDir "download.ps1") (Join-Path $LocalDir "下载固件.ps1")
Copy-Item -Force (Join-Path $LocalDir "download.bat") (Join-Path $LocalDir "下载固件.bat")
Copy-Item -Force (Join-Path $LocalDir "flash.bat") (Join-Path $LocalDir "烧录.bat")
Copy-Item -Force (Join-Path $LocalDir "flash_ap_only.bat") (Join-Path $LocalDir "烧录-只烧AP.bat")

Write-Host "Download firmware and dldtool..."
Set-Location $LocalDir
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $LocalDir "download.ps1")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "Done. Next time: double-click 下载固件.bat then 烧录.bat in $LocalDir" -ForegroundColor Green
