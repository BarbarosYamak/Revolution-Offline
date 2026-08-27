$ErrorActionPreference = 'Continue'
$log = 'C:\Projects\RevolutionOffline\local\dev\uo-viewer-esc.log'
$exe = 'C:\Projects\RevolutionOffline\bot\uo-client\build-m1\uo_viewer.exe'
$p = Start-Process -FilePath $exe -WorkingDirectory 'C:\Projects\RevolutionOffline\bot\uo-client\build-m1' `
     -ArgumentList '--skip-audit','--quit-after','120','--log',$log `
     -PassThru -RedirectStandardOutput 'C:\Projects\RevolutionOffline\local\dev\esc_stdout.txt' `
     -RedirectStandardError 'C:\Projects\RevolutionOffline\local\dev\esc_stderr.txt'
Write-Output "pid=$($p.Id)"

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
'@

$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 500
    $p.Refresh()
    if ($p.HasExited) { Write-Output "exited early"; break }
    if ($p.MainWindowHandle -ne [IntPtr]::Zero) { $hwnd = $p.MainWindowHandle; break }
}
if ($hwnd -eq [IntPtr]::Zero) { Write-Output "NO WINDOW"; if(-not $p.HasExited){$p.Kill()}; exit 1 }
Write-Output "hwnd=$hwnd title=$($p.MainWindowTitle)"

Start-Sleep -Seconds 3
# WM_KEYDOWN = 0x0100, VK_ESCAPE = 0x1B
[void][W]::PostMessage($hwnd, 0x0100, [IntPtr]0x1B, [IntPtr]0)
Write-Output "posted WM_KEYDOWN VK_ESCAPE"

$exited = $p.WaitForExit(20000)
Write-Output "exitedWithin20s=$exited"
if (-not $exited) { $p.Kill(); exit 2 }
Write-Output "exitcode=$($p.ExitCode)"
