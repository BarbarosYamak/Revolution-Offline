# Run one scenario as the ADMIN account (character "Observer").
#
#   .\run_admin.ps1 -Scenario m37_deco_britain -Tag deco1 [-TimeoutSec 300]
#
# This is OPERATOR tooling, not gameplay. It exists because this Sphere
# process was launched detached: it has no main window handle, so
# sphere_console.ps1 cannot reach it, and no telnet port is open. The only
# remaining route to a server-side function is an in-game privileged account.
#
# Credentials come from local/dev/admin-credentials.env (not committed).
# Nothing run through here may touch a bot character's skills, stats, gold or
# inventory -- see docs/M3_PROGRESSION_ECONOMY.md 13 for the standing rule.
param(
    [Parameter(Mandatory = $true)][string]$Scenario,
    [Parameter(Mandatory = $true)][string]$Tag,
    [int]$TimeoutSec = 300
)

$root   = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$client = Join-Path $root 'bot\uo-client'
$dev    = Join-Path $root 'local\dev'

Get-Content (Join-Path $dev 'admin-credentials.env') | ForEach-Object {
    if ($_ -match '^\s*([A-Za-z0-9_]+)\s*=\s*(.+?)\s*$') {
        Set-Item -Path ("env:" + $Matches[1]) -Value $Matches[2]
    }
}

$env:UO_BOT_USER = $env:ADMIN_ACCOUNT
$env:UO_BOT_PASS = $env:ADMIN_PASSWORD

$log     = Join-Path $dev "$Tag.log"
$console = Join-Path $dev "$Tag.console.txt"
$errFile = Join-Path $dev "$Tag.err.txt"

$args = @(
    '--host', '127.0.0.1', '--port', '2593',
    '--char-name', 'Observer',
    '--mul-dir',  (Join-Path $root 'runtime\mul'),
    '--data-dir', (Join-Path $client 'data'),
    '--scenario', (Join-Path $client "scripts\scenarios\$Scenario.txt"),
    '--log', $log,
    '--tag', $Tag,
    '--headless'
)

Write-Host "running $Scenario as Observer (Admin) (tag $Tag)"
$exePath = Join-Path $client 'build-m1\uo_client.exe'
$p = Start-Process -FilePath $exePath `
    -ArgumentList $args -NoNewWindow -PassThru `
    -RedirectStandardOutput $console -RedirectStandardError $errFile
if (-not $p.WaitForExit($TimeoutSec * 1000)) {
    Write-Host "TIMEOUT after $TimeoutSec s -- killing"
    $p.Kill()
    $p.WaitForExit()
}
Write-Host "exit code $($p.ExitCode)"
