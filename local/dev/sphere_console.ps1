# Send a command to the running SphereServer console, the same way an operator
# types it into the server window: put the text in the console EDIT control and
# post the IDOK command the window handler uses (Source-X
# src/sphere/ntwindow.cpp:628-644 -> g_Serv.m_sConsoleText / OnConsoleCmd).
#
# This is server operation, not gameplay: it changes nothing in Source-X, in
# the scripts, or in any character's state that the game rules would not allow.
param([Parameter(Mandatory = $true)][string[]]$Commands, [int]$DelayMs = 1500)

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll", CharSet=CharSet.Auto)]
  public static extern IntPtr FindWindowEx(IntPtr p, IntPtr c, string cls, string win);
  [DllImport("user32.dll", CharSet=CharSet.Auto)]
  public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")]
  public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Auto)]
  public static extern int GetClassName(IntPtr h, StringBuilder s, int max);
  [DllImport("user32.dll")]
  public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
}
'@

$proc = Get-Process -Name SphereSvrX64_nightly -ErrorAction SilentlyContinue
if (-not $proc) { throw "SphereSvrX64_nightly is not running" }
$main = $proc.MainWindowHandle
if ($main -eq [IntPtr]::Zero) { throw "Sphere has no main window handle" }

# The console input line is the EDIT child of the main window.
$edit = [IntPtr]::Zero
$cb = [W+EnumProc] {
    param($h, $p)
    $sb = New-Object System.Text.StringBuilder 256
    [void][W]::GetClassName($h, $sb, 256)
    if ($sb.ToString() -eq 'Edit') { $script:edit = $h; return $false }
    return $true
}
[void][W]::EnumChildWindows($main, $cb, [IntPtr]::Zero)
if ($edit -eq [IntPtr]::Zero) { throw "console input (EDIT control) not found" }

$WM_SETTEXT = 0x000C
$WM_COMMAND = 0x0111
$IDOK = 1

foreach ($c in $Commands) {
    [void][W]::SendMessage($edit, $WM_SETTEXT, [IntPtr]::Zero, $c)
    [void][W]::SendMessage($main, $WM_COMMAND, [IntPtr]$IDOK, $edit)
    Write-Output "console> $c"
    Start-Sleep -Milliseconds $DelayMs
}
