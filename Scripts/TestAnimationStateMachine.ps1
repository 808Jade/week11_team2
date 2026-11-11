# 런타임 및 ImGui 편집기 검사를 별도 실행 파일로 빌드하고, 실패한 단계의 종료 코드를 확인한다.
param([switch]$SkipBuild, [switch]$EditorOnly, [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$Installation = & $Locator -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$Output = Join-Path $Root ('Build/StateMachineTests/' + $Configuration)
$Binaries = Join-Path $Root ('Build/Bin/' + $Configuration + '-windows-x86_64')
$Libraries = Join-Path $Root ('vcpkg_installed/x64-windows/' + $(if ($Configuration -eq 'Debug') { 'debug/lib' } else { 'lib' }))
New-Item -ItemType Directory -Force -Path $Output | Out-Null
Push-Location -LiteralPath $Root

try {
    if (-not $SkipBuild) {
        & (Join-Path $Root '.tools/premake-5.0.0-beta8/premake5.exe') vs2026
        if ($LASTEXITCODE -ne 0) {
            throw 'Project generation failed.'
        }
        & (Join-Path $Installation 'MSBuild/Current/Bin/MSBuild.exe') Hitori.slnx /t:HitoriEditor ("/p:Configuration=" + $Configuration) /p:Platform=x64 /m:4 /nr:false /v:minimal /nologo
        if ($LASTEXITCODE -ne 0) {
            throw ($Configuration + ' build failed.')
        }
    }

    $Arguments = @('/nologo', '/std:c++20', '/EHsc', '/utf-8', '/DNOMINMAX', '/DWIN32_LEAN_AND_MEAN', '/bigobj', '/Zc:preprocessor')
    $Arguments += $(if ($Configuration -eq 'Debug') { @('/MDd', '/D_DEBUG', '/DENGINE_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2') } else { @('/MD', '/DNDEBUG', '/DENGINE_RELEASE') })
    foreach ($Directory in @('Source/Runtime', 'Intermediate/Generated/Runtime', 'vcpkg_installed/x64-windows/include')) {
        $Arguments += '/I"' + (Join-Path $Root $Directory) + '"'
    }
    $Arguments += '"' + (Join-Path $Root 'Tests/AnimationStateMachineTests.cpp') + '"'
    $Arguments += '/Fe"' + (Join-Path $Binaries 'AnimationStateMachineTests.exe') + '"'
    $Arguments += '/Fo"' + (Join-Path $Output 'AnimationStateMachineTests.obj') + '"'
    $Arguments += '/link'
    # 정적 등록만 사용하는 UObject 클래스도 테스트 실행 파일에서 누락되지 않게 전체 엔진을 링크한다.
    $Arguments += '/WHOLEARCHIVE:"' + (Join-Path $Binaries 'HitoriEngine.lib') + '"'
    $Arguments += '/LIBPATH:"' + $Libraries + '"'
    $Arguments += @('DirectXTK.lib', 'lua.lib', 'd3d11.lib', 'dxgi.lib', 'dxguid.lib', 'd3dcompiler.lib', 'ole32.lib', 'user32.lib', 'gdi32.lib', 'shlwapi.lib', 'shell32.lib', 'winmm.lib', '/IGNORE:4099')
    $Response = Join-Path $Output 'Tests.rsp'
    ($Arguments -join ' ') | Set-Content -LiteralPath $Response -Encoding UTF8
    $Runner = Join-Path $Output 'Compile.cmd'
    @('@echo off', ('call "' + (Join-Path $Installation 'VC/Auxiliary/Build/vcvars64.bat') + '" >nul'), ('cl.exe @"' + $Response + '"')) | Set-Content -LiteralPath $Runner -Encoding ASCII
    if (-not $EditorOnly) {
        & $Runner
        if ($LASTEXITCODE -ne 0) {
            throw 'Test compilation failed.'
        }
        & (Join-Path $Binaries 'AnimationStateMachineTests.exe')
        if ($LASTEXITCODE -ne 0) {
            throw 'State Machine tests failed.'
        }
    }

    # 편집기 검사는 실제 편집기 오브젝트와 노드 에디터를 링크해 중첩 탐색 및 그리기를 확인한다.
    $EditorArguments = @($Arguments | ForEach-Object { $_.Replace('AnimationStateMachineTests', 'AnimationStateMachineEditorTests') })
    foreach ($Object in @('FAnimGraphEditor', 'FAnimGraphStateMachineEditor', 'FAnimGraphPanel', 'FAnimGraphNodeBuilder', 'FAnimationAssetPicker', 'EditorPCH')) {
        $EditorArguments += '"' + (Join-Path $Root ('Build/Intermediate/' + $Configuration + '-windows-x86_64/' + $Configuration + '/HitoriEditor/' + $Object + '.obj')) + '"'
    }
    $EditorArguments += $(if ($Configuration -eq 'Debug') { 'imguid.lib' } else { 'imgui.lib' })
    $EditorArguments += 'imgui-node-editor.lib'
    ($EditorArguments -join ' ') | Set-Content -LiteralPath $Response -Encoding UTF8
    & $Runner
    if ($LASTEXITCODE -ne 0) {
        throw 'Editor test compilation failed.'
    }
    & (Join-Path $Binaries 'AnimationStateMachineEditorTests.exe')
    if ($LASTEXITCODE -ne 0) {
        throw 'State Machine editor tests failed.'
    }
} finally {
    Pop-Location
}
