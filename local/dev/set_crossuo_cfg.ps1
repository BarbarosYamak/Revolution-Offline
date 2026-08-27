<#
    Point tools\CrossUO\crossuo.cfg at one of our accounts.

    CrossUO is an open-source reimplementation of the UO 2D client. It reads
    Revolution's OWN client data (CustomPath + UseVerdata), so the art is the
    same as the real client -- but it is not bound by the real client's
    hard-coded 640x480 / 800x600 play window, which the binary itself admits to:
        "WindowPixelWidthAndHeight: Currently only supports 640x480 & 800x600."

    WARNING: CrossUO stores AcctPassword in PLAINTEXT. The real client at least
    applies a Caesar+13 obfuscation. Pass -NoPassword to leave it out and type
    the password at the login screen instead.
#>
param(
    [Parameter(Mandatory=$true)][string]$Account,
    [string]$CredsFile = "",
    [string]$Prefix = "",
    # CrossUO is not bound by the real client's hard-coded 640x480/800x600
    # play window; these are its own WindowWidth/WindowHeight keys.
    [int]$Width = 1600,
    [int]$Height = 900,
    [switch]$NoPassword,
    [string]$CfgPath = "C:\Projects\RevolutionOffline\tools\CrossUO\crossuo.cfg",
    [string]$MulPath = "C:\Projects\RevolutionOffline\local\revolution-client"
)

$pw = ""
if (-not $NoPassword -and $CredsFile -and (Test-Path $CredsFile)) {
    foreach ($l in Get-Content $CredsFile) {
        if ($l -match "^\s*$([regex]::Escape($Prefix))_PASSWORD\s*=\s*(.+?)\s*$") { $pw = $Matches[1] }
    }
}

$desired = [ordered]@{
    "AcctID"         = $Account
    "RememberAcctPW" = $(if ($pw) { "yes" } else { "no" })
    "AutoLogin"      = "no"
    "UseVerdata"     = "yes"
    "Crypt"          = "no"
    "CustomPath"     = $MulPath
    "LoginServer"    = "127.0.0.1,2593"
    "ClientVersion"  = "2.0.3"
    "WindowWidth"    = "$Width"
    "WindowHeight"   = "$Height"
}
if ($pw) { $desired["AcctPassword"] = $pw }

$lines = @()
if (Test-Path $CfgPath) { $lines = @(Get-Content $CfgPath) }

foreach ($k in $desired.Keys) {
    $v = $desired[$k]
    $hit = $false
    $lines = @(foreach ($l in $lines) {
        if ($l -match "^\s*$([regex]::Escape($k))=") { $hit = $true; "$k=$v" } else { $l }
    })
    if (-not $hit) { $lines += "$k=$v" }
}

# If the password was dropped, do not leave a stale one behind.
if (-not $pw) { $lines = @($lines | Where-Object { $_ -notmatch "^\s*AcctPassword=" }) }

Set-Content -Path $CfgPath -Value $lines -Encoding ASCII
Write-Host ("crossuo.cfg: AcctID={0}, password={1}, window={2}x{3}" -f $Account, $(if ($pw) { "set (PLAINTEXT)" } else { "not stored" }), $Width, $Height)
