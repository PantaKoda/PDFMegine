<#
.SYNOPSIS
  Removes PDF Bookmark: takes its folder off the user PATH and deletes the
  installation folder (the folder containing this script).

.PARAMETER Yes
  Do not ask for confirmation.
#>
[CmdletBinding()]
param(
    [switch]$Yes,
    # HKCU subkey holding the user PATH. Only tests change this.
    [string]$EnvironmentKey = 'Environment'
)
$ErrorActionPreference = 'Stop'

$folder = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
if (-not (Test-Path -LiteralPath (Join-Path $folder 'pdfbookmark.exe'))) {
    throw "'$folder' does not contain pdfbookmark.exe; nothing was removed."
}
if (-not $Yes) {
    $answer = Read-Host "Remove PDF Bookmark from '$folder'? [y/N]"
    if ($answer -notmatch '^(y|yes)$') { Write-Host 'Cancelled.'; return }
}

$key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($EnvironmentKey, $true)
if ($key) {
    try {
        if ($key.GetValueNames() -contains 'Path') {
            $kind = $key.GetValueKind('Path')
            $current = [string]$key.GetValue('Path', '',
                [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            $entries = @($current -split ';' | Where-Object { $_ -ne '' })
            $kept = @($entries | Where-Object { $_.TrimEnd('\') -ine $folder })
            if ($kept.Count -ne $entries.Count) {
                $key.SetValue('Path', ($kept -join ';'), $kind)
                Write-Host "Removed $folder from your PATH."
            }
        }
    } finally {
        $key.Close()
    }
}

# Leave the folder before deleting it.
Set-Location -LiteralPath $env:TEMP
Remove-Item -LiteralPath $folder -Recurse -Force
Write-Host "PDF Bookmark was removed. Open a new terminal for PATH changes to apply."
