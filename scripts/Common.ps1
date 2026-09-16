Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:ProviderId = '{9FB9E3A6-57EF-4A20-9377-2E8C546A09DB}'
$script:HandlerKey = 'Software\Classes\.dds\shellex\{E357FCCD-A995-4576-B01F-234630154E96}'
$script:ClassKey = "Software\Classes\CLSID\$script:ProviderId"
$script:InstallRoot = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'DDSThumbnail'))
$script:StatePath = Join-Path $script:InstallRoot 'registration.json'

function Assert-X64 {
    if (-not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
        throw 'Run this script from x64 PowerShell on Windows x64.'
    }
}
function Open-UserRegistry {
    return [Microsoft.Win32.RegistryKey]::OpenBaseKey('CurrentUser', 'Registry64')
}
function Get-RegistryValue([string]$Path, [string]$Name = '') {
    $base = Open-UserRegistry
    $key = $base.OpenSubKey($Path)
    try {
        if ($null -ne $key -and $key.GetValueNames() -contains $Name) {
            return [pscustomobject]@{ Present = $true; Value = $key.GetValue($Name, $null, 'DoNotExpandEnvironmentNames'); Kind = $key.GetValueKind($Name).ToString() }
        }
        return [pscustomobject]@{ Present = $false; Value = $null; Kind = 'String' }
    } finally { if ($null -ne $key) { $key.Dispose() }; $base.Dispose() }
}
function Set-RegistryValue([string]$Path, [string]$Name, $Snapshot) {
    $base = Open-UserRegistry
    $key = $null
    try {
        if ($Snapshot.Present) {
            $key = $base.CreateSubKey($Path)
            $kind = [Enum]::Parse([Microsoft.Win32.RegistryValueKind], $Snapshot.Kind)
            $key.SetValue($Name, $Snapshot.Value, $kind)
        } else {
            $key = $base.OpenSubKey($Path, $true)
            if ($null -ne $key) { $key.DeleteValue($Name, $false) }
        }
    } finally { if ($null -ne $key) { $key.Dispose() }; $base.Dispose() }
}
function Set-RegistryString([string]$Path, [string]$Name, [string]$Value) {
    Set-RegistryValue $Path $Name ([pscustomobject]@{ Present = $true; Value = $Value; Kind = 'String' })
}
function Remove-EmptyRegistryKey([string]$Path) {
    $base = Open-UserRegistry
    $key = $base.OpenSubKey($Path)
    try {
        if ($null -ne $key -and $key.ValueCount -eq 0 -and $key.SubKeyCount -eq 0) {
            $key.Dispose(); $key = $null
            $base.DeleteSubKey($Path, $false)
        }
    } finally { if ($null -ne $key) { $key.Dispose() }; $base.Dispose() }
}
function Notify-ThumbnailAssociation {
    if (-not ('DdsThumbnail.ShellNotification' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace DdsThumbnail {
    public static class ShellNotification {
        [DllImport("shell32.dll")]
        public static extern void SHChangeNotify(uint eventId, uint flags, IntPtr item1, IntPtr item2);
    }
}
'@
    }
    [DdsThumbnail.ShellNotification]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
}
function Assert-OwnedDirectory([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($script:InstallRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the installation directory: $full"
    }
    $relative = $full.Substring($script:InstallRoot.Length + 1)
    if ($relative -notmatch '^1\.0\.0-[a-f0-9]{16}$') { throw "Unrecognized package directory: $full" }
    if ((Test-Path -LiteralPath $script:InstallRoot) -and
        ((Get-Item -LiteralPath $script:InstallRoot).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'The installation root must not be a reparse point.'
    }
    # Never follow a junction/reparse point while removing an installation.
    if (Test-Path -LiteralPath $full) {
        $entries = @((Get-Item -LiteralPath $script:InstallRoot), (Get-Item -LiteralPath $full))
        $entries += @(Get-ChildItem -LiteralPath $full -Force -Recurse)
        foreach ($entry in $entries) {
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing reparse point: $($entry.FullName)" }
        }
    }
    return $full
}
