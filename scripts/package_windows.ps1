# Build and package the x64 / x86 Windows releases.
# Produces dist/PhiPhysiceEditorNeo_v1.0.0_windows-x64.zip and ...-x86.zip.
[CmdletBinding()]
param(
    [string]$Version = '1.0.0',
    [switch]$SkipBuild,
    [switch]$SkipVerify
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Dist = Join-Path $Root 'dist'
$BaseName = "PhiPhysiceEditorNeo-v$Version"
Write-Host "Project: $Root"
Write-Host "Version: $Version"

$running = Get-Process PhiPhysiceEditorNeo -ErrorAction SilentlyContinue
if ($running) {
    throw 'PhiPhysiceEditorNeo is running. Close it before packaging.'
}

function Find-VisualStudio {
    $vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($found) { return $found.Trim() }
    }
    foreach ($candidate in @(
        'D:\Program Files\Microsoft Visual Studio\18\Community',
        'C:\Program Files\Microsoft Visual Studio\18\Community',
        'C:\Program Files\Microsoft Visual Studio\2022\Community',
        'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community')) {
        if (Test-Path (Join-Path $candidate 'VC\Auxiliary\Build\vcvars64.bat')) { return $candidate }
    }
    throw 'Visual Studio with the C++ desktop workload was not found.'
}

$vs = Find-VisualStudio
$redistRoot = Join-Path $vs 'VC\Redist\MSVC'
$redistVersion = Get-ChildItem $redistRoot -Directory -ErrorAction SilentlyContinue |
    Where-Object {
        (Test-Path (Join-Path $_.FullName 'x64')) -and
        (Test-Path (Join-Path $_.FullName 'x86'))
    } |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $redistVersion) { throw "Visual C++ redistributable folder not found under $redistRoot" }

function Get-RuntimeDlls([string]$arch) {
    $crtDir = Get-ChildItem (Join-Path $redistVersion.FullName $arch) -Directory -Filter 'Microsoft.VC*.CRT' |
        Select-Object -First 1
    if (-not $crtDir) { throw "VC runtime folder not found for $arch" }
    $names = @('msvcp140.dll', 'vcruntime140.dll')
    if ($arch -eq 'x64') { $names += 'vcruntime140_1.dll' }
    $result = @()
    foreach ($name in $names) {
        $path = Join-Path $crtDir.FullName $name
        if (Test-Path $path) { $result += $path } else { Write-Warning "Missing runtime DLL: $path" }
    }
    return $result
}

if (-not $SkipBuild) {
    foreach ($arch in @('x64', 'x86')) {
        Write-Host ""
        Write-Host "=== Building $arch ==="
        $raylib = if ($arch -eq 'x64') {
            'D:\tools\raylib-5.5_win64_msvc16'
        } else {
            'D:\tools\raylib-5.5_win32_msvc16'
        }
        $buildArgs = @(
            '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'build_windows.ps1'),
            '-Arch', $arch,
            '-BuildDir', "build\$arch",
            '-NoCopyRoot'
        )
        if (Test-Path (Join-Path $raylib 'include\raylib.h')) {
            $buildArgs += @('-RaylibRoot', $raylib)
        }
        & powershell @buildArgs
        if ($LASTEXITCODE -ne 0) { throw "Build failed for $arch" }
    }
}

New-Item -ItemType Directory -Force -Path $Dist | Out-Null
$stageDirs = @{}
foreach ($arch in @('x64', 'x86')) {
    $stage = Join-Path $Dist "$BaseName-win-$arch"
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null

    $exe = Join-Path $Root "build\$arch\PhiPhysiceEditorNeo.exe"
    if (-not (Test-Path $exe)) { throw "Missing executable: $exe" }
    Copy-Item $exe (Join-Path $stage 'PhiPhysiceEditorNeo.exe')

    foreach ($item in @('components', 'assets', 'projects', 'docs')) {
        Copy-Item -Recurse -Force (Join-Path $Root $item) (Join-Path $stage $item)
    }
    foreach ($file in @('export_rules.txt', 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md')) {
        Copy-Item -Force (Join-Path $Root $file) (Join-Path $stage $file)
    }

    $runtime = Get-RuntimeDlls $arch
    foreach ($dll in $runtime) {
        Copy-Item -Force $dll (Join-Path $stage (Split-Path $dll -Leaf))
    }

    # No timestamps or local paths here: a staged folder must be byte-identical
    # to the committed one so the release hashes can be reproduced.
    $buildInfo = @(
        "PhiPhysiceEditorNeo $Version",
        "Architecture: $arch",
        "Configuration: Release",
        "",
        "License: MIT (see LICENSE)",
        "Font: PhiPhysice Neo Sans Regular, SIL OFL 1.1 (see assets/fonts/LICENSE-OFL.txt and NOTICE.md)",
        "Third-party notices: THIRD_PARTY_NOTICES.md (full texts in docs/licenses/)",
        ""
    ) -join [Environment]::NewLine
    Set-Content -Path (Join-Path $stage 'BUILD-INFO.txt') -Value $buildInfo -Encoding UTF8

    $stageDirs[$arch] = $stage
    Write-Host "Staged $arch -> $stage"
}

$zipMap = @{}
foreach ($arch in @('x64', 'x86')) {
    $zip = Join-Path $Dist ("PhiPhysiceEditorNeo_v" + $Version + "_windows-$arch.zip")
    if (Test-Path $zip) { Remove-Item -Force $zip }
    Compress-Archive -Path $stageDirs[$arch] -DestinationPath $zip -CompressionLevel Optimal
    $zipMap[$arch] = $zip
    Write-Host "Created $zip"
}

# SHA256 checksums.
$sums = Join-Path $Dist 'SHA256SUMS.txt'
$lines = @()
foreach ($arch in @('x64', 'x86')) {
    $hash = (Get-FileHash $zipMap[$arch] -Algorithm SHA256).Hash.ToLowerInvariant()
    $name = Split-Path $zipMap[$arch] -Leaf
    $lines += "$hash  $name"
}
Set-Content -Path $sums -Value ($lines -join [Environment]::NewLine) -Encoding ASCII
Write-Host "Created $sums"

if (-not $SkipVerify) {
    Write-Host ""
    Write-Host '=== Verifying packages ==='
    foreach ($arch in @('x64', 'x86')) {
        $zip = $zipMap[$arch]
        $check = Join-Path $env:TEMP ("phineo-verify-" + $arch)
        if (Test-Path $check) { Remove-Item -Recurse -Force $check }
        Expand-Archive -Path $zip -DestinationPath $check -Force
        $exe = Join-Path $check "$BaseName-win-$arch\PhiPhysiceEditorNeo.exe"
        $out = Join-Path $check 'selftest.log'
        $err = Join-Path $check 'selftest.err.log'
        $p = Start-Process -FilePath $exe -ArgumentList '--selftest' -NoNewWindow -Wait -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
        $tail = Get-Content $out -Tail 2 -ErrorAction SilentlyContinue
        Write-Host "$arch selftest exit=$($p.ExitCode) $($tail -join ' ')"
        if ($p.ExitCode -ne 0) {
            Get-Content $err -ErrorAction SilentlyContinue
            throw "Packaged $arch self-test failed"
        }
        Remove-Item -Recurse -Force $check
    }
}

Write-Host ""
Write-Host 'Done:'
Get-ChildItem $Dist -File | Select-Object Name, Length | Format-Table -AutoSize | Out-String | Write-Host
