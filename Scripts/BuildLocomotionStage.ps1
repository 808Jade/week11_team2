param([switch]$SkipBuild, [switch]$Import, [switch]$Capture, [string]$SourceDirectory = 'C:\Users\JUNGLE\Downloads\Universal Animation Library[Standard]\Universal Animation Library[Standard]\Unity')

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$Installation = & $Locator -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$Output = Join-Path $Root 'Build/LocomotionStage'
$Binaries = Join-Path $Root 'Build/Bin/Debug-windows-x86_64'
$Libraries = Join-Path $Root 'vcpkg_installed/x64-windows/debug/lib'
New-Item -ItemType Directory -Force -Path $Output | Out-Null
Push-Location -LiteralPath $Root

try {
    if (-not $SkipBuild) {
        & (Join-Path $Root '.tools/premake-5.0.0-beta8/premake5.exe') vs2026
        if ($LASTEXITCODE -ne 0) { throw 'Project generation failed.' }
        & (Join-Path $Installation 'MSBuild/Current/Bin/MSBuild.exe') Hitori.slnx /t:HitoriEditor /p:Configuration=Debug /p:Platform=x64 /m:4 /nr:false /v:minimal /nologo
        if ($LASTEXITCODE -ne 0) { throw 'Debug build failed.' }
    }

    $Arguments = @('/nologo', '/std:c++20', '/EHsc', '/utf-8', '/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN', '/bigobj', '/Zc:preprocessor', '/MDd', '/D_DEBUG', '/DENGINE_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2')
    foreach ($Directory in @('Source', 'Source/Runtime', 'Intermediate/Generated/Runtime', 'vcpkg_installed/x64-windows/include')) {
        $Arguments += '/I"' + (Join-Path $Root $Directory) + '"'
    }
    $Arguments += '"' + (Join-Path $Root 'Tests/LocomotionStageTool.cpp') + '"'
    $Arguments += '/Fe"' + (Join-Path $Binaries 'LocomotionStageTool.exe') + '"'
    $Arguments += '/Fo"' + (Join-Path $Output 'LocomotionStageTool.obj') + '"'
    $Arguments += '/link'
    $Arguments += '/WHOLEARCHIVE:"' + (Join-Path $Binaries 'HitoriEngine.lib') + '"'
    $Arguments += '/LIBPATH:"' + $Libraries + '"'
    foreach ($Object in @('FbxImporter', 'HumanoidRetarget', 'EditorPCH')) {
        $Arguments += '"' + (Join-Path $Root ('Build/Intermediate/Debug-windows-x86_64/Debug/HitoriEditor/' + $Object + '.obj')) + '"'
    }
    $Arguments += @('libfbxsdk.lib', 'DirectXTK.lib', 'lua.lib', 'd3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib', 'ole32.lib', 'user32.lib', 'gdi32.lib', 'shlwapi.lib', 'shell32.lib', 'winmm.lib', 'windowscodecs.lib', '/IGNORE:4099')
    $Response = Join-Path $Output 'Tool.rsp'
    ($Arguments -join ' ') | Set-Content -LiteralPath $Response -Encoding UTF8
    $Runner = Join-Path $Output 'Compile.cmd'
    @('@echo off', ('call "' + (Join-Path $Installation 'VC/Auxiliary/Build/vcvars64.bat') + '" >nul'), ('cl.exe @"' + $Response + '"')) | Set-Content -LiteralPath $Runner -Encoding ASCII
    & $Runner
    if ($LASTEXITCODE -ne 0) { throw 'Locomotion Stage tool compilation failed.' }
    if ($Import) {
        & (Join-Path $Binaries 'LocomotionStageTool.exe') --import $SourceDirectory
        if ($LASTEXITCODE -ne 0) { throw 'Animation library import failed.' }
    }
    & node (Join-Path $Root 'Scripts/GenerateLocomotionStage.mjs')
    if ($LASTEXITCODE -ne 0) { throw 'Stage content generation failed.' }
    & (Join-Path $Binaries 'LocomotionStageTool.exe') $(if ($Capture) { '--capture' } else { '--verify' })
    if ($LASTEXITCODE -ne 0) { throw 'Locomotion Stage verification failed.' }
} finally {
    Pop-Location
}
