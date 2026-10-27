param(
    [string]$Port = "COM5",
    [int]$Baud = 115200,
    [string[]]$Steps = @(),   # each entry "command|delayMs"
    [int]$SettleMs = 1200,
    [switch]$Reset
)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200
$sp.WriteTimeout = 1000
$sp.NewLine = "`n"
try {
    $sp.Open()
    if ($Reset) {
        $sp.DtrEnable = $false
        $sp.RtsEnable = $true
        Start-Sleep -Milliseconds 120
        $sp.RtsEnable = $false
        Start-Sleep -Milliseconds 120
    }
    Start-Sleep -Milliseconds 400
    $sp.DiscardInBuffer()
    $all = New-Object System.Collections.Generic.List[string]
    foreach ($step in $Steps) {
        $parts = $step.Split('|')
        $cmd = $parts[0]
        $delay = if ($parts.Count -gt 1) { [int]$parts[1] } else { $SettleMs }
        $sp.WriteLine($cmd)
        $until = (Get-Date).AddMilliseconds($delay)
        while ((Get-Date) -lt $until) {
            try {
                $line = $sp.ReadLine().TrimEnd("`r", "`n")
                if ($line) { $all.Add($line) }
            } catch [System.TimeoutException] { }
        }
    }
    try {
        while ($sp.BytesToRead -gt 0) { $all.Add($sp.ReadLine().TrimEnd("`r", "`n")) }
    } catch [System.TimeoutException] { }
    $all
} finally {
    if ($sp.IsOpen) { $sp.Close() }
    $sp.Dispose()
}
