param(
    [Parameter(Mandatory = $true)] [string] $Label,
    [ValidateSet('opengl', 'software')] [string] $Renderer = 'opengl',
    [UInt64] $MaxInsns = 12000000000,
    [UInt64] $FrameInterval = 500000000,
    [int] $MaxHostMemoryMb = 768,
    [UInt64] $MachineChunkInsns = 4096,
    [int] $GenericTop = 64,
    [string] $InputScript = 'tests\scripts\clock-accept.txt',
    [string] $ExpectedFinalSha = '876b58c2d0e6c63fec917c73dcc43a7d09983baf1da2b5b2176fa73a1b44da6d',
    [string] $BaselineReport = '',
    [double] $MinFinalFpsGainPct = 0.0,
    [double] $MaxFocusWallRegressionPct = -1.0,
    [string[]] $ExtraXlleArgs = @(),
    [switch] $Build,
    [switch] $Force,
    [switch] $NoPerfAccounting,
    [switch] $NoParse
)

$ErrorActionPreference = 'Stop'

$repo = Resolve-Path (Join-Path $PSScriptRoot '..')
Push-Location $repo
try {
    $cachePath = Join-Path $repo 'build-release\CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath)) {
        throw "missing build-release CMake cache; configure Release build first"
    }
    $cache = Get-Content -LiteralPath $cachePath
    $requiredCacheLines = @(
        'CMAKE_BUILD_TYPE:STRING=Release',
        'CMAKE_C_FLAGS_RELEASE:STRING=-O3 -DNDEBUG',
        'CMAKE_CXX_FLAGS_RELEASE:STRING=-O3 -DNDEBUG',
        'XLLE_NATIVE_OPTIMIZATIONS:BOOL=ON'
    )
    foreach ($required in $requiredCacheLines) {
        if (-not ($cache -contains $required)) {
            throw "refusing replay with non-optimized build cache; missing '$required'"
        }
    }
    if ($cache -contains 'XLLE_LTO_OPTIMIZATIONS:BOOL=ON' -or
        $cache -contains 'XLLE_LTO_OPTIMIZATIONS:UNINITIALIZED=ON') {
        throw "refusing replay with XLLE_LTO_OPTIMIZATIONS=ON; broad LTO is not a valid baseline"
    }

    if ($Build) {
        cmake --build build-release --target xlle test_nv2a --config Release
        if ($LASTEXITCODE -ne 0) {
            throw "build failed with exit code $LASTEXITCODE"
        }
    }

    $buildDir = Join-Path $repo 'build'
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

    $prefix = Join-Path $buildDir $Label
    $stdout = "$prefix.stdout.log"
    $stderr = "$prefix.stderr.log"
    $finalPpm = "$prefix-final.ppm"
    $report = "$prefix.report.json"

    $ownedOutputs = @($stdout, $stderr, $finalPpm, $report)
    $existing = @($ownedOutputs | Where-Object { Test-Path -LiteralPath $_ })
    if ($existing.Count -and -not $Force) {
        throw "output exists; pass -Force to overwrite: $($existing -join ', ')"
    }
    if ($Force) {
        foreach ($path in $ownedOutputs) {
            if (Test-Path -LiteralPath $path) {
                Remove-Item -LiteralPath $path -Force
            }
        }
    }

    $commandArgs = @(
        '--nv2a-renderer', $Renderer,
        '--boot', '..\machine\flash-dump-ff000000.bin',
        '--run2bl', '..\machine\bios-extract\bldr.bin',
        '--hdd', '..\machine\xbox-hdd-stockdash.img',
        '--eeprom', '..\machine\eeprom.bin',
        '--net-disconnected',
        '--reduced-retire-diag',
        '--test-synthetic-controller-hub',
        '--test-input-script', $InputScript,
        '--test-input-capture-prefix', $prefix,
        '--max-host-memory-mb', "$MaxHostMemoryMb",
        '--machine-chunk-insns', "$MachineChunkInsns",
        '--frame-series', $prefix, "$FrameInterval",
        '--max-insns', "$MaxInsns",
        '--frame-dump', $finalPpm
    )
    if (-not $NoPerfAccounting) {
        $commandArgs = @(
            $commandArgs[0..($commandArgs.Count - 5)] +
            @('--perf-accounting', '--perf-generic-top', "$GenericTop") +
            $commandArgs[($commandArgs.Count - 4)..($commandArgs.Count - 1)]
        )
    }
    if ($ExtraXlleArgs.Count) {
        $commandArgs += $ExtraXlleArgs
    }

    $quotedArgs = $commandArgs | ForEach-Object {
        if ($_ -match '[\s"]') {
            '"' + ($_ -replace '"', '\"') + '"'
        } else {
            $_
        }
    }
    $commandText = ".\build-release\xlle.exe $($quotedArgs -join ' ')"
    Write-Host $commandText

    & .\build-release\xlle.exe @commandArgs 1> $stdout 2> $stderr
    $exitCode = $LASTEXITCODE
    Write-Host "xlle exit=$exitCode stdout=$stdout stderr=$stderr final=$finalPpm"
    if ($exitCode -ne 0) {
        throw "xlle failed with exit code $exitCode"
    }

    if (-not $NoParse) {
        $parseArgs = @(
            'tools\measure_xbox_replay.py',
            '--name', $Label,
            '--stdout', $stdout,
            '--stderr', $stderr,
            '--frame-dump', $finalPpm,
            '--exe', 'build-release\xlle.exe',
            '--command', $commandText,
            '--report', $report,
            '--require-empty-stderr'
        )
        if ($ExpectedFinalSha) {
            $parseArgs += @('--expect-final-sha', $ExpectedFinalSha)
        }
        & py -3 @parseArgs
        if ($LASTEXITCODE -ne 0) {
            throw "replay report checks failed with exit code $LASTEXITCODE"
        }
        & py -3 tools\summarize_xbox_report.py $report
        if ($LASTEXITCODE -ne 0) {
            throw "summary failed with exit code $LASTEXITCODE"
        }
        if ($BaselineReport) {
            $compareArgs = @(
                'tools\compare_xbox_reports.py',
                $BaselineReport,
                $report,
                '--require-same-final-sha'
            )
            if ($MinFinalFpsGainPct -gt 0.0) {
                $compareArgs += @('--min-final-fps-gain-pct', "$MinFinalFpsGainPct")
            }
            if ($MaxFocusWallRegressionPct -ge 0.0) {
                $compareArgs += @('--max-focus-wall-regression-pct', "$MaxFocusWallRegressionPct")
            }
            & py -3 @compareArgs
            if ($LASTEXITCODE -ne 0) {
                throw "replay comparison checks failed with exit code $LASTEXITCODE"
            }
        }
    }
}
finally {
    Pop-Location
}
