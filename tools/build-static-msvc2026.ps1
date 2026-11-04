param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [string]$BuildDirectory = 'build-msvc2026-static',
    [string]$OutputDirectory = 'dist\LlamaStudio-msvc2026-x64-static',
    [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
function Check-Exit([string]$step) { if ($LASTEXITCODE -ne 0) { throw "$step failed (exit $LASTEXITCODE)" } }
function Full-Path([string]$path) { if ([IO.Path]::IsPathRooted($path)) { return [IO.Path]::GetFullPath($path) }; return [IO.Path]::GetFullPath((Join-Path $projectRoot $path)) }
$QtRoot = [IO.Path]::GetFullPath($QtRoot)
if (!(Test-Path -LiteralPath (Join-Path $QtRoot 'lib\cmake'))) { throw 'QtRoot must point to a static MSVC x64 Qt installation (with Widgets, Network and Test).' }
$qconfig = Join-Path $QtRoot 'mkspecs\qconfig.pri'
if (Test-Path -LiteralPath $qconfig) {
    $qtConfiguration = Get-Content -LiteralPath $qconfig -Raw
    if ($qtConfiguration -match '(?m)^QT_GCC_MAJOR_VERSION\s*=\s*\d+') { throw 'A MinGW Qt kit cannot be linked with MSVC. Select a static MSVC x64 kit.' }
    if ($qtConfiguration -match '(?m)^QT_ARCH\s*=\s*(\S+)' -and $Matches[1] -notin @('x86_64','amd64')) { throw 'This script requires an x64 Qt kit.' }
}
if ($Jobs -lt 1) { throw 'Jobs must be positive' }
$version = & cmake --version
Check-Exit 'Find CMake'
if ($version[0] -notmatch '(\d+\.\d+\.\d+)' -or [version]$Matches[1] -lt [version]'4.2.0') { throw 'Visual Studio 2026 requires CMake 4.2 or newer' }
$buildRoot = Full-Path $BuildDirectory
$outputPath = Full-Path $OutputDirectory
# A separate build tree is required when switching ABI/compiler from MinGW.
& cmake -S $projectRoot -B $buildRoot -G 'Visual Studio 18 2026' -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot" '-DLLAMA_STUDIO_STATIC_MSVC=ON' '-DBUILD_TESTING=ON'
Check-Exit 'Configure MSVC 2026 static x64'
& cmake --build $buildRoot --config Release --parallel $Jobs
Check-Exit 'Build'
& ctest --test-dir $buildRoot -C Release --output-on-failure
Check-Exit 'Tests'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'vswhere.exe is required for dumpbin dependency verification' }
$vsRoot = & $vswhere -latest -products '*' -version '[18.0,19.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Check-Exit 'Find Visual Studio 2026'
if (!$vsRoot) { throw 'Visual Studio 2026 C++ tools not found' }
$toolVersion = (Get-Content -LiteralPath (Join-Path $vsRoot 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
$dumpbin = Join-Path $vsRoot "VC\Tools\MSVC\$toolVersion\bin\Hostx64\x64\dumpbin.exe"
$exe = Join-Path $buildRoot 'Release\LlamaStudio.exe'
$imports = & $dumpbin /DEPENDENTS $exe
Check-Exit 'Read PE imports'
if ($imports -match '(?i)(Qt[56]\w*\.dll|vcruntime\w*\.dll|msvcp\w*\.dll|libssl\w*\.dll|libcrypto\w*\.dll|ucrtbase\.dll|api-ms-win-crt-)') { throw 'Unexpected dynamic Qt/OpenSSL/MSVC runtime dependency; use Qt built with -static -static-runtime (and Schannel for TLS).' }
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
Copy-Item -LiteralPath $exe -Destination (Join-Path $outputPath 'LlamaStudio.exe') -Force
$imports | Set-Content -LiteralPath (Join-Path $outputPath 'imports.txt') -Encoding utf8
foreach ($relative in @('resources\parameters.json','resources\build-commands.json','resources\llama.cpp-LICENSE','README.md','ROADMAP.md','STATIC-BUILD.md','THIRD-PARTY-NOTICES.md','CHANGELOG.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $relative) -Destination $outputPath -Force
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'resources\licenses') -Destination $outputPath -Recurse -Force
Write-Output "MSVC 2026 x64 static build: $outputPath\LlamaStudio.exe"
