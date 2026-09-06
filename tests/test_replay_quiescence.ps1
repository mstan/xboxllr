# Run the real leg function against a local PowerShell stub, never the emulator.
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$tokens=$null
$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $repo 'tools/run_stockdash_replay.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Harness parse failed' }
$leg=$ast.Find({param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Invoke-ReplayLeg'
}, $true)
if (!$leg) { throw 'Replay leg function missing' }
. ([scriptblock]::Create($leg.Extent.Text))
$testRoot=Join-Path ([IO.Path]::GetTempPath()) ('xlle-quiescence-' + [guid]::NewGuid())
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
try {
    $stub=Join-Path $testRoot 'stub.ps1'
    [IO.File]::WriteAllText($stub, '$global:xlleQuiescenceStubCalls += 1; $global:LASTEXITCODE = 0')
    $global:xlleQuiescenceStubCalls=0
    function Write-JsonFile($Path,$Value) {
        [IO.File]::WriteAllText($Path,(ConvertTo-Json -InputObject $Value -Depth 8))
    }
    function New-XlleArgs { return @() }
    function Get-QuotedCommandText { return '' }
    function Get-ProcessCensus { return ,$script:foreign }
    function Get-CpuSnapshot { return $null }
    $Force=$false
    $NoParse=$true
    $QuiescencePattern='test-only'
    $RequireQuiescence=$true
    $script:foreign=@([pscustomobject]@{name='cmake';pid=123})
    $prefix=Join-Path $testRoot 'busy'
    $caught=$false
    try {
        Invoke-ReplayLeg -Prefix $prefix -Exe $stub -Arm A -LegIndex 0 -Repetition 1 -LegName busy
    } catch { $caught=$_.ToString() -like '*host is busy*was not started*' }
    if (!$caught -or $global:xlleQuiescenceStubCalls -ne 0) {
        throw 'Known-busy host launched a leg or hid the refusal'
    }
    $record=Get-Content -LiteralPath "$prefix.quiescence.json" -Raw | ConvertFrom-Json
    if (!$record.contaminated -or $record.emulator_started -ne $false -or
        $record.reason -ne 'busy_before_launch' -or $record.before.Count -ne 1) {
        throw 'Missing preflight refusal evidence'
    }
    if (Test-Path -LiteralPath "$prefix.stdout.log") { throw 'Refused leg created stdout' }

    $script:foreign=@()
    Invoke-ReplayLeg -Prefix (Join-Path $testRoot 'quiet') -Exe $stub -Arm A -LegIndex 1 -Repetition 1 -LegName quiet
    if ($global:xlleQuiescenceStubCalls -ne 1) { throw 'Quiet host did not launch' }

    $RequireQuiescence=$false
    $script:foreign=@([pscustomobject]@{name='cmake';pid=123})
    $prefix=Join-Path $testRoot 'diagnostic'
    Invoke-ReplayLeg -Prefix $prefix -Exe $stub -Arm A -LegIndex 2 -Repetition 1 -LegName diagnostic
    $record=Get-Content -LiteralPath "$prefix.quiescence.json" -Raw | ConvertFrom-Json
    if ($global:xlleQuiescenceStubCalls -ne 2 -or !$record.contaminated) {
        throw 'Non-gated diagnostic behavior changed or contamination was hidden'
    }
    Write-Host 'test_replay_quiescence: PASS (busy refusal, recorded evidence, quiet launch, diagnostic mode)'
} finally {
    # Only immediate files created under this unique test directory; no recursion.
    Get-ChildItem -LiteralPath $testRoot -File | ForEach-Object {
        Remove-Item -LiteralPath $_.FullName -Force
    }
    Remove-Item -LiteralPath $testRoot
    Remove-Variable -Scope Global -Name xlleQuiescenceStubCalls -ErrorAction SilentlyContinue
}
