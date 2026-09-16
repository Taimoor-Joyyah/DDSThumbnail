[CmdletBinding()]
param()
. (Join-Path $PSScriptRoot 'Common.ps1')
Assert-X64
if (-not (Test-Path -LiteralPath $script:StatePath)) { Write-Output 'No installation state found; nothing was changed.'; return }
$state = Get-Content -LiteralPath $script:StatePath -Raw | ConvertFrom-Json
if ($state.Version -ne 1 -or $state.ProviderId -ne $script:ProviderId) { throw 'Unrecognized installation state.' }
# Validate every target before changing registration or deleting any files.
$directories = @($state.Directories | ForEach-Object { Assert-OwnedDirectory $_ })
$server = Get-RegistryValue "$script:ClassKey\InprocServer32"
$ownedServers = @($directories | ForEach-Object { Join-Path $_ 'DDSThumbnail.dll' })
if ($server.Present -and $ownedServers -notcontains $server.Value) {
    throw 'The provider CLSID now points to an unrecognized installation. Its files and registration were preserved.'
}
$handler = Get-RegistryValue $script:HandlerKey
if ($handler.Present -and $handler.Value -eq $script:ProviderId) {
    Set-RegistryValue $script:HandlerKey '' $state.PreviousHandler
    Remove-EmptyRegistryKey $script:HandlerKey
}
if ($server.Present -and $ownedServers -contains $server.Value) {
    $absent = [pscustomobject]@{ Present = $false; Value = $null; Kind = 'String' }
    Set-RegistryValue "$script:ClassKey\InprocServer32" '' $absent
    Set-RegistryValue "$script:ClassKey\InprocServer32" 'ThreadingModel' $absent
    Remove-EmptyRegistryKey "$script:ClassKey\InprocServer32"
    Set-RegistryValue $script:ClassKey '' $absent
    Remove-EmptyRegistryKey $script:ClassKey
}
Notify-ThumbnailAssociation
$remaining = @()
foreach ($directory in $directories) {
    if (Test-Path -LiteralPath $directory) {
        try { Remove-Item -LiteralPath $directory -Recurse -Force -ErrorAction Stop }
        catch { $remaining += $directory; Write-Warning "Files are still in use: $directory. Run this uninstall script from the original package after signing out and back in." }
    }
}
if ($remaining.Count) {
    $state.Directories = $remaining
    $state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $script:StatePath -Encoding UTF8
} else { Remove-Item -LiteralPath $script:StatePath -Force }
Write-Output 'DDS thumbnail provider unregistered. Any previous per-user thumbnail handler was restored if this provider still owned the association.'
