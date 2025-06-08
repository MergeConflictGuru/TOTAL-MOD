# AttachVS.ps1
$proc = Get-Process -Name gothic2 -ErrorAction SilentlyContinue
if ($proc) {
    $procId = $proc.Id
    try {
        $dte = [Runtime.InteropServices.Marshal]::GetActiveObject('VisualStudio.DTE.17.0')
        $dbg = $dte.Debugger
        $procToAttach = $dbg.LocalProcesses | Where-Object { $_.ProcessID -eq $procId }
        if ($procToAttach) {
            $procToAttach.Attach()
            Write-Host "Attached Visual Studio to gothic2.exe"
        }
        else {
            Write-Host "Process not found in Visual Studio debugger list"
        }
    }
    catch {
        Write-Host "Visual Studio not running or DTE not accessible"
    }
}
else {
    Write-Host "gothic2.exe process not found"
}