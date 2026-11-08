param(
    [string]$QtRoot = 'E:\Qt_installer\QtStaticSSL',
    [string]$CompilerBin = 'E:\Qt\Qt5.14.2\Tools\mingw730_32\bin',
    [string]$MsysRoot = 'C:\msys64',
    [string]$BuildDirectory = 'build-static',
    [string]$OutputDirectory = 'dist\LlamaStudio-static'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$dependencyRoot = Join-Path $projectRoot 'build-static-deps'
$sourceRoot = Join-Path $dependencyRoot 'openssl-OpenSSL_1_1_1w'
$buildRoot = if([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $projectRoot $BuildDirectory }
$outputPath = if([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $projectRoot $OutputDirectory }
$env:PATH = "$CompilerBin;$env:PATH"
function Check-Exit([string]$step) {
    if ($LASTEXITCODE -ne 0) { throw "$step failed (exit $LASTEXITCODE)" }
}
function Unix-Path([string]$path) {
    $resolved = [IO.Path]::GetFullPath($path).Replace('\', '/')
    if ($resolved -notmatch '^([A-Za-z]):/(.*)$') { throw "Expected a local drive path: $path" }
    return '/' + $Matches[1].ToLowerInvariant() + '/' + $Matches[2]
}
New-Item -ItemType Directory -Force $dependencyRoot | Out-Null
if (!(Test-Path -LiteralPath (Join-Path $sourceRoot 'libssl.a')) -or !(Test-Path -LiteralPath (Join-Path $sourceRoot 'libcrypto.a'))) {
    $archive = Join-Path $dependencyRoot 'openssl-1.1.1w.tar.gz'
    if (!(Test-Path -LiteralPath $archive)) {
        Invoke-WebRequest 'https://github.com/openssl/openssl/archive/refs/tags/OpenSSL_1_1_1w.tar.gz' -OutFile $archive
    }
    $expectedHash = '2130E8C2FB3B79D1086186F78E59E8BC8D1A6AEDF17AB3907F4CB9AE20918C41'
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) { throw 'OpenSSL archive SHA256 mismatch' }
    if (!(Test-Path -LiteralPath (Join-Path $sourceRoot 'Configure'))) {
        & tar -xzf $archive -C $dependencyRoot
        Check-Exit 'Extract OpenSSL'
    }
    # Paths go through environment variables to preserve spaces and Cyrillic.
    $env:STUDIO_COMPILER_UNIX = Unix-Path $CompilerBin
    $env:STUDIO_OPENSSL_UNIX = Unix-Path $sourceRoot
    $env:MSYSTEM = 'MINGW32'
    & (Join-Path $MsysRoot 'usr\bin\bash.exe') -c 'export PATH="$STUDIO_COMPILER_UNIX:/usr/bin:/c/Windows/System32"
export LC_ALL=C
cd "$STUDIO_OPENSSL_UNIX" || exit
perl Configure mingw no-shared no-tests no-asm && make -j4 build_libs > build.log 2>&1'
    Check-Exit 'Build OpenSSL'
}
& cmake -S $projectRoot -B $buildRoot -G Ninja '-DCMAKE_BUILD_TYPE=Release' '-DLLAMA_STUDIO_STATIC_QT=ON' "-DCMAKE_PREFIX_PATH=$QtRoot" "-DCMAKE_CXX_COMPILER=$CompilerBin/g++.exe" "-DLLAMA_STUDIO_STATIC_OPENSSL_DIR=$sourceRoot"
Check-Exit 'Configure'
& cmake --build $buildRoot -j 4
Check-Exit 'Build'
& ctest --test-dir $buildRoot --output-on-failure
Check-Exit 'Tests'
New-Item -ItemType Directory -Force $outputPath | Out-Null
$exe = Join-Path $outputPath 'LlamaStudio.exe'
Copy-Item -LiteralPath (Join-Path $buildRoot 'LlamaStudio.exe') -Destination $exe
& (Join-Path $CompilerBin 'strip.exe') --strip-unneeded $exe
Check-Exit 'Strip'
foreach ($relative in @('resources\parameters.json', 'resources\build-commands.json', 'resources\llama.cpp-LICENSE', 'README.md', 'ROADMAP.md', 'STATIC-BUILD.md', 'THIRD-PARTY-NOTICES.md', 'CHANGELOG.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $relative) -Destination $outputPath
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'resources\licenses') -Destination $outputPath -Recurse -Force
$screenshotsPath = Join-Path $outputPath 'docs\screenshots'
New-Item -ItemType Directory -Force -Path $screenshotsPath | Out-Null
foreach ($name in @('parameters.png','build.png')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "docs\screenshots\$name") -Destination $screenshotsPath -Force
}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'LICENSE') -Destination (Join-Path $outputPath 'OpenSSL-LICENSE.txt')
$imports = & (Join-Path $CompilerBin 'objdump.exe') -p $exe | Select-String 'DLL Name:'
Check-Exit 'Read imports'
$imports | ForEach-Object { $_.Line.Trim() } | Set-Content -LiteralPath (Join-Path $outputPath 'imports.txt')
if ($imports -match '(?i)(Qt[56]|libssl|libcrypto|libgcc|libstdc|libwinpthread)') { throw 'Unexpected dynamic runtime dependency; see imports.txt' }
Write-Output "Static build: $exe"
