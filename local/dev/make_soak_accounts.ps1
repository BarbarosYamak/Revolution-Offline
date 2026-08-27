# M3.9 PHASE 14 -- provision the extra accounts the soak needs.
#
#   .\make_soak_accounts.ps1 -From 13 -To 50
#
# The shard owner asked for 30-50 bots at once. Only twelve accounts existed,
# each purpose-built for an earlier milestone, so this appends plain soak
# accounts for the rest.
#
# NOTHING HERE IS PRIVILEGED. Sphere creates the account on first login
# (AccApp), and the character is made with the ordinary 0x00 create packet, so
# these bots get exactly what the shard hands any new player -- Source-X clamps
# each requested skill to 50 and the sum to 100 in CChar::InitPlayer, and its
# own newbie templates decide the kit. No skills, gold or items are granted.
#
# Credentials land in bot-credentials.env, which is covered by /local/ in
# .gitignore and MUST NOT be committed: this shard runs Md5Passwords=0, so
# every password in that file is stored in plaintext by the server too.
#
# Idempotent: an account block that is already present is left alone, so this
# can be re-run without churning passwords for bots that already have characters.
param(
    [int]$From = 13,
    [int]$To   = 50
)

$dev  = $PSScriptRoot
$file = Join-Path $dev 'bot-credentials.env'
if (-not (Test-Path $file)) { throw "missing $file" }

$existing = Get-Content $file -Raw

# Sphere truncates stored passwords to MAX_ACCOUNT_PASSWORD_ENTER (16) in
# CAccount::SetPassword, so anything longer could never match on the next login.
function New-Pass {
    $chars = 'abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789'.ToCharArray()
    -join (1..16 | ForEach-Object { $chars | Get-Random })
}

$added = 0
$lines = New-Object System.Collections.Generic.List[string]

for ($i = $From; $i -le $To; $i++) {
    $user = 'RevolutionSoak{0:D2}' -f $i
    $char = 'Soak{0:D2}' -f $i
    if ($existing -match [regex]::Escape("UO_BOT_USER$i=")) { continue }
    $pass = New-Pass
    $lines.Add("")
    $lines.Add("# Session $i -- M3.9 soak. Ordinary character, no provisioning.")
    $lines.Add("UO_BOT_USER$i=$user")
    $lines.Add("UO_BOT_CHAR$i=$char")
    $lines.Add("UO_BOT_PASS_$($user.ToUpper())=$pass")
    $added++
}

if ($added -gt 0) {
    Add-Content -Path $file -Value ($lines -join "`r`n") -Encoding utf8
}
"added $added account block(s) to bot-credentials.env"
