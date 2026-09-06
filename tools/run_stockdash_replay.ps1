# Stock-dashboard replay harness (beads-xbox.1.14.1).
#
# Protocol, per docs/PERF-METHODOLOGY.md section 2:
#   * noperf is the DEFAULT. --perf-accounting costs ~17% and changes hot-loop
#     code (src/app/main.cpp:1077 enables detailed telemetry), so headline
#     numbers come from noperf runs; pass -PerfAccounting for attribution runs.
#   * With -Candidate, one invocation runs interleaved legs A1 B1 A2 B2 A3 B3 in
#     a single session. Every leg writes its own report JSON carrying its leg
#     index, arm label, exact argv, chunk size, exe SHA-256, git HEAD, and a
#     before/after census of foreign compiler/runner processes.
#   * Without -Candidate and without an explicit -Repetitions, the script keeps
#     its original single-run behavior and file naming.
#
# Examples:
#   .\tools\run_stockdash_replay.ps1 -Label baseline-noperf-12b
#   .\tools\run_stockdash_replay.ps1 -Label chunk-abba -Candidate .\build-cand\xlle.exe -Repetitions 3
#   .\tools\run_stockdash_replay.ps1 -Label attribution -PerfAccounting

param(
    [Parameter(Mandatory = $true)] [string] $Label,
    [ValidateSet('opengl', 'software')] [string] $Renderer = 'opengl',
    [UInt64] $MaxInsns = 12000000000,
    [UInt64] $FrameInterval = 500000000,
    [int] $MaxHostMemoryMb = 768,
    [UInt64] $MachineChunkInsns = 4096,
    [int] $GenericTop = 64,
    [string] $InputScript = 'tests\scripts\clock-accept.txt',
    # Re-pinned 2026-09-05: the copy-on-write HDD attach (beads-xbox.1.14.11) moved
    # the trajectory, and 876b58c2 was the pre-copy-on-write disk's answer -- it has
    # been unreachable since. This is the SHA of docs/PERF-METHODOLOGY.md section 3a's
    # golden, reproduced by every leg of the 2026-09-05 knee sweep at
    # --nv2a-draw-threads 8, 12 and 16 with the profile-hash folds off.
    [string] $ExpectedFinalSha = '36e87a97a1883e44115db2be6d03996baccd6eae2b8499cb1d52446d04b09dfe',
    # The repo carries its own reference identities in goldens/ (see goldens/README.md).
    # This is NOT defaulted to goldens\golden-noperf-defaults12-2026-09-05d.json on purpose:
    # a golden record also supplies the 24 expected per-profile output hashes, and those are
    # only emitted in the REFERENCE configuration (--nv2a-draw-threads 0 --nv2a-profile-hashes).
    # The shipping defaults run with the folds off (docs/PERF-METHODOLOGY.md section 3b), so
    # defaulting this would make every default-configuration leg fail on missing hashes rather
    # than on a real difference. Pass it explicitly on reference legs:
    #   -GoldenRecord goldens\golden-noperf-defaults12-2026-09-05d.json `
    #     -ExtraXlleArgs @('--nv2a-draw-threads','0','--nv2a-profile-hashes')
    # Default-configuration legs are gated on $ExpectedFinalSha above, which every arm of every
    # 2026-09-05 measured set reproduced.
    [string] $GoldenRecord = '',
    [string] $ExpectedProfileHashes = '',
    [string] $BaselineReport = '',
    [double] $MinFinalFpsGainPct = 0.0,
    [double] $MaxFocusWallRegressionPct = -1.0,
    [string[]] $ExtraXlleArgs = @(),
    # Per-arm extra arguments (beads-xbox.1.15.5). $ExtraXlleArgs still applies to
    # BOTH arms; these append after it, arm A first, arm B second. This is what
    # makes a SAME-BINARY A/B possible -- the toggle-on / toggle-off byte-lock and
    # timing of a runtime flag, where using two different exes would confound the
    # flag with the build. Both are recorded verbatim in each leg's .argv.json.
    [string[]] $ExtraXlleArgsA = @(),
    [string[]] $ExtraXlleArgsB = @(),
    [switch] $Build,
    [switch] $Force,
    # noperf is the default; -PerfAccounting opts in. -NoPerfAccounting is kept
    # as an accepted no-op so existing call sites and scripts do not break.
    [switch] $PerfAccounting,
    [switch] $NoPerfAccounting,
    [switch] $NoParse,
    [string] $Engine = 'unicorn-2.1.1',
    [string] $BaselineExe = 'build-release\xlle.exe',
    [string] $Candidate = '',
    [int] $Repetitions = 3,
    [double] $MinImprovementPct = 5.0,
    [switch] $RequireQuiescence,
    [switch] $RequireSameProfileHashes,
    # Retry WHOLE contaminated pairs, within this invocation/session. All attempted
    # legs remain on disk; selection uses only the process census, never wall time.
    [switch] $RetryContaminatedPairs,
    [ValidateRange(1, 100)] [int] $MaxPairAttempts = 12,
    [string] $QuiescencePattern = '^(xlle|g\+\+|cc1plus|ld|collect2|cmake|ninja|python|pythonw|py)([-._0-9]|$)'
)

$ErrorActionPreference = 'Stop'

if ($PerfAccounting -and $NoPerfAccounting) {
    throw "-PerfAccounting and -NoPerfAccounting are mutually exclusive"
}
if ($Repetitions -lt 1) {
    throw "-Repetitions must be >= 1"
}

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

    $sessionId = '{0}-{1}' -f $Label, (Get-Date -Format 'yyyyMMddTHHmmss')
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)

    function Write-JsonFile {
        param([string] $Path, $Value)
        $json = ConvertTo-Json -InputObject $Value -Depth 8
        [System.IO.File]::WriteAllText($Path, $json, $utf8NoBom)
    }

    # ---- host quiescence -------------------------------------------------
    # PERF-METHODOLOGY section 2 rule 3: a leg is only quiet if no compiler or
    # other runner was live before or after it. The census is recorded either
    # way; it is never used to silently discard a leg.
    function Get-ProcessCensus {
        param([int[]] $ExcludePids = @())
        $found = @()
        foreach ($process in (Get-Process -ErrorAction SilentlyContinue)) {
            if ($process.Id -eq $PID) { continue }
            if ($ExcludePids -contains $process.Id) { continue }
            if ($process.ProcessName -notmatch $QuiescencePattern) { continue }
            $processPath = $null
            try { $processPath = $process.Path } catch { $processPath = $null }
            $startTime = $null
            try { $startTime = $process.StartTime.ToString('o') } catch { $startTime = $null }
            $found += [pscustomobject]@{
                pid       = $process.Id
                name      = $process.ProcessName
                path      = $processPath
                startTime = $startTime
            }
        }
        return , $found
    }

    function Get-CpuSnapshot {
        try {
            $cpu = Get-CimInstance -ClassName Win32_Processor -ErrorAction Stop |
                Select-Object -First 1
            return [pscustomobject]@{
                name                 = $cpu.Name
                numberOfCores        = $cpu.NumberOfCores
                numberOfLogicalCores = $cpu.NumberOfLogicalProcessors
                maxClockSpeedMhz     = $cpu.MaxClockSpeed
                currentClockSpeedMhz = $cpu.CurrentClockSpeed
                loadPercentage       = $cpu.LoadPercentage
            }
        } catch {
            return $null
        }
    }

    function New-XlleArgs {
        param([string] $Prefix, [string] $FinalPpm, [string] $Arm = 'A')
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
            '--test-input-capture-prefix', $Prefix,
            '--max-host-memory-mb', "$MaxHostMemoryMb",
            '--machine-chunk-insns', "$MachineChunkInsns",
            '--frame-series', $Prefix, "$FrameInterval"
        )
        if ($PerfAccounting) {
            $commandArgs += @('--perf-accounting', '--perf-generic-top', "$GenericTop")
        }
        $commandArgs += @(
            '--max-insns', "$MaxInsns",
            '--frame-dump', $FinalPpm
        )
        if ($ExtraXlleArgs.Count) {
            $commandArgs += $ExtraXlleArgs
        }
        if ($Arm -eq 'A' -and $ExtraXlleArgsA.Count) {
            $commandArgs += $ExtraXlleArgsA
        }
        if ($Arm -eq 'B' -and $ExtraXlleArgsB.Count) {
            $commandArgs += $ExtraXlleArgsB
        }
        return , $commandArgs
    }

    function Get-QuotedCommandText {
        param([string[]] $CommandArgs)
        $quoted = $CommandArgs | ForEach-Object {
            if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ }
        }
        return ($quoted -join ' ')
    }

    # ---- one leg ----------------------------------------------------------
    function Invoke-ReplayLeg {
        param(
            [string] $Prefix,
            [string] $Exe,
            [string] $Arm,
            [int] $LegIndex,
            [int] $Repetition,
            [string] $LegName
        )

        $stdout = "$Prefix.stdout.log"
        $stderr = "$Prefix.stderr.log"
        $finalPpm = "$Prefix-final.ppm"
        $report = "$Prefix.report.json"
        $argvJson = "$Prefix.argv.json"
        $quiescenceJson = "$Prefix.quiescence.json"

        $ownedOutputs = @($stdout, $stderr, $finalPpm, $report, $argvJson, $quiescenceJson)
        $existing = @($ownedOutputs | Where-Object { Test-Path -LiteralPath $_ })
        if ($existing.Count -and -not $Force) {
            throw "output exists; pass -Force to overwrite: $($existing -join ', ')"
        }
        if ($Force) {
            foreach ($path in $ownedOutputs) {
                if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
            }
        }

        if (-not (Test-Path -LiteralPath $Exe)) {
            throw "missing exe for arm ${Arm}: $Exe"
        }
        $exePath = (Resolve-Path -LiteralPath $Exe).Path

        $commandArgs = New-XlleArgs -Prefix $Prefix -FinalPpm $finalPpm -Arm $Arm
        $commandText = "$exePath $(Get-QuotedCommandText -CommandArgs $commandArgs)"
        Write-JsonFile -Path $argvJson -Value (@($exePath) + $commandArgs)
        Write-Host "leg $LegIndex arm=$Arm rep=$Repetition"
        Write-Host $commandText

        $before = Get-ProcessCensus
        $cpuBefore = Get-CpuSnapshot

        & $exePath @commandArgs 1> $stdout 2> $stderr
        $exitCode = $LASTEXITCODE

        $after = Get-ProcessCensus
        $cpuAfter = Get-CpuSnapshot

        $foreign = @()
        $foreign += @($before)
        $foreign += @($after)
        $contaminated = ($foreign.Count -gt 0)
        $quiescence = [pscustomobject]@{
            pattern           = $QuiescencePattern
            harness_pid       = $PID
            before            = @($before)
            after             = @($after)
            foreign_processes = @($foreign | ForEach-Object { "$($_.name):$($_.pid)" } | Select-Object -Unique)
            contaminated      = $contaminated
            cpu_before        = $cpuBefore
            cpu_after         = $cpuAfter
        }
        Write-JsonFile -Path $quiescenceJson -Value $quiescence

        Write-Host "xlle exit=$exitCode stdout=$stdout stderr=$stderr final=$finalPpm contaminated=$contaminated"
        if ($contaminated) {
            Write-Warning "leg $LegIndex ran with foreign compiler/runner processes present: $($quiescence.foreign_processes -join ', ')"
        }
        if ($exitCode -ne 0) {
            throw "xlle failed with exit code $exitCode"
        }

        if ($NoParse) { return $null }

        $parseArgs = @(
            'tools\measure_xbox_replay.py',
            '--name', $LegName,
            '--stdout', $stdout,
            '--stderr', $stderr,
            '--frame-dump', $finalPpm,
            '--exe', $exePath,
            '--command', $commandText,
            '--report', $report,
            '--require-empty-stderr',
            '--argv-json', $argvJson,
            '--chunk-insns', "$MachineChunkInsns",
            '--engine', $Engine,
            '--leg-index', "$LegIndex",
            '--arm', $Arm,
            '--repetition', "$Repetition",
            '--session', $sessionId,
            '--quiescence-json', $quiescenceJson
        )
        if ($PerfAccounting) {
            $parseArgs += '--perf-accounting'
        } else {
            $parseArgs += '--no-perf-accounting'
        }
        if ($GoldenRecord) {
            $parseArgs += @('--golden', $GoldenRecord)
        } elseif ($ExpectedFinalSha) {
            $parseArgs += @('--expect-final-sha', $ExpectedFinalSha)
        }
        if ($ExpectedProfileHashes) {
            $parseArgs += @('--expected-profile-hashes', $ExpectedProfileHashes)
        }

        # This function's return value is the report path. A bare native call
        # would fold every line of Python output into the pipeline and therefore
        # into the return value, so capture it and re-emit on the real stdout
        # handle (which stays redirectable, unlike Write-Host/Out-Host).
        $parseOutput = & py -3 @parseArgs
        $parseExit = $LASTEXITCODE
        foreach ($line in $parseOutput) { [Console]::Out.WriteLine($line) }
        if ($parseExit -ne 0) {
            throw "replay report checks failed with exit code $parseExit"
        }
        $summaryOutput = & py -3 tools\summarize_xbox_report.py $report
        $summaryExit = $LASTEXITCODE
        foreach ($line in $summaryOutput) { [Console]::Out.WriteLine($line) }
        if ($summaryExit -ne 0) {
            throw "summary failed with exit code $summaryExit"
        }
        return $report
    }

    # ---- leg plan ---------------------------------------------------------
    $explicitRepetitions = $PSBoundParameters.ContainsKey('Repetitions')
    # A same-binary A/B (one exe, two flag sets) is a legitimate and stronger
    # experiment than two exes: it cannot confound the change with the build.
    $twoArm = [bool] $Candidate -or ($ExtraXlleArgsA.Count -gt 0) -or ($ExtraXlleArgsB.Count -gt 0)
    if ($twoArm -and -not $Candidate) { $Candidate = $BaselineExe }
    $singleRun = (-not $twoArm) -and (-not $explicitRepetitions)
    if ($RetryContaminatedPairs -and (-not $twoArm -or $NoParse)) {
        throw '-RetryContaminatedPairs requires two parsed arms'
    }
    if ($RetryContaminatedPairs -and $MaxPairAttempts -lt $Repetitions) {
        throw '-MaxPairAttempts must be at least -Repetitions'
    }

    $armAReports = @()
    $armBReports = @()

    if ($singleRun) {
        # Backward-compatible path: identical file naming to the original script.
        $prefix = Join-Path $buildDir $Label
        $report = Invoke-ReplayLeg -Prefix $prefix -Exe $BaselineExe -Arm 'A' `
            -LegIndex 0 -Repetition 1 -LegName $Label
        if ($report) { $armAReports += $report }
    } else {
        $legIndex = 0
        $pairAttempts = @()
        for ($rep = 1; $rep -le $Repetitions; $rep++) {
            if ($RetryContaminatedPairs -and $pairAttempts.Count -ge $MaxPairAttempts) {
                throw "only $($rep - 1) clean pairs after $MaxPairAttempts attempts; no timing gate"
            }
            $legName = '{0}-leg{1:d2}-A-r{2}' -f $Label, $legIndex, $rep
            $prefixA = Join-Path $buildDir $legName
            $report = Invoke-ReplayLeg -Prefix (Join-Path $buildDir $legName) `
                -Exe $BaselineExe -Arm 'A' -LegIndex $legIndex -Repetition $rep -LegName $legName
            $reportA = $report
            $legIndex++

            if ($twoArm) {
                $legName = '{0}-leg{1:d2}-B-r{2}' -f $Label, $legIndex, $rep
                $prefixB = Join-Path $buildDir $legName
                $report = Invoke-ReplayLeg -Prefix (Join-Path $buildDir $legName) `
                    -Exe $Candidate -Arm 'B' -LegIndex $legIndex -Repetition $rep -LegName $legName
                if ($RetryContaminatedPairs) {
                    $censusA = Get-Content -Raw -LiteralPath "$prefixA.quiescence.json" | ConvertFrom-Json
                    $censusB = Get-Content -Raw -LiteralPath "$prefixB.quiescence.json" | ConvertFrom-Json
                    $acceptPair = -not ($censusA.contaminated -or $censusB.contaminated)
                    $pairAttempts += [pscustomobject]@{
                        attempt = $pairAttempts.Count + 1
                        repetition = $rep
                        arm_a = $reportA
                        arm_b = $report
                        accepted = $acceptPair
                        reason = $(if ($acceptPair) { 'both process censuses clean' }
                                   else { 'foreign process in at least one leg; whole pair excluded' })
                    }
                    Write-JsonFile -Path (Join-Path $buildDir "$Label-pair-selection.json") `
                        -Value ([pscustomobject]@{ session = $sessionId; attempts = $pairAttempts })
                    if (-not $acceptPair) {
                        Write-Warning "pair attempt $($pairAttempts.Count) contaminated; archived both legs, retrying repetition $rep"
                        $legIndex++
                        $rep--
                        continue
                    }
                }
                if ($report) { $armBReports += $report }
                $legIndex++
            }
            if ($reportA) { $armAReports += $reportA }
        }
    }

    if ($NoParse) { return }

    # ---- gates ------------------------------------------------------------
    if ($twoArm) {
        $setSummary = Join-Path $buildDir "$Label-abba-summary.json"
        $setGate = Join-Path $buildDir "$Label-abba-gate.json"

        $summarizeArgs = @(
            'tools\summarize_xbox_report_set.py',
            '--arm-a'
        ) + $armAReports + @('--arm-b') + $armBReports + @('--json', $setSummary)
        & py -3 @summarizeArgs
        if ($LASTEXITCODE -ne 0) {
            throw "two-arm summary failed with exit code $LASTEXITCODE"
        }

        $gateArgs = @(
            'tools\compare_xbox_reports.py',
            '--set-a'
        ) + $armAReports + @('--set-b') + $armBReports + @(
            '--min-improvement-pct', "$MinImprovementPct",
            '--json', $setGate
        )
        if ($RequireQuiescence) { $gateArgs += '--require-quiescence' }
        if ($RequireSameProfileHashes) { $gateArgs += '--require-same-profile-hashes' }
        & py -3 @gateArgs
        if ($LASTEXITCODE -ne 0) {
            throw "set-vs-set retention gate rejected with exit code $LASTEXITCODE"
        }
    } elseif ($armAReports.Count -gt 1) {
        $setSummary = Join-Path $buildDir "$Label-set-summary.json"
        $summarizeArgs = @('tools\summarize_xbox_report_set.py') + $armAReports +
            @('--require-same-final-sha', '--require-empty-stderr')
        & py -3 @summarizeArgs
        if ($LASTEXITCODE -ne 0) {
            throw "report-set summary failed with exit code $LASTEXITCODE"
        }
    }

    if ($BaselineReport -and $armAReports.Count -eq 1 -and -not $twoArm) {
        $compareArgs = @(
            'tools\compare_xbox_reports.py',
            $BaselineReport,
            $armAReports[0],
            '--require-same-final-sha'
        )
        if ($MinFinalFpsGainPct -gt 0.0) {
            $compareArgs += @('--min-final-fps-gain-pct', "$MinFinalFpsGainPct")
        }
        if ($MaxFocusWallRegressionPct -ge 0.0) {
            $compareArgs += @('--max-focus-wall-regression-pct', "$MaxFocusWallRegressionPct")
        }
        if ($RequireSameProfileHashes) { $compareArgs += '--require-same-profile-hashes' }
        & py -3 @compareArgs
        if ($LASTEXITCODE -ne 0) {
            throw "replay comparison checks failed with exit code $LASTEXITCODE"
        }
    }
}
finally {
    Pop-Location
}
