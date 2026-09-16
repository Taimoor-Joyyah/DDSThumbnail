# Explicit integration test; changes only this provider's registration, then
# restores the user's original .dds thumbnail association in a finally block.
[CmdletBinding()]
param([string]$PackageDirectory)
if ([string]::IsNullOrWhiteSpace($PackageDirectory)) {
    $PackageDirectory = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\dist'
}
. (Join-Path $PSScriptRoot '..\scripts\Common.ps1')
Assert-X64
if (Test-Path -LiteralPath $script:StatePath) { throw 'Uninstall the test provider before running registration tests.' }
$package = [IO.Path]::GetFullPath($PackageDirectory)
$original = Get-RegistryValue $script:HandlerKey
$originalDefaultApp = Get-RegistryValue 'Software\Classes\.dds'
$sentinel = '{11111111-2222-3333-4444-555555555555}'
$replacement = '{55555555-4444-3333-2222-111111111111}'
function Assert-Equal($actual, $expected, [string]$description) {
    if ($actual -ne $expected) { throw "Failed: $description" }
    Write-Output "PASS: $description"
}
try {
    Set-RegistryString $script:HandlerKey '' $sentinel
    & (Join-Path $package 'Install.ps1')
    Assert-Equal (Get-RegistryValue $script:HandlerKey).Value $script:ProviderId 'handler registration'
    $first = Get-Content -LiteralPath $script:StatePath -Raw | ConvertFrom-Json
    Assert-Equal $first.PreviousHandler.Value $sentinel 'previous handler backed up'
    & (Join-Path $package 'Install.ps1')
    $again = Get-Content -LiteralPath $script:StatePath -Raw | ConvertFrom-Json
    Assert-Equal $again.PreviousHandler.Value $sentinel 'reinstall preserves original backup'
    Assert-Equal $again.ActiveDirectory $first.ActiveDirectory 'identical reinstall uses same directory'
    & (Join-Path $package 'Uninstall.ps1')
    Assert-Equal (Get-RegistryValue $script:HandlerKey).Value $sentinel 'uninstall restores previous handler'
    Assert-Equal (Get-RegistryValue "$script:ClassKey\InprocServer32").Present $false 'uninstall removes COM server'
    Assert-Equal (Test-Path -LiteralPath $script:StatePath) $false 'uninstall cleans state'
    & (Join-Path $package 'Install.ps1')
    Set-RegistryString $script:HandlerKey '' $replacement
    & (Join-Path $package 'Uninstall.ps1')
    Assert-Equal (Get-RegistryValue $script:HandlerKey).Value $replacement 'later handler change is preserved'
    $after = Get-RegistryValue 'Software\Classes\.dds'
    Assert-Equal ($after | ConvertTo-Json -Compress) ($originalDefaultApp | ConvertTo-Json -Compress) 'default application preserved'
} finally {
    if (Test-Path -LiteralPath $script:StatePath) { & (Join-Path $package 'Uninstall.ps1') }
    Set-RegistryValue $script:HandlerKey '' $original
    Remove-EmptyRegistryKey $script:HandlerKey
    Notify-ThumbnailAssociation
}
