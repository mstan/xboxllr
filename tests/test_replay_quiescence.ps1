$ErrorActionPreference = 'Stop'

$repo = Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'tools/replay_quiescence.ps1')
Initialize-ReplayObserver

function Assert-True($Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Eq($Actual, $Expected, [string] $Message) {
    if ($Actual -ne $Expected) {
        throw "$Message (expected '$Expected', got '$Actual')"
    }
}

function New-LogPair([string] $Root, [string] $Name) {
    return [pscustomobject]@{
        stdout = Join-Path $Root "$Name.stdout.log"
        stderr = Join-Path $Root "$Name.stderr.log"
    }
}

function Start-FixtureProcess([string] $Exe, [string[]] $CommandArgs) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Exe
    $info.Arguments = Get-QuotedCommandText -CommandArgs $CommandArgs
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { throw 'fixture process did not start' }
    return $process
}

function Stop-FixtureProcess($Process) {
    if ($null -eq $Process) { return }
    try {
        if (-not $Process.HasExited) {
            $Process.Kill()
            $Process.WaitForExit()
        }
    } finally {
        $Process.Dispose()
    }
}

$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('xlle-quiescence-' + [guid]::NewGuid())
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
$resident = $null
$busy = $null

try {
    $fixtureExe = Join-Path $testRoot 'xlle-quiescence-fixture.exe'
    $fixtureSource = @'
using System;
using System.Diagnostics;
using System.Text;
using System.Threading;

public static class Program {
    static int Main(string[] args) {
        string mode = args.Length == 0 ? "" : args[0];
        if (mode == "sleep") {
            Thread.Sleep(Int32.Parse(args[1]));
            return 0;
        }
        if (mode == "spawn-competitor") {
            string self = Process.GetCurrentProcess().MainModule.FileName;
            var info = new ProcessStartInfo(self, "sleep " + args[1]);
            info.UseShellExecute = false;
            info.CreateNoWindow = true;
            using (Process child = Process.Start(info)) {
                Thread.Sleep(Int32.Parse(args[2]));
                child.WaitForExit();
            }
            return 0;
        }
        if (mode == "echoargs") {
            int exitCode = Int32.Parse(args[1]);
            Console.Error.WriteLine("ERR:" + (args.Length - 2).ToString());
            for (int i = 2; i < args.Length; ++i) {
                string encoded = Convert.ToBase64String(Encoding.UTF8.GetBytes(args[i]));
                Console.WriteLine("ARG:" + encoded);
            }
            return exitCode;
        }
        if (mode == "pipes") {
            string line = new string('x', 4096);
            for (int i = 0; i < 256; ++i) {
                Console.Write(line);
                Console.Error.Write(line);
            }
            return 0;
        }
        if (mode == "cwd") { Console.Write(Environment.CurrentDirectory); return 0; }
        Console.Error.WriteLine("unknown mode: " + mode);
        return 90;
    }
}
'@
    Add-Type -TypeDefinition $fixtureSource -OutputAssembly $fixtureExe -OutputType ConsoleApplication

    $pattern = 'xlle-quiescence-fixture(\.vshost)?$'
    $intervalMs = 50
    $allowedGapMs = 5000

    $logs = New-LogPair $testRoot 'clean'
    $result = Invoke-ObservedProcess -Exe $fixtureExe -CommandArgs @('sleep','250') `
        -Stdout $logs.stdout -Stderr $logs.stderr -Pattern $pattern `
        -IntervalMs $intervalMs -AllowedGapMs $allowedGapMs -RequireQuiescence
    Assert-Eq $result.exit_code 0 'clean owned fixture exit code'
    Assert-True $result.quiescence.emulator_started 'clean owned fixture was not launched'
    Assert-True $result.quiescence.timing_usable 'clean owned fixture was not timing usable'
    Assert-True (-not $result.quiescence.contaminated) 'owned fixture contaminated its own leg'
    Assert-True ($result.quiescence.observer.owned.Count -ge 1) 'owned process identity missing'

    $logs = New-LogPair $testRoot 'competitor'
    $result = Invoke-ObservedProcess -Exe $fixtureExe -CommandArgs @('spawn-competitor','1200','1700') `
        -Stdout $logs.stdout -Stderr $logs.stderr -Pattern $pattern `
        -IntervalMs $intervalMs -AllowedGapMs $allowedGapMs -RequireQuiescence
    Assert-Eq $result.exit_code 0 'competitor parent exit code'
    Assert-True $result.quiescence.contaminated 'same-name in-leg competitor was not detected'
    Assert-True (-not $result.quiescence.timing_usable) 'contaminated leg was marked timing usable'
    Assert-True ($result.quiescence.before.Count -eq 0) 'competitor was present before launch'
    Assert-True ($result.quiescence.after.Count -eq 0) 'competitor remained after launch'
    Assert-True ($result.quiescence.during.Count -ge 1) 'competitor missing from during evidence'

    $busy = Start-FixtureProcess $fixtureExe @('sleep','5000')
    Start-Sleep -Milliseconds 150
    $logs = New-LogPair $testRoot 'busy'
    $result = Invoke-ObservedProcess -Exe $fixtureExe -CommandArgs @('sleep','100') `
        -Stdout $logs.stdout -Stderr $logs.stderr -Pattern $pattern `
        -IntervalMs $intervalMs -AllowedGapMs $allowedGapMs -RequireQuiescence
    Assert-True (-not $result.quiescence.emulator_started) 'busy required leg launched'
    Assert-Eq $result.quiescence.reason 'busy_before_launch' 'busy refusal reason'
    Assert-True $result.quiescence.contaminated 'busy refusal did not record contamination'
    Assert-True (-not (Test-Path -LiteralPath $logs.stdout)) 'busy refusal created stdout'
    Stop-FixtureProcess $busy
    $busy = $null

    $resident = Start-FixtureProcess $fixtureExe @('sleep','5000')
    Start-Sleep -Milliseconds 150
    $logs = New-LogPair $testRoot 'resident'
    $result = Invoke-ObservedProcess -Exe $fixtureExe -CommandArgs @('sleep','200') `
        -Stdout $logs.stdout -Stderr $logs.stderr -Pattern $pattern `
        -IntervalMs $intervalMs -AllowedGapMs $allowedGapMs -ResidentPids @($resident.Id) `
        -RequireQuiescence
    Assert-Eq $result.exit_code 0 'resident exclusion exit code'
    Assert-True $result.quiescence.emulator_started 'resident exclusion did not launch'
    Assert-True $result.quiescence.timing_usable 'resident exclusion was not timing usable'
    Assert-True (-not $result.quiescence.contaminated) 'resident exclusion contaminated leg'
    Assert-True ($result.quiescence.observer.residents.Count -eq 1) 'resident identity missing'
    $staleIdentity = [Xlle.Replay.ProcessIdentity]::new()
    $staleIdentity.pid = $resident.Id
    $staleIdentity.name = $resident.ProcessName
    $staleIdentity.start_ticks = $resident.StartTime.ToUniversalTime().Ticks - 1
    $logs = New-LogPair $testRoot 'reused-pid'
    $result = Invoke-ObservedProcess -Exe $fixtureExe -CommandArgs @('sleep','200') `
        -Stdout $logs.stdout -Stderr $logs.stderr -Pattern $pattern `
        -IntervalMs $intervalMs -AllowedGapMs $allowedGapMs -ResidentIdentities @($staleIdentity) `
        -RequireQuiescence
    Assert-Eq $result.quiescence.reason 'busy_before_launch' 'different creation time received resident exclusion'
    Stop-FixtureProcess $resident
    $resident = $null

    $logs = New-LogPair $testRoot 'observer-required'
    $result = Invoke-ObservedProcess -Exe $fixtureExe -CommandArgs @('sleep','100') `
        -Stdout $logs.stdout -Stderr $logs.stderr -Pattern '(' `
        -IntervalMs $intervalMs -AllowedGapMs $allowedGapMs -RequireQuiescence
    Assert-True (-not $result.quiescence.emulator_started) 'required observer failure launched'
    Assert-Eq $result.quiescence.reason 'observer_unavailable' 'observer failure reason'
    Assert-True (-not $result.quiescence.timing_usable) 'observer failure was timing usable'
    Assert-True (-not $result.quiescence.observer.healthy) 'observer failure was healthy'
    Assert-True (@($result.quiescence.observer.errors).Count -ge 1) 'observer failure missing errors'

    $logs = New-LogPair $testRoot 'observer-diagnostic'
    $result = Invoke-ObservedProcess -Exe $fixtureExe -CommandArgs @('sleep','100') `
        -Stdout $logs.stdout -Stderr $logs.stderr -Pattern '(' `
        -IntervalMs $intervalMs -AllowedGapMs $allowedGapMs
    Assert-Eq $result.exit_code 0 'diagnostic observer failure exit code'
    Assert-True $result.quiescence.emulator_started 'diagnostic observer failure did not launch'
    Assert-True (-not $result.quiescence.timing_usable) 'diagnostic observer failure was timing usable'
    Assert-True (-not $result.quiescence.observer.healthy) 'diagnostic observer failure was healthy'

    $argv = @('', 'plain', 'has "quote"', 'trail\')
    $logs = New-LogPair $testRoot 'argv'
    $result = Invoke-ObservedProcess -Exe $fixtureExe -CommandArgs (@('echoargs','17') + $argv) `
        -Stdout $logs.stdout -Stderr $logs.stderr -Pattern $pattern `
        -IntervalMs $intervalMs -AllowedGapMs $allowedGapMs -RequireQuiescence
    Assert-Eq $result.exit_code 17 'argv exit code'
    Assert-True $result.quiescence.timing_usable 'argv leg was not timing usable'
    $actual = @(Get-Content -LiteralPath $logs.stdout | ForEach-Object {
        if ($_ -notlike 'ARG:*') { throw "unexpected stdout line: $_" }
        [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($_.Substring(4)))
    })
    Assert-Eq $actual.Count $argv.Count 'argv count'
    for ($i = 0; $i -lt $argv.Count; ++$i) {
        Assert-Eq $actual[$i] $argv[$i] "argv[$i] roundtrip"
    }
    Assert-Eq (Get-Content -LiteralPath $logs.stderr -Raw).Trim() 'ERR:4' 'stderr roundtrip'

    $logs = New-LogPair $testRoot 'pipes'
    $result = Invoke-ObservedProcess -Exe $fixtureExe -CommandArgs @('pipes') `
        -Stdout $logs.stdout -Stderr $logs.stderr -Pattern $pattern `
        -IntervalMs $intervalMs -AllowedGapMs $allowedGapMs -RequireQuiescence
    Assert-Eq $result.exit_code 0 'large pipe exit code'
    Assert-Eq (Get-Item -LiteralPath $logs.stdout).Length 1048576 'stdout was truncated'
    Assert-Eq (Get-Item -LiteralPath $logs.stderr).Length 1048576 'stderr was truncated'

    $logs = New-LogPair $testRoot 'cwd'
    Push-Location $testRoot
    try {
        $result = Invoke-ObservedProcess -Exe $fixtureExe -CommandArgs @('cwd') `
            -Stdout $logs.stdout -Stderr $logs.stderr -Pattern $pattern `
            -IntervalMs $intervalMs -AllowedGapMs $allowedGapMs -RequireQuiescence
    } finally { Pop-Location }
    Assert-Eq $result.exit_code 0 'cwd exit code'
    Assert-Eq ([IO.File]::ReadAllText($logs.stdout)) $testRoot 'child ignored PowerShell working directory'

    # Exercise the actual integrated leg function: required observation must
    # reject a mid-run contaminant even when report parsing is disabled.
    $parseErrors = $null; $tokens = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $repo 'tools/run_stockdash_replay.ps1'), [ref]$tokens, [ref]$parseErrors)
    Assert-Eq $parseErrors.Count 0 'harness parse errors'
    $leg = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-ReplayLeg'
    }, $true)
    . ([scriptblock]::Create($leg.Extent.Text))
    function New-XlleArgs { return , @('spawn-competitor','1200','1700') }
    function Get-CpuSnapshot { return $null }
    function Write-JsonFile($Path, $Value) {
        [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 8))
    }
    $QuiescencePattern = $pattern; $QuiescenceIntervalMs = $intervalMs
    $QuiescenceMaxGapMs = $allowedGapMs; $ResidentProcessIds = @()
    $RequireQuiescence = $true; $NoParse = $true; $RetryContaminatedPairs = $false
    $Force = $false
    $prefix = Join-Path $testRoot 'integrated'
    $caught = $false
    try { Invoke-ReplayLeg -Prefix $prefix -Exe $fixtureExe -Arm A -LegIndex 0 -Repetition 1 -LegName integrated }
    catch { $caught = $_ -like '*lacks healthy quiet-host observation*' }
    Assert-True $caught 'required NoParse leg accepted contamination'
    $record = Get-Content -LiteralPath "$prefix.quiescence.json" -Raw | ConvertFrom-Json
    Assert-True $record.contaminated 'integrated leg did not archive contamination'
    Assert-True (-not (Test-ReplayTimingUsable $record)) 'serialized contaminated evidence passed'

    Write-Host 'test_replay_quiescence: PASS (real observer subprocesses, resident exclusion, observer failure, argv)'
} finally {
    Stop-FixtureProcess $busy
    Stop-FixtureProcess $resident
    if (Test-Path -LiteralPath $testRoot) {
        Get-ChildItem -LiteralPath $testRoot -File | ForEach-Object {
            Remove-Item -LiteralPath $_.FullName -Force
        }
        Remove-Item -LiteralPath $testRoot
    }
}
