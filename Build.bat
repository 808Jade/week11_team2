@echo off
setlocal
set "HitoriBuildFile=%~f0"
set "HitoriBuildArg1=%~1"
set "HitoriBuildArg2=%~2"
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$Source = [IO.File]::ReadAllText($env:HitoriBuildFile); & ([scriptblock]::Create(($Source -split '(?m)^:HitoriPowerShell\r?$', 2)[1]))"
set "BuildExitCode=%ERRORLEVEL%"
if /I "%~1"=="-NoPause" goto Finish
if defined CI goto Finish
echo.
pause
:Finish
exit /b %BuildExitCode%

:HitoriPowerShell
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

try {
    if ($env:HitoriBuildArg2 -or ($env:HitoriBuildArg1 -and $env:HitoriBuildArg1 -ine '-NoPause')) {
        throw 'Usage: Build.bat [-NoPause]'
    }

    $WorkspaceRoot = Split-Path -Parent $env:HitoriBuildFile
    Set-Location -LiteralPath $WorkspaceRoot
    Write-Host '[1/4] Checking Windows x64 build tools...'
    if (-not [Environment]::Is64BitOperatingSystem) {
        throw 'Hitori requires 64-bit Windows.'
    }
    $Git = Get-Command git.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $Git) {
        throw 'Git for Windows is required. Install Git and run Build.bat again.'
    }

    $Locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $Locator)) {
        throw 'Install Visual Studio 2022/2026 or Build Tools with Desktop development with C++ and a Windows 10/11 SDK, then run Build.bat again.'
    }
    $Installation = & $Locator -latest -prerelease -products '*' -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json | Select-Object -First 1
    if ($LASTEXITCODE -ne 0 -or -not $Installation) {
        throw 'Visual Studio C++ tools were not found. Install Desktop development with C++ and a Windows 10/11 SDK, then run Build.bat again.'
    }
    $VSVersion = [version]$Installation.installationVersion
    if ($VSVersion.Major -lt 17) {
        throw 'Visual Studio 2022 or newer is required.'
    }
    $Generator = if ($VSVersion.Major -ge 18) {
        'vs2026'
    } else {
        'vs2022'
    }
    $Solution = if ($Generator -eq 'vs2026') {
        'Hitori.slnx'
    } else {
        'Hitori.sln'
    }
    $SdkRegistry = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -Name KitsRoot10 -ErrorAction SilentlyContinue
    $SdkRoot = if ($SdkRegistry) {
        $SdkRegistry.KitsRoot10
    } else {
        $null
    }
    $SdkLibraries = @(if ($SdkRoot) {
        Get-ChildItem -Path (Join-Path $SdkRoot 'Lib/*/um/x64/kernel32.lib') -ErrorAction SilentlyContinue
    })
    if ($SdkLibraries.Count -eq 0) {
        throw 'A Windows 10/11 SDK is required. Add it using Visual Studio Installer and run Build.bat again.'
    }
    Write-Host "Using $($Installation.displayName), x64"

    $ToolsDirectory = Join-Path $WorkspaceRoot '.tools'
    New-Item -ItemType Directory -Force -Path $ToolsDirectory | Out-Null
    $env:VCPKG_DISABLE_METRICS = '1'
    $env:VCPKG_VISUAL_STUDIO_PATH = $Installation.installationPath
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    Write-Host '[2/4] Preparing Premake 5.0.0-beta8...'
    $PremakeDirectory = Join-Path $ToolsDirectory 'premake-5.0.0-beta8'
    $Premake = Join-Path $PremakeDirectory 'premake5.exe'
    $PremakeArchive = Join-Path $ToolsDirectory 'premake-5.0.0-beta8-windows.zip'
    if (-not (Test-Path -LiteralPath $PremakeArchive)) {
        $Download = "$PremakeArchive.download"
        Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/premake/premake-core/releases/download/v5.0.0-beta8/premake-5.0.0-beta8-windows.zip' -OutFile $Download
        Move-Item -LiteralPath $Download -Destination $PremakeArchive -Force
    }
    if ((Get-FileHash -LiteralPath $PremakeArchive -Algorithm SHA256).Hash -ne 'E64CE2ED8778E0098F63674CCA61FE33941B5F0C8D9A4AFD651152BDEA3758AB') {
        Remove-Item -LiteralPath $PremakeArchive -Force
        throw 'Premake download failed SHA-256 verification. Run Build.bat again to download it again.'
    }
    if (-not (Test-Path -LiteralPath $Premake)) {
        Expand-Archive -LiteralPath $PremakeArchive -DestinationPath $PremakeDirectory -Force
    }

    Write-Host '[3/4] Restoring vcpkg dependencies...'
    $VcpkgRoot = Join-Path $ToolsDirectory 'vcpkg'
    $Vcpkg = Join-Path $VcpkgRoot 'vcpkg.exe'
    $Manifest = Get-Content -LiteralPath (Join-Path $WorkspaceRoot 'vcpkg.json') -Raw | ConvertFrom-Json
    $VcpkgCommit = $Manifest.'builtin-baseline'
    if ($VcpkgCommit -notmatch '^[0-9a-f]{40}$') {
        throw 'vcpkg.json must specify a full builtin-baseline commit.'
    }

    if (-not (Test-Path -LiteralPath (Join-Path $VcpkgRoot '.git'))) {
        if (Test-Path -LiteralPath $VcpkgRoot) {
            throw "The vcpkg directory exists but is not a Git repository: $VcpkgRoot"
        }
        & $Git.Source init $VcpkgRoot
        if ($LASTEXITCODE -ne 0) {
            throw 'Could not initialize vcpkg.'
        }
        & $Git.Source -C $VcpkgRoot remote add origin https://github.com/microsoft/vcpkg.git
        if ($LASTEXITCODE -ne 0) {
            throw 'Could not configure the vcpkg remote.'
        }
    }
    $CurrentCommit = & $Git.Source -C $VcpkgRoot rev-parse --verify --quiet HEAD
    $NeedsCheckout = $LASTEXITCODE -ne 0 -or $CurrentCommit -ne $VcpkgCommit
    $IsShallowRepository = & $Git.Source -C $VcpkgRoot rev-parse --is-shallow-repository
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not inspect the vcpkg repository.'
    }
    if ($NeedsCheckout -or $IsShallowRepository -eq 'true' -or -not (Test-Path -LiteralPath $Vcpkg)) {
        $FetchOptions = @()
        if ($IsShallowRepository -eq 'true') {
            $FetchOptions += '--unshallow'
        }
        & $Git.Source -C $VcpkgRoot fetch origin $VcpkgCommit @FetchOptions
        if ($LASTEXITCODE -ne 0) {
            throw 'Could not fetch the pinned vcpkg commit.'
        }
        & $Git.Source -C $VcpkgRoot checkout --detach $VcpkgCommit
        if ($LASTEXITCODE -ne 0) {
            throw 'Could not check out the pinned vcpkg commit.'
        }
        & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
        if ($LASTEXITCODE -ne 0) {
            throw 'Could not bootstrap vcpkg.'
        }
    }
    $env:VCPKG_ROOT = $VcpkgRoot
    & $Vcpkg install "--x-manifest-root=$WorkspaceRoot" "--x-install-root=$(Join-Path $WorkspaceRoot 'vcpkg_installed')" --triplet=x64-windows --x-wait-for-lock
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not restore project dependencies.'
    }

    Write-Host '[4/4] Generating the Visual Studio solution...'
    & $Premake "--file=$(Join-Path $WorkspaceRoot 'premake5.lua')" $Generator
    if ($LASTEXITCODE -ne 0) {
        throw 'Project generation failed.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $WorkspaceRoot $Solution))) {
        throw "Premake did not generate $Solution."
    }

    Write-Host ''
    Write-Host 'Hitori solution is ready.' -ForegroundColor Green
    Write-Host "Solution: $(Join-Path $WorkspaceRoot $Solution)"
    Write-Host 'Open the solution in Visual Studio and select Debug or Release | x64 to build.'
    exit 0
} catch {
    Write-Host ''
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
