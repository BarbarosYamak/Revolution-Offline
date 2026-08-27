<#
    Prepare local\revolution-client\uo.cfg for a given account.

    Sets the account name, the saved password, and the window sizes, so the
    launchers can start the genuine Revolution client straight into the login
    screen with everything filled in.

    PASSWORD ENCODING
    The UO 2D client does not store uo.cfg passwords in plaintext. It applies a
    Caesar shift of +13 over the printable ASCII range:

        enc(c) = ((c - 32 + 13) mod 95) + 32

    Derived by comparing the value the client itself had written
    ("uaWune{]C'_&XetF") against the known Admin password -- it decodes exactly.
    So what this writes is byte-identical to what the client would save itself
    after a manual login with "save password" on. It is obfuscation, not
    encryption: treat uo.cfg as holding the password.

    WINDOW SIZE
    GamePlayWindowSize is the in-world viewport; FullScreenRes is the fullscreen
    mode. client.dll only contains the strings "640x480" and "800x600", so those
    are the only entries its own Options dropdown offers -- but the cfg values
    are read at startup and larger ones are worth trying. If a size is refused
    the client falls back on its own; nothing here can break it permanently.
#>
param(
    [Parameter(Mandatory=$true)][string]$Account,
    [string]$Password = "",
    # Rather than passing the password on a command line, where it would be
    # visible in process listings, point at the credentials file and its key
    # prefix and let this script read it.
    [string]$CredsFile = "",
    [string]$Prefix = "",
    [string]$GameWindow = "",
    [string]$FullScreenRes = "",
    [string]$CfgPath = "C:\Projects\RevolutionOffline\local\revolution-client\uo.cfg"
)

function Encode-UoPassword([string]$p) {
    if ([string]::IsNullOrEmpty($p)) { return "" }
    $sb = New-Object System.Text.StringBuilder
    foreach ($ch in $p.ToCharArray()) {
        $v = [int][char]$ch
        [void]$sb.Append([char](((($v - 32 + 13) % 95) + 95) % 95 + 32))
    }
    return $sb.ToString()
}

if ($CredsFile -and -not $Password) {
    if (Test-Path $CredsFile) {
        foreach ($l in Get-Content $CredsFile) {
            if ($l -match "^\s*$([regex]::Escape($Prefix))_PASSWORD\s*=\s*(.+?)\s*$") { $Password = $Matches[1] }
        }
    }
}

if (-not (Test-Path $CfgPath)) { Write-Error "uo.cfg not found at $CfgPath"; exit 1 }

$lines = @(Get-Content -Path $CfgPath)

function Set-Key([string[]]$text, [string]$key, [string]$value) {
    $hit = $false
    $out = foreach ($l in $text) {
        if ($l -match "^\s*$([regex]::Escape($key))=") { $hit = $true; "$key=$value" } else { $l }
    }
    if (-not $hit) { $out = $out + "$key=$value" }
    return $out
}

$lines = Set-Key $lines "AcctID" $Account
if ($Password) {
    $lines = Set-Key $lines "AcctPassword" (Encode-UoPassword $Password)
    $lines = Set-Key $lines "SavePassword"   "on"
    $lines = Set-Key $lines "RememberAcctPW" "on"
}
# The 2D client HARD-REJECTS anything else, with a fatal dialog on startup:
#   GraphicManager::setGameplayWindowPixelWidthAndHeight:
#   Currently only supports 640x480 & 800x600.
# Confirmed twice: the string is referenced exactly once in .text (VA
# 0x44902D), and setting 1280x720 really did produce that dialog. So refuse
# to write a value that would make the client unlaunchable.
if ($GameWindow -and $GameWindow -notin @("640x480","800x600")) {
    Write-Warning ("GamePlayWindowSize {0} is not supported by this client; using 800x600. Use CrossUO for a larger viewport." -f $GameWindow)
    $GameWindow = "800x600"
}
if ($GameWindow)    { $lines = Set-Key $lines "GamePlayWindowSize" $GameWindow }
if ($FullScreenRes) { $lines = Set-Key $lines "FullScreenRes"      $FullScreenRes }

Set-Content -Path $CfgPath -Value $lines -Encoding ASCII

$shown = if ($Password) { "set" } else { "unchanged" }
Write-Host ("uo.cfg: AcctID={0}, password={1}, GamePlayWindowSize={2}, FullScreenRes={3}" -f `
            $Account, $shown, $(if($GameWindow){$GameWindow}else{"unchanged"}), `
            $(if($FullScreenRes){$FullScreenRes}else{"unchanged"}))
