param([switch]$Build, [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release')

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Executable = Join-Path $Root ('Build/Bin/' + $Configuration + '-windows-x86_64/HitoriGame.exe')

if ($Build -or -not (Test-Path -LiteralPath $Executable)) {
    $Locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $Installation = & $Locator -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    Push-Location -LiteralPath $Root
    try {
        & (Join-Path $Installation 'MSBuild/Current/Bin/MSBuild.exe') Hitori.slnx /t:HitoriGame ("/p:Configuration=" + $Configuration) /p:Platform=x64 /m:4 /nr:false /v:minimal /nologo
        if ($LASTEXITCODE -ne 0) {
            throw 'Devil Duel build failed.'
        }
    } finally {
        Pop-Location
    }
}

Start-Process -FilePath $Executable -ArgumentList '-Map=Scenes/DevilDuel.scene' -WorkingDirectory $Root
