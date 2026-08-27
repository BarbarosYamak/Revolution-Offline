# M3.9 PHASE 14 -- launch the multi-bot soak.
#
#   .\run_soak.ps1 -Count 40 -Scenario m39_soak -TimeoutSec 2400
#
# Launches N bots against the local shard at once and waits for all of them.
# The milestone asked for 5-10; the shard owner asked for 30-50 "just to test",
# which is the more interesting number -- concurrency bugs do not show up at
# five.
#
# WHY THIS IS SAFE TO RUN AT THIS SCALE
#
# Each uo_client is ~25 MB resident: map0.mul is 89 MB but it is memory-MAPPED,
# not copied, so forty clients share one physical copy. Forty bots is roughly
# 1 GB against 15.7 GB free on 16 cores.
#
# Sessions 1-12 are the purpose-built milestone characters and are NOT used
# here. Several of them carry M3.7's economy proofs in their packs, and death on
# this shard is full loot loss. The soak uses sessions 13+ (RevolutionSoakNN),
# which are ordinary characters created by the normal 0x00 packet and own
# nothing worth losing.
#
# Launches are staggered: forty simultaneous logins plus character creation is a
# thundering herd on the login server, and a connection refused at second zero
# would look like a soak failure when it is really a test artefact.
param(
    [int]$Count      = 40,
    [int]$First      = 13,
    [string]$Scenario = 'm39_soak',
    [int]$TimeoutSec = 2400,
    [int]$StaggerMs  = 1200,
    [string]$Prefix  = 'soak'
)

$dev = $PSScriptRoot
$jobs = @()

Write-Host "launching $Count bots, sessions $First..$($First + $Count - 1), scenario $Scenario"

for ($i = 0; $i -lt $Count; $i++) {
    $session = $First + $i
    $tag     = "$Prefix$session"

    # Ordinary character creation. Source-X clamps each skill to 50 and the sum
    # to 100 (CChar::InitPlayer) and picks the newbie kit itself -- these values
    # are a REQUEST, not a grant. Harmless if the character already exists: the
    # client only sends 0x00 when the character list comes back empty.
    $args = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', (Join-Path $dev 'run_m25.ps1'),
        '-Scenario', $Scenario,
        '-Tag', $tag,
        '-Session', $session,
        '-TimeoutSec', $TimeoutSec,
        '-CreateSkills', 'Tactics=30,Wrestling=30,Healing=20',
        '-CreateStats',  '40:30:10',
        '-UseRunnerCopy'
    )

    $p = Start-Process -FilePath 'powershell.exe' -ArgumentList $args `
                       -WorkingDirectory $dev -PassThru -WindowStyle Hidden
    $jobs += [pscustomobject]@{ Session = $session; Tag = $tag; Proc = $p }
    Start-Sleep -Milliseconds $StaggerMs
}

Write-Host "all $($jobs.Count) launched; waiting..."
$deadline = (Get-Date).AddSeconds($TimeoutSec + 180)
foreach ($j in $jobs) {
    $remain = [int]($deadline - (Get-Date)).TotalSeconds
    if ($remain -lt 1) { $remain = 1 }
    try { Wait-Process -Id $j.Proc.Id -Timeout $remain -ErrorAction Stop } catch {}
}

Write-Host "`n=== SOAK RESULTS ==="
$ok = 0; $bad = 0
foreach ($j in $jobs) {
    $console = Join-Path $dev "$($j.Tag).console.txt"
    $errf    = Join-Path $dev "$($j.Tag).err.txt"
    $verdict = 'NO OUTPUT'
    if (Test-Path $console) {
        $c = Get-Content $console -Raw
        # The verdict lives in the console log, never in the .log file -- an
        # earlier milestone reported two slices as passing by reading the wrong
        # file. Aborts land in .err.txt.
        $aborted = (Test-Path $errf) -and ((Get-Content $errf -Raw) -match 'ABORTED')
        if ($aborted)                          { $verdict = 'ABORTED' }
        elseif ($c -match 'soak circuit complete') { $verdict = 'OK' }
        elseif ($c -match 'logout_complete')   { $verdict = 'ENDED EARLY' }
        else                                   { $verdict = 'INCOMPLETE' }
    }
    if ($verdict -eq 'OK') { $ok++ } else { $bad++ }
    "{0,-10} {1}" -f $j.Tag, $verdict
}
"`nOK=$ok  NOT-OK=$bad  of $($jobs.Count)"
