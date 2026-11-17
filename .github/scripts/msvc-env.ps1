# Puts the Visual Studio x64 developer environment (MSVC, the Windows SDK, and the CMake and Ninja
# that ship with Visual Studio) into the environment of every later step of the job.
$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio with the x64 C++ tools on this runner.' }

$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
Write-Host "Using $vcvars"

# Run vcvars in cmd and copy every variable it sets into GITHUB_ENV.
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        "$($Matches[1])=$($Matches[2])" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
    }
}
