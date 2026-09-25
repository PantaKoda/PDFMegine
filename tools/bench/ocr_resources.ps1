# Runs one command and reports its elapsed time, peak private bytes, peak
# working set and system commit (before -> sampled peak). Used for PDFMegine
# issue #1; see docs/BUILDING.md section 8.
#
#   pwsh tools/bench/ocr_resources.ps1 <label> <exe> <args...>
#
# Samples the process every 50 ms and the system commit every 500 ms, so very
# short spikes can be missed; stderr of the command is saved to <label>.stderr.txt.
param([string]$Label, [string]$Exe, [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest)

function Commit-GB {
    $os = Get-CimInstance Win32_OperatingSystem
    return [math]::Round(($os.TotalVirtualMemorySize - $os.FreeVirtualMemory) / 1MB, 2)
}

$env:OPENCV_LOG_LEVEL = 'ERROR'
$commitBefore = Commit-GB
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Exe
foreach ($a in $Rest) { [void]$psi.ArgumentList.Add($a) }
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$p = [System.Diagnostics.Process]::Start($psi)
$null = $p.StandardOutput.ReadToEndAsync()
$errTask = $p.StandardError.ReadToEndAsync()
$peakPrivate = 0; $peakWs = 0; $peakCommit = $commitBefore; $n = 0
while (-not $p.HasExited) {
    try {
        $p.Refresh()
        if ($p.PrivateMemorySize64 -gt $peakPrivate) { $peakPrivate = $p.PrivateMemorySize64 }
        if ($p.WorkingSet64 -gt $peakWs) { $peakWs = $p.WorkingSet64 }
    } catch {}
    if (($n++ % 10) -eq 0) { $c = Commit-GB; if ($c -gt $peakCommit) { $peakCommit = $c } }
    Start-Sleep -Milliseconds 50
}
$sw.Stop()
$p.WaitForExit()
"{0,-28} exit={1} time={2,7:N1}s peakPrivate={3,6:N2}GB peakWS={4,6:N2}GB commit {5:N1}->{6:N1}GB" -f `
    $Label, $p.ExitCode, $sw.Elapsed.TotalSeconds, ($peakPrivate / 1GB), ($peakWs / 1GB), $commitBefore, $peakCommit
$errTask.Result | Set-Content -Encoding utf8 "$Label.stderr.txt"
