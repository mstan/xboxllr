# Exercise the actual harness orchestration with deterministic process censuses.
# No emulator, compiler, firmware, or timed run is involved.
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$source = [IO.File]::ReadAllText((Join-Path $repo 'tools/run_stockdash_replay.ps1'))
$start = $source.IndexOf('    $armAReports = @()')
$end = $source.IndexOf('    if ($NoParse) { return }', $start)
if ($start -lt 0 -or $end -le $start) { throw 'harness orchestration not found' }
$orchestrate = [scriptblock]::Create($source.Substring($start, $end - $start))
$buildDir = Join-Path ([IO.Path]::GetTempPath()) ('xlle-pair-test-' + [guid]::NewGuid())
[IO.Directory]::CreateDirectory($buildDir) | Out-Null
$createdPaths = [Collections.Generic.List[string]]::new()
try {
    function Write-JsonFile($Path, $Value) {
        [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 8))
        $createdPaths.Add($Path)
    }
    function Invoke-ReplayLeg($Prefix, $Exe, $Arm, $LegIndex, $Repetition, $LegName) {
        if ($LegIndex -eq $failLeg) { throw 'injected correctness failure' }
        $script:calls += "$LegIndex/$Arm/$Repetition"
        Write-JsonFile "$Prefix.quiescence.json" ([pscustomobject]@{
            contaminated = ($contaminatedLegs -contains $LegIndex)
        })
        return "$Prefix.report.json"
    }
    $singleRun = $false
    $twoArm = $true
    $Repetitions = 2
    $BaselineExe = 'unused-A'
    $Candidate = 'unused-B'
    $sessionId = 'one-session'
    $RetryContaminatedPairs = $true
    $MaxPairAttempts = 3
    $failLeg = -1
    $contaminatedLegs = @(0)  # reject BOTH legs of pair 1, even though B is clean
    $Label = 'retry'
    $script:calls = @()
    . $orchestrate
    if (($script:calls -join ',') -ne '0/A/1,1/B/1,2/A/1,3/B/1,4/A/2,5/B/2') {
        throw "wrong retry order: $script:calls"
    }
    if ($armAReports.Count -ne 2 -or $armBReports.Count -ne 2 -or
        $armAReports[0] -notlike '*leg02-A-r1*' -or $armBReports[0] -notlike '*leg03-B-r1*') {
        throw 'contaminated pair or clean mate leaked into selected reports'
    }
    $selection = Get-Content -Raw "$buildDir/$Label-pair-selection.json" | ConvertFrom-Json
    if ($selection.session -ne $sessionId -or $selection.attempts.Count -ne 3 -or
        $selection.attempts[0].accepted -or !$selection.attempts[1].accepted) {
        throw 'selection audit is incomplete'
    }
    $Label = 'limit'
    $MaxPairAttempts = 1
    $caught = $false
    try { . $orchestrate } catch { $caught = $_ -like '*clean pairs after*' }
    if (!$caught) { throw 'attempt limit did not stop without a gate' }
    $Label = 'fault'
    $MaxPairAttempts = 3
    $failLeg = 0
    $caught = $false
    try { . $orchestrate } catch { $caught = $_ -like '*injected correctness failure*' }
    if (!$caught) { throw 'correctness failure was retried or swallowed' }
    $Label = 'legacy'
    $failLeg = -1
    $RetryContaminatedPairs = $false
    $script:calls = @()
    . $orchestrate
    if (($script:calls -join ',') -ne '0/A/1,1/B/1,2/A/2,3/B/2' -or
        $armAReports.Count -ne 2 -or $armBReports.Count -ne 2) {
        throw 'ordinary interleaving changed'
    }
    Write-Host 'test_replay_pair_retry: PASS (retry, whole-pair exclusion, audit, limit, fault, legacy order)'
} finally {
    # Delete only the exact test-created files, then the now-empty directory.
    foreach ($path in ($createdPaths | Select-Object -Unique)) {
        Remove-Item -LiteralPath $path -Force
    }
    Remove-Item -LiteralPath $buildDir
}
