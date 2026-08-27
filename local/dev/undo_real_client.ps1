# Reverses setup_real_client.ps1. Run ELEVATED.
foreach ($k in @("HKLM:\SOFTWARE\Revolution UO Shards",
                 "HKLM:\SOFTWARE\WOW6432Node\Revolution UO Shards",
                 "HKCU:\SOFTWARE\Revolution UO Shards",
                 "HKLM:\SOFTWARE\Origin Worlds Online",
                 "HKLM:\SOFTWARE\WOW6432Node\Origin Worlds Online",
                 "HKCU:\SOFTWARE\Origin Worlds Online",
                 "HKCU:\SOFTWARE\WOW6432Node\Origin Worlds Online")) {
    if (Test-Path $k) { Remove-Item -Path $k -Recurse -Force; Write-Host "removed $k" }
}
Get-NetFirewallRule -DisplayName "RevolutionOffline-uoclient-block-outbound" -ErrorAction SilentlyContinue | Remove-NetFirewallRule
Write-Host "reverted."
