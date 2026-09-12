# Shared host observer/launcher. Dot-source and initialize before timed legs.
function Initialize-ReplayObserver {
    if (-not ('Xlle.Replay.ProcessObserver' -as [type])) {
        Add-Type -Path (Join-Path $PSScriptRoot 'replay_process_observer.cs') -ErrorAction Stop
    }
}

function New-ReplayObserver {
    param([string] $Pattern, [int] $IntervalMs, [double] $AllowedGapMs, [int[]] $ResidentPids,
          [object[]] $ResidentIdentities = @())
    $observer = [Xlle.Replay.ProcessObserver]::new($Pattern, $IntervalMs, $AllowedGapMs)
    try {
        foreach ($residentPid in $ResidentPids) { $observer.AddResident($residentPid) }
        foreach ($identity in $ResidentIdentities) { $observer.AddResidentIdentity($identity) }
        return $observer
    } catch { $observer.Dispose(); throw }
}

function Get-QuotedCommandText {
    param([string[]] $CommandArgs)
    return [Xlle.Replay.ProcessObserver]::QuoteArguments($CommandArgs)
}

function Test-ReplayTimingUsable {
    param($Record)
    $o = $Record.observer
    if ($Record.contaminated -isnot [bool] -or $Record.contaminated -or !$o) { return $false }
    if ($o.schema -ne 'xboxlle.process-observer.v1' -or $o.mode -ne 'sampled') { return $false }
    foreach ($field in @('started','completed','healthy','covered_leg')) {
        if ($o.$field -isnot [bool] -or !$o.$field) { return $false }
    }
    if ($o.degraded -isnot [bool] -or $o.degraded -or $o.errors -isnot [array] -or $o.errors.Count) { return $false }
    if (($o.samples -isnot [int] -and $o.samples -isnot [long]) -or $o.samples -lt 2 -or
        ($o.interval_ms -isnot [int] -and $o.interval_ms -isnot [long]) -or $o.interval_ms -le 0) { return $false }
    foreach ($field in @('max_gap_ms','allowed_gap_ms')) {
        $v = $o.$field
        if ($v -isnot [int] -and $v -isnot [long] -and $v -isnot [double] -and $v -isnot [decimal]) { return $false }
        if ([double]::IsNaN($v) -or [double]::IsInfinity($v)) { return $false }
    }
    return $o.allowed_gap_ms -gt 0 -and $o.max_gap_ms -ge 0 -and $o.max_gap_ms -le $o.allowed_gap_ms
}

function Invoke-ObservedProcess {
    param([string] $Exe, [string[]] $CommandArgs, [string] $Stdout, [string] $Stderr,
          [string] $Pattern, [int] $IntervalMs, [double] $AllowedGapMs,
          [int[]] $ResidentPids = @(), [object[]] $ResidentIdentities = @(), [switch] $RequireQuiescence)
    $observer = $null
    $evidence = $null
    $monitorError = $null
    $launchError = $null
    $reason = $null
    $before = @()
    $process = $null
    $outFile = $null
    $errFile = $null
    $processStarted = $false
    $exited = $false
    $readyAtStart = $false
    $ownedRegistered = $false
    $exitCode = $null
    try {
        try {
            $observer = New-ReplayObserver -Pattern $Pattern -IntervalMs $IntervalMs -AllowedGapMs $AllowedGapMs `
                -ResidentPids $ResidentPids -ResidentIdentities $ResidentIdentities
            $observer.Start()
            $readyAtStart = $observer.Ready
            $before = @($observer.Before)
            if (!$readyAtStart) { throw 'process observer did not become ready' }
        } catch { $monitorError = $_.Exception.Message }
        if ($RequireQuiescence -and $monitorError) { $reason = 'observer_unavailable' }
        elseif ($RequireQuiescence -and $before.Count) { $reason = 'busy_before_launch' }
        else {
            $outFile = [IO.File]::Open($Stdout, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::Read)
            $errFile = [IO.File]::Open($Stderr, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::Read)
            $info = [Diagnostics.ProcessStartInfo]::new()
            $info.FileName = $Exe
            $info.WorkingDirectory = (Get-Location).ProviderPath
            $info.Arguments = Get-QuotedCommandText -CommandArgs $CommandArgs
            $info.UseShellExecute = $false
            $info.CreateNoWindow = $true
            $info.RedirectStandardOutput = $true
            $info.RedirectStandardError = $true
            $process = [Diagnostics.Process]::new()
            $process.StartInfo = $info
            if (!$process.Start()) { throw 'process did not start' }
            $processStarted = $true
            # Start both drains before waiting: neither pipe can block the child.
            $outTask = $process.StandardOutput.BaseStream.CopyToAsync($outFile)
            $errTask = $process.StandardError.BaseStream.CopyToAsync($errFile)
            if ($observer) {
                try { $observer.AddOwned($process); $ownedRegistered = $true }
                catch { $monitorError = $_.Exception.Message }
            }
            $process.WaitForExit()
            $exited = $true
            [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($outTask,$errTask))
            $exitCode = $process.ExitCode
        }
    } catch {
        $launchError = $_.Exception.Message
        # Only the exact child launched above may be stopped on a launcher/I/O
        # failure. Never stop a process found by the host observer.
        if ($processStarted -and !$exited) {
            if (!$process.HasExited) { $process.Kill() }
            $process.WaitForExit()
        }
    } finally {
        if ($observer) {
            try { $evidence = $observer.Finish($processStarted -and $exited -and $readyAtStart -and $ownedRegistered) }
            catch { $monitorError = $_.Exception.Message }
            $observer.Dispose()
        }
        if ($outFile) { $outFile.Dispose() }
        if ($errFile) { $errFile.Dispose() }
        if ($process) { $process.Dispose() }
    }
    if (!$evidence) {
        $evidence = [pscustomobject]@{
            schema='xboxlle.process-observer.v1'; mode='sampled'; started=$false
            completed=$false; healthy=$false; covered_leg=$false; degraded=$true
            errors=@(); samples=0; interval_ms=$IntervalMs; allowed_gap_ms=$AllowedGapMs
            max_gap_ms=0; sample_work_ms=0; observed_ms=0; started_at=$null; stopped_at=$null
            before=@(); after=@(); during=@(); owned=@(); residents=@()
        }
    }
    if ($monitorError) {
        $evidence.healthy = $false
        $evidence.errors = @($evidence.errors) + @($monitorError)
    }
    $foreign = @($evidence.during)
    $record = [pscustomobject]@{
        pattern=$Pattern; harness_pid=$PID; before=@($evidence.before); after=@($evidence.after)
        during=$foreign; foreign_processes=@($foreign | ForEach-Object { "$($_.name):$($_.pid)" } | Select-Object -Unique)
        contaminated=($foreign.Count -gt 0); observer=$evidence; emulator_started=$processStarted
        reason=$reason; launch_error=$launchError
    }
    $record | Add-Member -NotePropertyName timing_usable -NotePropertyValue (Test-ReplayTimingUsable $record)
    return [pscustomobject]@{ exit_code=$exitCode; quiescence=$record }
}
