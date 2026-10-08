$ErrorActionPreference = 'Stop'
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio C++ tools were not found' }
$devCmdPath = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
if ($devCmdPath.Contains('"')) { throw 'Invalid developer command path' }
$devCommand = 'call "' + $devCmdPath + '" -no_logo -arch=x64 -host_arch=x64 -winsdk=10.0.28000.0 -vcvars_ver=14.51.36231 && set'
$environmentLines = & $env:ComSpec /d /c $devCommand
if ($LASTEXITCODE -ne 0) { throw 'Developer environment initialization failed' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}
if ($env:WindowsSDKVersion -ne '10.0.28000.0\' -or $env:VCToolsVersion -ne '14.51.36231') {
    throw 'The pinned SDK/toolset is not active'
}
