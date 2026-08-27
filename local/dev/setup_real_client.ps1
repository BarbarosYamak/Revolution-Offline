# Run this ONCE in an ELEVATED PowerShell.
#
# Makes the genuine Revolution client (local\revolution-client\client.dll,
# which is an EXE renamed) runnable directly -- WITHOUT Revolution.exe,
# LoaderDLL.dll or WCP.dll, i.e. without the anti-cheat/updater chain.
#
# Why this is needed: the UO client refuses to start unless its install is
# registered, and reports "Ultima Online does not appear to be installed
# correctly on your system".
#
# THE KEY IS SHARD-REBRANDED. It is NOT the stock "Origin Worlds Online" path.
# Read straight out of client.dll:
#     SOFTWARE\Revolution UO Shards\Ultima Online\1.0
#     SOFTWARE\Revolution UO Shards\Ultima Online\1.0\HWProfile
# with values ExePath / InstCDPath / Patch / Language.
#
# A 32-bit process reads HKLM\SOFTWARE through WOW6432Node, so both views are
# written. HKCU is written too, harmlessly, in case this build prefers it.
#
# It also adds an outbound firewall block for the client. Windows does not
# filter loopback, so 127.0.0.1:2593 still works while nothing else can leave.

$dir = "C:\Projects\RevolutionOffline\local\revolution-client"
$exe = Join-Path $dir "uoclient.exe"
$ver = "1.46.0.3"     # Revolution's client version

$keys = @(
    "HKLM:\SOFTWARE\Revolution UO Shards\Ultima Online\1.0",
    "HKLM:\SOFTWARE\WOW6432Node\Revolution UO Shards\Ultima Online\1.0",
    "HKCU:\SOFTWARE\Revolution UO Shards\Ultima Online\1.0"
)

foreach ($base in $keys) {
    New-Item -Path $base -Force | Out-Null
    New-Item -Path (Join-Path $base "HWProfile") -Force | Out-Null
    New-ItemProperty -Path $base -Name "ExePath"    -Value $exe    -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $base -Name "InstCDPath" -Value "$dir\" -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $base -Name "Language"   -Value "ENU"   -PropertyType String -Force | Out-Null
    New-ItemProperty -Path $base -Name "Patch"      -Value $ver    -PropertyType String -Force | Out-Null
    Write-Host "registry written: $base"
}

# Remove the wrong stock-path keys an earlier attempt created, so nothing is
# left lying around claiming a UO install that does not exist.
foreach ($stale in @("HKLM:\SOFTWARE\Origin Worlds Online",
                     "HKLM:\SOFTWARE\WOW6432Node\Origin Worlds Online",
                     "HKCU:\SOFTWARE\Origin Worlds Online",
                     "HKCU:\SOFTWARE\WOW6432Node\Origin Worlds Online")) {
    if (Test-Path $stale) { Remove-Item -Path $stale -Recurse -Force; Write-Host "removed stale: $stale" }
}

$rule = "RevolutionOffline-uoclient-block-outbound"
Get-NetFirewallRule -DisplayName $rule -ErrorAction SilentlyContinue | Remove-NetFirewallRule -ErrorAction SilentlyContinue
New-NetFirewallRule -DisplayName $rule -Direction Outbound -Program $exe `
                    -Action Block -Profile Any | Out-Null
Write-Host "firewall rule created: $rule (loopback is unaffected)"
Write-Host ""
Write-Host "Done. Launch with tools\launch_admin.bat or tools\launch_observer.bat"
