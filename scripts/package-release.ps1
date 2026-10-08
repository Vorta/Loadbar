# Build and stage local release files. Does not commit, tag, upload, or launch Loadbar.
param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    . ./scripts/enter-dev-shell.ps1
    cmake --preset windows-x64-release
    if ($LASTEXITCODE -ne 0) { throw 'Release configure failed' }
    cmake --build --preset windows-x64-release
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
    ctest --preset windows-x64-release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Release tests failed' }
    $binary = Get-Item -LiteralPath 'out/build/windows-x64-release/Loadbar.exe'
    $version = $binary.VersionInfo.ProductVersion
    if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid embedded release version' }
    $stage = Join-Path $projectRoot "out/release/$version"
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    $executable = Join-Path $stage 'Loadbar.exe'
    $archive = Join-Path $stage "Loadbar-$version-windows-x64.zip"
    Copy-Item -LiteralPath $binary.FullName -Destination $executable -Force
    Compress-Archive -LiteralPath $executable -DestinationPath $archive -CompressionLevel Optimal -Force
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        if ($zip.Entries.Count -ne 1 -or $zip.Entries[0].FullName -ne 'Loadbar.exe') {
            throw 'Release archive must contain only Loadbar.exe'
        }
        $stream = $zip.Entries[0].Open()
        try {
            $packedHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))
            if ($packedHash -ne (Get-FileHash -LiteralPath $executable).Hash) {
                throw 'Archived executable differs from staged executable'
            }
        } finally { $stream.Dispose() }
    } finally { $zip.Dispose() }
    $checksums = @($executable, $archive) | ForEach-Object {
        $hash = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $([IO.Path]::GetFileName($_))"
    }
    $checksums | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding utf8
    Write-Output "Local release files: $stage"
    $checksums
} finally { Pop-Location }
