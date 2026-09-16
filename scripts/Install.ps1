[CmdletBinding()]
param([string]$SourceDirectory)
if ([string]::IsNullOrWhiteSpace($SourceDirectory)) {
    $SourceDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
}
. (Join-Path $PSScriptRoot 'Common.ps1')
Assert-X64
$source = [IO.Path]::GetFullPath($SourceDirectory)
$required = @('DDSThumbnail.dll', 'dds-preview.exe', 'Install.ps1', 'Uninstall.ps1', 'Common.ps1', 'README.md', 'licenses\DirectXTex-LICENSE.txt')
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $file) -PathType Leaf)) { throw "Missing package file: $file" }
}
$hashes = ($required | ForEach-Object { (Get-FileHash -LiteralPath (Join-Path $source $_) -Algorithm SHA256).Hash }) -join ''
$sha = [Security.Cryptography.SHA256]::Create()
try { $hash = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($hashes)))).Replace('-', '').ToLowerInvariant().Substring(0,16) }
finally { $sha.Dispose() }
$destination = Join-Path $script:InstallRoot "1.0.0-$hash"
$null = Assert-OwnedDirectory $destination
New-Item -ItemType Directory -Path (Join-Path $destination 'licenses') -Force | Out-Null
foreach ($file in $required) {
    $from = Join-Path $source $file
    $to = Join-Path $destination $file
    if ((Test-Path -LiteralPath $to) -and (Get-FileHash -LiteralPath $to).Hash -eq (Get-FileHash -LiteralPath $from).Hash) { continue }
    Copy-Item -LiteralPath $from -Destination $to -Force
}
$oldState = if (Test-Path -LiteralPath $script:StatePath) { Get-Content -LiteralPath $script:StatePath -Raw | ConvertFrom-Json } else { $null }
if ($null -ne $oldState -and ($oldState.Version -ne 1 -or $oldState.ProviderId -ne $script:ProviderId)) {
    throw 'Unrecognized installation state.'
}
$oldStateText = if ($null -ne $oldState) { Get-Content -LiteralPath $script:StatePath -Raw } else { $null }
$handlerBefore = Get-RegistryValue $script:HandlerKey
$classBefore = Get-RegistryValue $script:ClassKey
$serverBefore = Get-RegistryValue "$script:ClassKey\InprocServer32"
$threadingBefore = Get-RegistryValue "$script:ClassKey\InprocServer32" 'ThreadingModel'
if ($handlerBefore.Present -and $handlerBefore.Value -eq $script:ProviderId -and $null -eq $oldState) {
    throw 'Provider is already registered but its backup state is missing. Restore registration.json before reinstalling.'
}
$previous = if ($null -ne $oldState -and $handlerBefore.Present -and $handlerBefore.Value -eq $script:ProviderId) { $oldState.PreviousHandler } else { $handlerBefore }
$directories = @($destination)
if ($null -ne $oldState) { $directories += @($oldState.Directories) }
$state = [ordered]@{ Version = 1; ProviderId = $script:ProviderId; PreviousHandler = $previous; Directories = @($directories | Select-Object -Unique); ActiveDirectory = $destination }
try {
    $state | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$script:StatePath.tmp" -Encoding UTF8
    Move-Item -LiteralPath "$script:StatePath.tmp" -Destination $script:StatePath -Force
    Set-RegistryString $script:ClassKey '' 'DDS Thumbnail Provider'
    Set-RegistryString "$script:ClassKey\InprocServer32" '' (Join-Path $destination 'DDSThumbnail.dll')
    Set-RegistryString "$script:ClassKey\InprocServer32" 'ThreadingModel' 'Apartment'
    Set-RegistryString $script:HandlerKey '' $script:ProviderId
} catch {
    Set-RegistryValue $script:HandlerKey '' $handlerBefore
    Set-RegistryValue "$script:ClassKey\InprocServer32" 'ThreadingModel' $threadingBefore
    Set-RegistryValue "$script:ClassKey\InprocServer32" '' $serverBefore
    Set-RegistryValue $script:ClassKey '' $classBefore
    Remove-EmptyRegistryKey "$script:ClassKey\InprocServer32"
    Remove-EmptyRegistryKey $script:ClassKey
    Remove-EmptyRegistryKey $script:HandlerKey
    if ($null -ne $oldStateText) { Set-Content -LiteralPath $script:StatePath -Value $oldStateText -Encoding UTF8 }
    elseif (Test-Path -LiteralPath $script:StatePath) { Remove-Item -LiteralPath $script:StatePath }
    throw
}
Notify-ThumbnailAssociation
Write-Output "Installed DDS thumbnails for this user: $destination"
Write-Output 'Reopen the DDS folder in Large or Extra large icons view. Existing cached icons may need a thumbnail-cache refresh; see README.md.'
