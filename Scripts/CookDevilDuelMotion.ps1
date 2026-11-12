$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Output = Join-Path $Root 'Build/DevilDuelCook'
$Binaries = Join-Path $Root 'Build/Bin/Release-windows-x86_64'
$Locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$Installation = & $Locator -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
New-Item -ItemType Directory -Path $Output -Force | Out-Null
$Arguments = @('/nologo', '/std:c++20', '/EHsc', '/MD', '/DNDEBUG', '/DENGINE_RELEASE', '/utf-8', '/bigobj', '/Zc:preprocessor')
foreach ($Directory in @('Source', 'Source/Runtime', 'Intermediate/Generated/Runtime', 'vcpkg_installed/x64-windows/include')) {
    $Arguments += '/I"' + (Join-Path $Root $Directory) + '"'
}
$Arguments += '"' + (Join-Path $Root 'Scripts/CookDevilDuelMotion.cpp') + '"'
$Arguments += '/Fe"' + (Join-Path $Binaries 'DevilDuelCook.exe') + '"'
$Arguments += '/Fo"' + (Join-Path $Output 'Cook.obj') + '"'
$Arguments += '/link'
foreach ($Object in @('FbxImporter', 'HumanoidRetarget', 'EditorPCH')) {
    $Arguments += '"' + (Join-Path $Root ('Build/Intermediate/Release-windows-x86_64/Release/HitoriEditor/' + $Object + '.obj')) + '"'
}
$Arguments += '/WHOLEARCHIVE:"' + (Join-Path $Binaries 'HitoriEngine.lib') + '"'
$Arguments += '/LIBPATH:"' + (Join-Path $Root 'vcpkg_installed/x64-windows/lib') + '"'
$Arguments += @('DirectXTK.lib', 'lua.lib', 'libfbxsdk.lib', 'd3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib', 'ole32.lib', 'user32.lib', 'gdi32.lib', 'shlwapi.lib', 'shell32.lib', 'winmm.lib', '/IGNORE:4099')
$Response = Join-Path $Output 'Cook.rsp'
($Arguments -join ' ') | Set-Content -LiteralPath $Response -Encoding UTF8
$Runner = Join-Path $Output 'Compile.cmd'
@('@echo off', ('call "' + (Join-Path $Installation 'VC/Auxiliary/Build/vcvars64.bat') + '" >nul'), ('cl.exe @"' + $Response + '"')) | Set-Content -LiteralPath $Runner -Encoding ASCII
Push-Location -LiteralPath $Root
try {
    & $Runner
    if ($LASTEXITCODE -ne 0) {
        throw 'Motion cooker compilation failed.'
    }
    & (Join-Path $Binaries 'DevilDuelCook.exe')
    if ($LASTEXITCODE -ne 0) {
        throw 'Motion cooking failed.'
    }
} finally {
    Pop-Location
}
