# pack-zip.ps1 — 生成便携版 zip（Scoop 等）发布资产
# 用法: powershell -File scoop\pack-zip.ps1 [-Version 1.0.2]
# 产物: eton-<Version>-x64.zip（eton.exe + LICENSE）
param(
    [string]$Version = "1.0.2"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root "eton.exe"
if (-not (Test-Path $exe)) { throw "未找到 eton.exe,请先运行 build.bat" }

$stage = Join-Path $root "build\zip"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item $stage -ItemType Directory -Force | Out-Null

Copy-Item $exe $stage
Copy-Item (Join-Path $root "LICENSE") $stage

$outZip = Join-Path $root ("eton-{0}-x64.zip" -f $Version)
if (Test-Path $outZip) { Remove-Item $outZip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $outZip
Remove-Item $stage -Recurse -Force

$sha = (Get-FileHash $outZip -Algorithm SHA256).Hash.ToLower()
Write-Host "`n便携包完成: $outZip"
Write-Host "  SHA256: $sha"
