<#
.SYNOPSIS
  Installs PDF Bookmark for the current user and adds it to the user PATH,
  so "pdfbookmark" works in any new terminal. No administrator rights needed.

.PARAMETER Destination
  Install folder. Default: %LOCALAPPDATA%\Programs\PDF Bookmark

.PARAMETER NoPath
  Copy the files only; do not change PATH.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File install.ps1
#>
[CmdletBinding()]
param(
    [string]$Destination = (Join-Path $env:LOCALAPPDATA 'Programs\PDF Bookmark'),
    [switch]$NoPath,
    # HKCU subkey holding the user PATH. Only tests change this.
    [string]$EnvironmentKey = 'Environment'
)
$ErrorActionPreference = 'Stop'

$source = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$dest = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
if (-not (Test-Path -LiteralPath (Join-Path $source 'pdfbookmark.exe'))) {
    throw "pdfbookmark.exe was not found next to install.ps1 ($source). Extract the whole package first."
}

if ($dest -ne $source) {
    if (Test-Path -LiteralPath $dest) {
        $isInstall = Test-Path -LiteralPath (Join-Path $dest 'pdfbookmark.exe')
        $isEmpty = -not (Get-ChildItem -LiteralPath $dest -Force | Select-Object -First 1)
        if (-not $isInstall -and -not $isEmpty) {
            throw "'$dest' exists and is not a PDF Bookmark installation; choose another -Destination."
        }
        if ($isInstall) {
            Write-Host "Replacing the existing installation in $dest"
            Remove-Item -LiteralPath $dest -Recurse -Force
        }
    }
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Get-ChildItem -LiteralPath $source -Force |
        Copy-Item -Destination $dest -Recurse -Force
}

function Update-UserPath([string]$Folder, [string]$KeyName) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($KeyName)
    try {
        $kind = [Microsoft.Win32.RegistryValueKind]::ExpandString
        $current = ''
        if ($key.GetValueNames() -contains 'Path') {
            # Keep the value type and unexpanded %VARIABLES% of existing entries.
            $kind = $key.GetValueKind('Path')
            $current = [string]$key.GetValue('Path', '',
                [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        }
        $entries = @($current -split ';' | Where-Object { $_ -ne '' })
        $present = $entries | Where-Object { $_.TrimEnd('\') -ieq $Folder }
        if ($present) { return $false }
        $key.SetValue('Path', ((@($entries) + $Folder) -join ';'), $kind)
        return $true
    } finally {
        $key.Close()
    }
}

$pathChanged = $false
if (-not $NoPath) {
    $pathChanged = Update-UserPath $dest $EnvironmentKey
    if ($pathChanged -and $EnvironmentKey -eq 'Environment') {
        # Tell Explorer and new terminals that the environment changed.
        Add-Type -Namespace PdfBookmark -Name Native -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern System.IntPtr SendMessageTimeout(System.IntPtr hWnd, uint msg,
    System.UIntPtr wParam, string lParam, uint flags, uint timeout, out System.UIntPtr result);
'@
        $result = [UIntPtr]::Zero
        [void][PdfBookmark.Native]::SendMessageTimeout([IntPtr]0xffff, 0x1A,
            [UIntPtr]::Zero, 'Environment', 2, 5000, [ref]$result)
    }
}

$version = & (Join-Path $dest 'pdfbookmark.exe') --version
Write-Host ""
Write-Host "Installed $version to: $dest"
if ($NoPath) {
    Write-Host "PATH was not changed. Run it as: `"$dest\pdfbookmark.exe`" --help"
} elseif ($pathChanged) {
    Write-Host "Added to your PATH. Open a NEW terminal and run:  pdfbookmark --help"
} else {
    Write-Host "Already on your PATH. Run:  pdfbookmark --help"
}
Write-Host "To remove it later, run Uninstall.cmd in that folder."
