# Run a Ghidra script from tools/ghidra_scripts against the analysed project (read-only, no re-analysis) and print
# its output lines.   powershell -File tools/ghidra_query.ps1 <Script.java> [args...]
# Full Ghidra output: ..\ghidra\query.log
param([Parameter(Mandatory = $true)][string]$Script, [Parameter(ValueFromRemainingArguments = $true)][string[]]$ScriptArgs)
$sugma = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$ghidra = Join-Path $sugma "tools\ghidra_12.1.4_PUBLIC"
$work = Join-Path $sugma "ghidra"
$program = (Get-ChildItem $work -Filter "D2RLoader_*_dump.exe" | Sort-Object LastWriteTime -Descending | Select-Object -First 1).Name
$log = Join-Path $work "query.log"
$env:GHIDRA_HEADLESS_MAXMEM = "6G"
$argLine = @("`"$(Join-Path $work 'project')`"", "D2R", "-process", $program, "-noanalysis", "-readOnly",
             "-scriptPath", "`"$(Join-Path $PSScriptRoot 'ghidra_scripts')`"", "-postScript", $Script) + $ScriptArgs
$p = Start-Process -FilePath (Join-Path $ghidra "support\analyzeHeadless.bat") -ArgumentList $argLine -NoNewWindow `
    -Wait -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
Get-Content $log | Where-Object { $_ -match "\.java> " } | ForEach-Object { $_ -replace '^.*?\.java> ', '' -replace '\s+\(GhidraScript\)\s*$', '' }
Get-Content $log | Where-Object { $_ -match "ERROR|Exception" } | Select-Object -First 5
exit $p.ExitCode
