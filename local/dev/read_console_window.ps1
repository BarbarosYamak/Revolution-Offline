# Read the text of the SphereServer console window's output control.
# Companion to sphere_console.ps1 (which only WRITES commands): SHOW and
# other query verbs print to the console window, not to the log file, so
# diagnosing live object state needs a way to READ that window.
param([int]$TailLines = 40)

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class WR {
  [DllImport("user32.dll", CharSet=CharSet.Auto)]
  public static extern int GetClassName(IntPtr h, StringBuilder s, int max);
  [DllImport("user32.dll")]
  public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
  [DllImport("user32.dll", CharSet=CharSet.Auto)]
  public static extern int SendMessage(IntPtr h, uint m, int w, StringBuilder l);
  [DllImport("user32.dll", CharSet=CharSet.Auto)]
  public static extern int SendMessage(IntPtr h, uint m, int w, int l);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
}
'@

$proc = Get-Process -Name SphereSvrX64_nightly -ErrorAction SilentlyContinue
if (-not $proc) { throw "SphereSvrX64_nightly is not running" }
$main = $proc.MainWindowHandle
if ($main -eq [IntPtr]::Zero) { throw "Sphere has no main window handle" }

$script:controls = @()
$cb = [WR+EnumProc] {
    param($h, $p)
    $sb = New-Object System.Text.StringBuilder 256
    [void][WR]::GetClassName($h, $sb, 256)
    $script:controls += [pscustomobject]@{ Handle = $h; Class = $sb.ToString() }
    return $true
}
[void][WR]::EnumChildWindows($main, $cb, [IntPtr]::Zero)

$WM_GETTEXT = 0x000D
$WM_GETTEXTLENGTH = 0x000E

foreach ($c in $script:controls) {
    if ($c.Class -match 'RICHEDIT|RichEdit|Edit') {
        $len = [WR]::SendMessage($c.Handle, $WM_GETTEXTLENGTH, 0, 0)
        if ($len -gt 10) {   # skip the (short) input line
            $sb = New-Object System.Text.StringBuilder ($len + 1)
            [void][WR]::SendMessage($c.Handle, $WM_GETTEXT, $len + 1, $sb)
            $text = $sb.ToString() -split "\r?\n"
            Write-Output ("=== {0} ({1} lines) ===" -f $c.Class, $text.Count)
            $text | Select-Object -Last $TailLines | Write-Output
        }
    }
}
