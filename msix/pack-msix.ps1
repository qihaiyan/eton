param(
    [string]$Name = "ETON",
    [string]$Publisher = "CN=ETON Local Test",
    [string]$Version = "1.0.0.0",
    [switch]$Sign
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $root "eton.exe"
if (-not (Test-Path $exe)) { throw "未找到 eton.exe,请先运行 build.bat" }

$rev = ($Version -split '\.')[3]
if ($null -ne $rev -and $rev -ne '0') {
    Write-Warning "版本 $Version 的第 4 段(修订号)非 0:微软商店会拒绝此包(包接受验证错误);本机侧载不受影响。"
}

$sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
$makeappx = Get-ChildItem $sdkRoot -Recurse -Filter makeappx.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\x64\\" } |
            Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
$signtool = Get-ChildItem $sdkRoot -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "\\x64\\" } |
            Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $makeappx) { throw "未找到 makeappx.exe,请安装 Windows SDK" }

$layout = Join-Path $root "build\msix\layout"
if (Test-Path $layout) { Remove-Item $layout -Recurse -Force }
New-Item $layout -ItemType Directory -Force | Out-Null
New-Item (Join-Path $layout "Assets") -ItemType Directory -Force | Out-Null

Copy-Item $exe $layout

Add-Type -AssemblyName System.Drawing
$srcImg = [System.Drawing.Image]::FromFile((Join-Path $root "res\app.png"))
function Save-Scaled([int]$px, [string]$file) {
    $bmp = New-Object System.Drawing.Bitmap $px, $px
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($srcImg, 0, 0, $px, $px)
    $g.Dispose()
    $bmp.Save((Join-Path $layout "Assets\$file"), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}
Save-Scaled 44  "Square44x44Logo.png"
Save-Scaled 44  "Square44x44Logo.scale-100.png"
Save-Scaled 88  "Square44x44Logo.scale-200.png"
Save-Scaled 44  "Square44x44Logo.targetsize-44.png"
Save-Scaled 150 "Square150x150Logo.png"
Save-Scaled 150 "Square150x150Logo.scale-100.png"
Save-Scaled 300 "Square150x150Logo.scale-200.png"
Save-Scaled 50  "StoreLogo.scale-100.png"
Save-Scaled 50  "StoreLogo.png"
$srcImg.Dispose()

$manifest = Get-Content (Join-Path $PSScriptRoot "AppxManifest.template.xml") -Raw -Encoding UTF8
$manifest = $manifest.Replace("__NAME__", $Name).Replace("__PUBLISHER__", $Publisher).Replace("__VERSION__", $Version)
[System.IO.File]::WriteAllText((Join-Path $layout "AppxManifest.xml"), $manifest, (New-Object System.Text.UTF8Encoding $false))

$outMsix = Join-Path $root ("eton-{0}-x64.msix" -f $Version)
if (Test-Path $outMsix) { Remove-Item $outMsix -Force }
& $makeappx pack /o /v /d $layout /p $outMsix
if ($LASTEXITCODE -ne 0) { throw "makeappx 打包失败" }

if ($Sign) {
    if (-not $signtool) { throw "未找到 signtool.exe" }
    $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $Publisher -and $_.HasPrivateKey }
    if (-not $cert) {
        $cert = New-SelfSignedCertificate -Subject $Publisher -Type CodeSigningCert `
                -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddYears(2)
    }
    & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint $outMsix
    if ($LASTEXITCODE -ne 0) { throw "signtool 签名失败" }
    $cerPath = Join-Path $root "build\msix\eton-test-signing.cer"
    [System.IO.File]::WriteAllBytes($cerPath, $cert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))
    Write-Host "已导出测试证书: $cerPath (安装包前需将其导入 LocalMachine\TrustedPeople)"
}

Write-Host "`nMSIX 打包完成: $outMsix"
Write-Host "  Name      = $Name"
Write-Host "  Publisher = $Publisher"
Write-Host "  Version   = $Version"
