# Build PhiPhysiceEditorNeo on Windows with Visual Studio's MSVC, CMake and Ninja.
# If the project path contains non-ASCII characters, the script maps a temporary
# drive letter with subst so Ninja/CMake see an ASCII path.
[CmdletBinding()]
param(
    [ValidateSet('x64', 'x86')]
    [string]$Arch = 'x64',
    [string]$RaylibRoot = $env:RAYLIB_ROOT,
    [switch]$SkipTests,
    [switch]$Run,
    [string]$BuildDir = '',
    [switch]$NoCopyRoot
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Write-Host "Project: $Root"

$running = Get-Process PhiPhysiceEditorNeo -ErrorAction SilentlyContinue
if ($running) {
    throw "PhiPhysiceEditorNeo is running. Close it before rebuilding (the executable is copied to the project root)."
}

function Find-VisualStudio {
    $vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($found) { return $found.Trim() }
    }
    $candidates = @(
        'D:\Program Files\Microsoft Visual Studio\18\Community',
        'C:\Program Files\Microsoft Visual Studio\18\Community',
        'C:\Program Files\Microsoft Visual Studio\2022\Community',
        'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate 'VC\Auxiliary\Build\vcvars64.bat')) { return $candidate }
    }
    throw "Visual Studio with the C++ desktop workload was not found."
}

$vs = Find-VisualStudio
$vcvarsName = if ($Arch -eq 'x86') { 'vcvars32.bat' } else { 'vcvars64.bat' }
$vcvars = Join-Path $vs ('VC\Auxiliary\Build\' + $vcvarsName)
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not (Test-Path $cmake)) { $cmake = (Get-Command cmake.exe).Source }
if (-not (Test-Path $ninja)) { $ninja = (Get-Command ninja.exe).Source }


# Import the MSVC environment.
$cmdLine = '"' + $vcvars + '" >nul 2>&1 && set'
$envLines = & cmd /c $cmdLine
if ($LASTEXITCODE -ne 0) { throw "Failed to initialise the Visual Studio environment." }
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        Set-Item -Path ('env:' + $matches[1]) -Value $matches[2]
    }
}

if (-not $RaylibRoot) {
    $defaults = if ($Arch -eq 'x86') {
        @(
            'D:\tools\raylib-5.5_win32_msvc16',
            'C:\raylib',
            (Join-Path $Root 'third_party\raylib')
        )
    } else {
        @(
            'D:\tools\raylib-5.5_win64_msvc16',
            'C:\raylib',
            (Join-Path $Root 'third_party\raylib')
        )
    }
    foreach ($candidate in $defaults) {
        if (Test-Path (Join-Path $candidate 'include\raylib.h')) { $RaylibRoot = $candidate; break }
    }
}
if ($RaylibRoot -and -not (Test-Path (Join-Path $RaylibRoot 'include\raylib.h'))) {
    Write-Warning "RAYLIB_ROOT does not contain include/raylib.h: $RaylibRoot (CMake will fetch raylib)"
    $RaylibRoot = ''
}

# Map an ASCII drive if the source path contains non-ASCII characters.
$asciiRoot = $Root
$mappedDrive = $null
if ($Root -match '[^\x00-\x7F]') {
    foreach ($letter in 'PQRSTUVWXYZ'.ToCharArray()) {
        $drive = [string]$letter + ':'
        $rootOnDrive = $drive + '\'
        if (Test-Path $rootOnDrive) { continue }
        $substCmd = 'subst ' + $drive + ' "' + $Root + '"'
        & cmd /c $substCmd | Out-Null
        if ($LASTEXITCODE -eq 0) {
            $mappedDrive = $drive
            $asciiRoot = $rootOnDrive
            break
        }
    }
    if (-not $mappedDrive) { throw "No free drive letter for a temporary subst mapping." }
    Write-Host "Mapped $mappedDrive -> $Root"
}

try {
    $build = if ($BuildDir) {
        if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $asciiRoot $BuildDir }
    } else {
        Join-Path $asciiRoot ('build\' + $Arch)
    }
    $cache = Join-Path $build 'CMakeCache.txt'
    if (Test-Path $cache) {
        $cacheLine = Select-String -Path $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' | Select-Object -First 1
        if ($cacheLine) {
            $cachedSource = $cacheLine.Matches[0].Groups[1].Value.Replace([char]47, [char]92).TrimEnd([char]92)
            $wantedSource = $asciiRoot.Replace([char]47, [char]92).TrimEnd([char]92)
            if ($cachedSource -ine $wantedSource) {
                Write-Host "Build cache belongs to another path; reconfiguring."
                Remove-Item -Recurse -Force $build
            }
        }
    }

    $buildArgs = @(
        '-S', $asciiRoot,
        '-B', $build,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        ("-DCMAKE_MAKE_PROGRAM=" + $ninja)
    )
    if ($RaylibRoot) { $buildArgs += ("-DRAYLIB_ROOT=" + $RaylibRoot) }
    $buildArgs += ("-DPHIPHYSICE_COPY_TO_ROOT=" + $(if ($NoCopyRoot) { 'OFF' } else { 'ON' }))

    Write-Host "Configuring..."
    & $cmake @buildArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

    # Ninja matches MSVC /showIncludes lines with an exact byte prefix. CMake's
    # detected prefix is decoded through the console code page and can end up
    # different from the bytes cl.exe actually writes, which silently drops all
    # header dependencies. Probe the compiler once and copy the real prefix.
    $rules = Join-Path $build 'CMakeFiles\rules.ninja'
    if (Test-Path $rules) {
        $probeSource = Join-Path $env:TEMP 'phiphysice_showincludes_probe.c'
        $probeOutput = Join-Path $env:TEMP 'phiphysice_showincludes_probe.txt'
        Set-Content -Path $probeSource -Encoding ASCII -Value '#include <stdio.h>'
        $probeCmd = 'cl /nologo /showIncludes /c "' + $probeSource + '" /FoNUL > "' +
                    $probeOutput + '" 2>&1'
        & cmd /c $probeCmd | Out-Null
        if (Test-Path $probeOutput) {
            $probe = [System.IO.File]::ReadAllBytes($probeOutput)
            $header = [System.Text.Encoding]::ASCII.GetBytes('stdio.h')
            $headerAt = -1
            for ($i = 0; $i -le $probe.Length - $header.Length; ++$i) {
                $match = $true
                for ($j = 0; $j -lt $header.Length; ++$j) {
                    if ($probe[$i + $j] -ne $header[$j]) { $match = $false; break }
                }
                if ($match) { $headerAt = $i; break }
            }
            if ($headerAt -ge 0) {
                $lineStart = $headerAt
                while ($lineStart -gt 0 -and $probe[$lineStart - 1] -ne 10) { --$lineStart }
                $driveAt = -1
                for ($i = $headerAt; $i -gt $lineStart; --$i) {
                    if ($probe[$i] -eq 58 -and $i + 1 -lt $probe.Length -and
                        $probe[$i + 1] -eq 92) {
                        $driveAt = $i - 1
                        break
                    }
                }
                if ($driveAt -gt $lineStart) {
                    $prefixEnd = $driveAt
                    while ($prefixEnd -gt $lineStart -and
                           ($probe[$prefixEnd - 1] -eq 32 -or $probe[$prefixEnd - 1] -eq 13)) {
                        --$prefixEnd
                    }
                    $actualPrefix = $probe[$lineStart..($prefixEnd - 1)]
                    $bytes = [System.IO.File]::ReadAllBytes($rules)
                    $needle = [System.Text.Encoding]::ASCII.GetBytes('msvc_deps_prefix = ')
                    $start = -1
                    for ($i = 0; $i -le $bytes.Length - $needle.Length; ++$i) {
                        $match = $true
                        for ($j = 0; $j -lt $needle.Length; ++$j) {
                            if ($bytes[$i + $j] -ne $needle[$j]) { $match = $false; break }
                        }
                        if ($match) { $start = $i; break }
                    }
                    if ($start -ge 0) {
                        $lineStart2 = $start + $needle.Length
                        $lineEnd2 = $lineStart2
                        while ($lineEnd2 -lt $bytes.Length -and $bytes[$lineEnd2] -ne 10) {
                            ++$lineEnd2
                        }
                        $merged = New-Object System.Collections.Generic.List[byte]
                        $merged.AddRange([byte[]]$bytes[0..($lineStart2 - 1)])
                        $merged.AddRange([byte[]]$actualPrefix)
                        if ($lineEnd2 -lt $bytes.Length) {
                            $merged.AddRange([byte[]]$bytes[$lineEnd2..($bytes.Length - 1)])
                        }
                        [System.IO.File]::WriteAllBytes($rules, $merged.ToArray())
                    }
                }
            }
        }
        Remove-Item $probeSource, $probeOutput -Force -ErrorAction SilentlyContinue
    }

    Write-Host "Building..."
    & $ninja -C $build
    if ($LASTEXITCODE -ne 0) { throw "Build failed." }

    $builtExe = Join-Path $build 'PhiPhysiceEditorNeo.exe'
    $exe = $builtExe
    if (-not $NoCopyRoot) {
        $exe = Join-Path $Root 'PhiPhysiceEditorNeo.exe'
        Copy-Item -Path $builtExe -Destination $exe -Force
    }
    if (-not $SkipTests) {
        Write-Host "Running self test..."
        $testOut = Join-Path $build 'selftest.log'
        $testErr = Join-Path $build 'selftest.err.log'
        # The executable uses the Windows GUI subsystem (no console window), so
        # PowerShell must explicitly wait for it and capture its redirected output.
        $test = Start-Process -FilePath $builtExe -ArgumentList '--selftest' -NoNewWindow -Wait -PassThru -RedirectStandardOutput $testOut -RedirectStandardError $testErr
        Get-Content $testOut -ErrorAction SilentlyContinue
        if ($test.ExitCode -ne 0) {
            Get-Content $testErr -ErrorAction SilentlyContinue
            throw "Self test failed."
        }
    }

    Write-Host ""
    Write-Host "Build complete: $exe"
    Write-Host "Run:  $exe"
    if ($Run) { Start-Process -FilePath $exe -WorkingDirectory $Root }
}
finally {
    if ($mappedDrive) {
        & cmd /c ('subst ' + $mappedDrive + ' /D') | Out-Null
        Write-Host "Removed drive mapping $mappedDrive"
    }
}
