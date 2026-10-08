# Isolated incremental resource regression. Never launches Loadbar or changes the live desktop.
param([ValidateSet('Manifest', 'License')][string]$Resource = 'Manifest')
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path -Parent $PSScriptRoot
$checkRoot = Join-Path $sourceRoot ('out/build/' + $Resource.ToLowerInvariant() + '-check-' + [Guid]::NewGuid().ToString('N'))
$copyRoot = Join-Path $checkRoot 'source'
New-Item -ItemType Directory -Path $copyRoot -Force | Out-Null
foreach ($item in @('CMakeLists.txt','CMakePresets.json','LICENSE','src','tests','resources','cmake','scripts')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $item) -Destination $copyRoot -Recurse
}
Push-Location $copyRoot
try {
    cmake --preset windows-x64-debug
    if ($LASTEXITCODE -ne 0) { throw 'Isolated configure failed' }
    if ($Resource -eq 'License') {
        cmake --build --preset windows-x64-debug --target loadbar_tests
        if ($LASTEXITCODE -ne 0) { throw 'Initial isolated license build failed' }
        ctest --preset windows-x64-debug -R loadbar-resources --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'Initial embedded license mismatch' }
        $licensePath = Join-Path $copyRoot 'LICENSE'
        $marker = 'Loadbar license dependency probe ' + [Guid]::NewGuid().ToString('N')
        [IO.File]::AppendAllText($licensePath, "`n$marker`n", [Text.UTF8Encoding]::new($false))
        # The exact-byte test must detect the stale executable before the incremental build.
        $staleCheck = & ctest --preset windows-x64-debug -R loadbar-resources --output-on-failure 2>&1
        if ($LASTEXITCODE -eq 0 -or ($staleCheck -join "`n") -notmatch 'Embedded notice differs from source license') {
            throw 'Resource checker failed to detect the deliberately stale license'
        }
        cmake --build --preset windows-x64-debug --target loadbar_tests
        if ($LASTEXITCODE -ne 0) { throw 'License-only incremental build failed' }
        ctest --preset windows-x64-debug -R loadbar-resources --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw 'License-only edit did not reach the executable' }
        Write-Output "PASS: stale license rejected; license-only edit rebuilt embedded bytes. Evidence: $checkRoot"
        return
    }
    cmake --build --preset windows-x64-debug --target loadbar loadbar_settings_tests
    if ($LASTEXITCODE -ne 0) { throw 'Initial isolated build failed' }
    $artifacts = @('Loadbar.exe','loadbar_settings_tests.exe')
    $manifestPath = Join-Path $copyRoot 'resources/loadbar.manifest'
    $marker = 'Loadbar manifest dependency probe ' + [Guid]::NewGuid().ToString('N')
    $manifestText = [IO.File]::ReadAllText($manifestPath)
    if (-not $manifestText.Contains('</assembly>')) { throw 'Manifest root missing' }
    [IO.File]::WriteAllText($manifestPath,
        $manifestText.Replace('</assembly>', "<description>$marker</description></assembly>"),
        [Text.UTF8Encoding]::new($false))
    # Only the copied manifest changed. No touch of CMakeLists or other sources can force linking.
    cmake --build --preset windows-x64-debug --target loadbar loadbar_settings_tests
    if ($LASTEXITCODE -ne 0) { throw 'Manifest-only incremental build failed' }
    foreach ($artifact in $artifacts) {
        $artifactPath = Join-Path $copyRoot ('out/build/windows-x64-debug/' + $artifact)
        $extractedPath = Join-Path $checkRoot ($artifact + '.manifest')
        & mt.exe "-inputresource:$artifactPath;#1" "-out:$extractedPath"
        if ($LASTEXITCODE -ne 0) { throw "Manifest extraction failed: $artifact" }
        [xml]$embedded = [IO.File]::ReadAllText($extractedPath)
        $descriptions = $embedded.SelectNodes('//*[local-name()="description"]')
        if (-not ($descriptions | Where-Object { $_.InnerText -eq $marker })) {
            throw "Manifest-only edit did not reach $artifact"
        }
    }
    Write-Output "PASS: manifest-only edit reached both executables; original source untouched. Evidence: $checkRoot"
}
finally {
    Pop-Location
}
