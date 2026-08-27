# Run a two-session M2.5 scenario, sequenced by OBSERVABLE STATE rather than by
# a sleep: the traveller is not started until the blocker's own log says it has
# arrived where it is meant to be.
#
# This is the M2 rendezvous debt paid off at the harness level. It deliberately
# does not become a synchronisation framework -- it waits for one event.
#
#   .\run_m25_pair.ps1 -Blocker m25_obstacle_blocker -BlockerSession 4 `
#                      -Traveller m25_obstacle -TravellerSession 1 -Tag m25_s6
param(
    [Parameter(Mandatory = $true)][string]$Blocker,
    [Parameter(Mandatory = $true)][string]$Traveller,
    [Parameter(Mandatory = $true)][string]$Tag,
    [int]$BlockerSession = 4,
    [int]$TravellerSession = 1,
    [int]$ReadyTimeoutSec = 180,
    [int]$TimeoutSec = 300,
    [int]$BlockerSessionArg = 0,
    # What in the first session's log means "in position". Still one observable
    # event, not a synchronisation framework.
    [string]$ReadyPattern = 'event travel_done',
    [switch]$UseRunnerCopy
)

$dev = $PSScriptRoot
$blockerTag = "$Tag`_blocker"
$travellerTag = "$Tag`_traveller"
$blockerLog = Join-Path $dev "$blockerTag.log"
if (Test-Path $blockerLog) { Remove-Item $blockerLog }

$b = Start-Job -ScriptBlock {
    param($dev, $scenario, $tag, $session, $timeout, $copy)
    if ($copy) {
        & (Join-Path $dev 'run_m25.ps1') -Scenario $scenario -Tag $tag -Session $session -TimeoutSec $timeout -UseRunnerCopy
    } else {
        & (Join-Path $dev 'run_m25.ps1') -Scenario $scenario -Tag $tag -Session $session -TimeoutSec $timeout
    }
} -ArgumentList $dev, $Blocker, $blockerTag, $BlockerSession, $TimeoutSec, ([bool]$UseRunnerCopy)

Write-Host "waiting for the blocker to be in position..."
$deadline = (Get-Date).AddSeconds($ReadyTimeoutSec)
$ready = $false
while ((Get-Date) -lt $deadline) {
    if ((Test-Path $blockerLog) -and
        (Select-String -Path $blockerLog -Pattern $ReadyPattern -Quiet)) {
        $ready = $true
        break
    }
    Start-Sleep -Milliseconds 500
}
if (-not $ready) {
    Write-Host "blocker never reported arrival; aborting"
    Stop-Job $b; Remove-Job $b -Force
    exit 1
}
Write-Host "blocker in position; starting the traveller"

if ($UseRunnerCopy) {
    & (Join-Path $dev 'run_m25.ps1') -Scenario $Traveller -Tag $travellerTag `
        -Session $TravellerSession -TimeoutSec $TimeoutSec -UseRunnerCopy
} else {
    & (Join-Path $dev 'run_m25.ps1') -Scenario $Traveller -Tag $travellerTag `
        -Session $TravellerSession -TimeoutSec $TimeoutSec
}

Stop-Job $b -ErrorAction SilentlyContinue
Receive-Job $b -ErrorAction SilentlyContinue | Out-Null
Remove-Job $b -Force -ErrorAction SilentlyContinue
