param([switch]$SkipBuild, [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release')

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$Installation = & $Locator -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$Output = Join-Path $Root 'Build/DevilDuelTests'
$Binaries = Join-Path $Root ('Build/Bin/' + $Configuration + '-windows-x86_64')
$Libraries = Join-Path $Root ('vcpkg_installed/x64-windows/' + $(if ($Configuration -eq 'Debug') { 'debug/lib' } else { 'lib' }))
New-Item -ItemType Directory -Force -Path $Output | Out-Null
Push-Location -LiteralPath $Root

try {
    if (-not $SkipBuild) {
        & (Join-Path $Installation 'MSBuild/Current/Bin/MSBuild.exe') Hitori.slnx '/t:HitoriGame;HitoriEditor' ("/p:Configuration=" + $Configuration) /p:Platform=x64 /m:4 /nr:false /v:minimal /nologo
        if ($LASTEXITCODE -ne 0) {
            throw 'Devil Duel build failed.'
        }
    }

    $Arguments = @('/nologo', '/std:c++20', '/EHsc', '/utf-8', '/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN', '/bigobj', '/Zc:preprocessor')
    $Arguments += $(if ($Configuration -eq 'Debug') { @('/MDd', '/D_DEBUG', '/DENGINE_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2') } else { @('/MD', '/DNDEBUG', '/DENGINE_RELEASE') })
    foreach ($Directory in @('Source/Runtime', 'Intermediate/Generated/Runtime', 'vcpkg_installed/x64-windows/include')) {
        $Arguments += '/I"' + (Join-Path $Root $Directory) + '"'
    }
    $Arguments += '"' + (Join-Path $Root 'Tests/DevilDuelTests.cpp') + '"'
    $Arguments += '/Fe"' + (Join-Path $Binaries 'DevilDuelTests.exe') + '"'
    $Arguments += '/Fo"' + (Join-Path $Output 'DevilDuelTests.obj') + '"'
    $Arguments += '/link'
    $Arguments += '/WHOLEARCHIVE:"' + (Join-Path $Binaries 'HitoriEngine.lib') + '"'
    $Arguments += '/LIBPATH:"' + $Libraries + '"'
    $Arguments += @('DirectXTK.lib', 'lua.lib', 'd3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib', 'ole32.lib', 'user32.lib', 'gdi32.lib', 'shlwapi.lib', 'shell32.lib', 'winmm.lib', 'windowscodecs.lib', '/IGNORE:4099')
    $Response = Join-Path $Output 'Tests.rsp'
    ($Arguments -join ' ') | Set-Content -LiteralPath $Response -Encoding UTF8
    $Runner = Join-Path $Output 'Compile.cmd'
    @('@echo off', ('call "' + (Join-Path $Installation 'VC/Auxiliary/Build/vcvars64.bat') + '" >nul'), ('cl.exe @"' + $Response + '"')) | Set-Content -LiteralPath $Runner -Encoding ASCII
    & $Runner
    if ($LASTEXITCODE -ne 0) {
        throw 'Devil Duel test compilation failed.'
    }
    & (Join-Path $Binaries 'DevilDuelTests.exe')
    if ($LASTEXITCODE -ne 0) {
        throw 'Devil Duel tests failed.'
    }
} finally {
    Pop-Location
}
