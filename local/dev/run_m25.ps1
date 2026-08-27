# Run one M2.5 scenario against the local Source-X shard.
#
#   .\run_m25.ps1 -Scenario m25_service_bank -Tag s1 [-Session 1] [-TimeoutSec 300]
#
# Credentials come from local/dev/bot-credentials.env, which is not committed.
# Logs land in local/dev/<tag>.log (session) and <tag>.console.txt (stdout).
param(
    [Parameter(Mandatory = $true)][string]$Scenario,
    [Parameter(Mandatory = $true)][string]$Tag,
    [int]$Session = 1,
    [int]$TimeoutSec = 300,
    # Ask the shard to create the character on first login. The three skills
    # are a REQUEST: Source-X clamps each to 50 and the sum to 100 in
    # CChar::InitPlayer, and its own newbie templates decide the kit.
    [string]$CreateSkills = '',
    # Starting stats as "str:dex:int". Source-X clamps each to 60 and the sum
    # to 80. M3.7 needs this because i_pickaxe carries REQSTR=50 and the shovel
    # is unwearable in Revolution's tiledata, so a miner must be built STR 50.
    [string]$CreateStats = '',
    # Long runs hold uo_client.exe open, which blocks the next link.
    # Launching them from a copy keeps the build tree free to rebuild while a
    # multi-hour training block is still going.
    [switch]$UseRunnerCopy
)

$root   = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$client = Join-Path $root 'bot\uo-client'
$dev    = Join-Path $root 'local\dev'

# Load the credential file into the environment for this process only.
Get-Content (Join-Path $dev 'bot-credentials.env') | ForEach-Object {
    if ($_ -match '^\s*([A-Za-z0-9_]+)\s*=\s*(.+?)\s*$') {
        Set-Item -Path ("env:" + $Matches[1]) -Value $Matches[2]
    }
}

$suffix = if ($Session -eq 1) { '' } else { "$Session" }
$user = (Get-Item ("env:UO_BOT_USER" + $suffix)).Value
$char = (Get-Item ("env:UO_BOT_CHAR" + $suffix)).Value
$passKey = "UO_BOT_PASS_" + $user.ToUpper()
$pass = (Get-Item ("env:" + $passKey) -ErrorAction SilentlyContinue)
if ($pass) { $env:UO_BOT_PASS = $pass.Value }
$env:UO_BOT_USER = $user

$log     = Join-Path $dev "$Tag.log"
$console = Join-Path $dev "$Tag.console.txt"
$errFile = Join-Path $dev "$Tag.err.txt"

$args = @(
    '--host', '127.0.0.1', '--port', '2593',
    '--char-name', $char,
    '--mul-dir',  (Join-Path $root 'runtime\mul'),
    '--data-dir', (Join-Path $client 'data'),
    '--scenario', (Join-Path $client "scripts\scenarios\$Scenario.txt"),
    '--log', $log,
    '--tag', $Tag,
    '--headless'
)
if ($CreateSkills -ne '') {
    $args += @('--create-char', '--create-skills', $CreateSkills)
}
if ($CreateStats -ne '') {
    if ($CreateSkills -eq '') { $args += @('--create-char') }
    $args += @('--create-stats', $CreateStats)
}

Write-Host "running $Scenario as $char (tag $Tag)"
$exePath = Join-Path $client 'build-m1\uo_client.exe'
if ($UseRunnerCopy) {
    # Snapshot the CURRENT build per run. A long job holds its exe open,
    # which blocks the next link, and one shared copy silently goes stale --
    # a run once failed because it was executing a binary from before the
    # feature it was testing existed.
    $runDir = Join-Path $client 'build-m1\run'
    if (-not (Test-Path $runDir)) { New-Item -ItemType Directory $runDir | Out-Null }
    $snapshot = Join-Path $runDir ($Tag + '.exe')
    Copy-Item $exePath $snapshot -Force
    $exePath = $snapshot
}
$p = Start-Process -FilePath $exePath `
    -ArgumentList $args -NoNewWindow -PassThru `
    -RedirectStandardOutput $console -RedirectStandardError $errFile
if (-not $p.WaitForExit($TimeoutSec * 1000)) {
    Write-Host "TIMEOUT after $TimeoutSec s -- killing"
    $p.Kill()
    $p.WaitForExit()
}
Write-Host "exit code $($p.ExitCode)"
