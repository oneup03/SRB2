# Builds SRB2 (SDL/OpenGL target -> bin\VC10\Win32\<Config>\Srb2Win.exe).
#
# Builds src\sdl\Srb2SDL-vc10.vcxproj directly rather than srb2-vc10.sln: the
# solution references a couple of projects that don't exist in this checkout
# (zlib.vcxproj, Srb2win-vc10.vcxproj) plus the standalone r_opengl-vc10 DLL
# project which doesn't compile here. The SDL project compiles r_opengl.c
# itself (STATIC_OPENGL) and only really depends on libpng, so building it
# directly gives a clean exit code.
#
# Usage:
#   .\build.ps1                       # Release / Win32
#   .\build.ps1 -Configuration Debug  # Debug / Win32
#   .\build.ps1 -Rebuild              # clean + build
#
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [ValidateSet('Win32')]
    [string]$Platform = 'Win32',
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
$project  = Join-Path $repoRoot 'src\sdl\Srb2SDL-vc10.vcxproj'

# Locate MSBuild via vswhere so this works regardless of VS edition/version.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found at $vswhere - is Visual Studio installed?"
}
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild `
    -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) {
    Write-Error "MSBuild.exe not found via vswhere."
}

# $(SolutionDir) must point at the repo root (trailing backslash) so the
# comptime.bat prebuild event resolves - it lives at the repo root, not in
# src\sdl, and the prebuild command is "$(SolutionDir)comptime.bat".
$solutionDir = $repoRoot.TrimEnd('\') + '\'

$targets = if ($Rebuild) { 'Rebuild' } else { 'Build' }

Write-Host "MSBuild: $msbuild"
Write-Host "Building $Configuration|$Platform ($targets)..."

& $msbuild $project `
    /t:$targets `
    /m `
    /nologo `
    /verbosity:minimal `
    /p:Configuration=$Configuration `
    /p:Platform=$Platform `
    /p:SolutionDir=$solutionDir

$code = $LASTEXITCODE
if ($code -eq 0) {
    $exe = Join-Path $repoRoot "bin\VC10\$Platform\$Configuration\Srb2Win.exe"
    Write-Host "Build succeeded -> $exe" -ForegroundColor Green
} else {
    Write-Host "Build FAILED (exit $code)" -ForegroundColor Red
}
exit $code
