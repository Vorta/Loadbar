param([ValidateSet('Format', 'Tidy')][string]$Action, [string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$toolName = if ($Action -eq 'Format') { 'clang-format' } else { 'clang-tidy' }
$toolVersion = & $toolName --version
if (($toolVersion -join "`n") -notmatch '23\.1\.3') { throw "$toolName must be version 23.1.3" }
$files = Get-ChildItem (Join-Path $projectRoot 'src'),(Join-Path $projectRoot 'tests') -Recurse -File |
    Where-Object { $_.Extension -in '.cpp', '.hpp' } | Sort-Object FullName
$failedFiles = @()
if ($Action -eq 'Tidy') {
    $tidyDirectory = Join-Path $BuildDirectory 'tidy-commands'
    New-Item -ItemType Directory -Path $tidyDirectory -Force | Out-Null
    $commands = Get-Content -LiteralPath (Join-Path $BuildDirectory 'compile_commands.json') -Raw | ConvertFrom-Json
    foreach ($entry in $commands) {
        # Clang always uses its conforming preprocessor; only MSVC accepts this driver switch.
        # Keep the real compilation database and all project diagnostics unchanged.
        $entry.command = $entry.command -replace '(?<=\s)/Zc:preprocessor(?=\s|$)', ''
    }
    $commands | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $tidyDirectory 'compile_commands.json') -Encoding utf8
}
foreach ($file in $files) {
    if ($Action -eq 'Format') {
        & clang-format --dry-run --Werror $file.FullName
    } elseif ($file.Extension -eq '.cpp') {
        & clang-tidy -p $tidyDirectory $file.FullName
    } else { continue }
    if ($LASTEXITCODE -ne 0) { $failedFiles += $file.FullName }
}
if ($failedFiles.Count) { throw "$toolName failed for $($failedFiles.Count) file(s): $($failedFiles -join ', ')" }
