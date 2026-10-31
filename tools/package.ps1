param(
    [string]$QtRoot = 'E:\Qt\Qt5.14.2\5.14.2\mingw73_32',
    [string]$Executable = 'build\LlamaStudio.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$outputPath = Join-Path $projectRoot 'dist\LlamaStudio'
New-Item -ItemType Directory -Force (Join-Path $outputPath 'plugins\platforms') | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot $Executable) -Destination $outputPath
foreach ($name in @('Qt5Core.dll', 'Qt5Gui.dll', 'Qt5Network.dll', 'Qt5Widgets.dll', 'libgcc_s_dw2-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')) {
    Copy-Item -LiteralPath (Join-Path $QtRoot "bin\$name") -Destination $outputPath
}
foreach ($name in @('qwindows.dll', 'qoffscreen.dll')) {
    Copy-Item -LiteralPath (Join-Path $QtRoot "plugins\platforms\$name") -Destination (Join-Path $outputPath 'plugins\platforms')
}
foreach ($relative in @('resources\parameters.json', 'resources\build-commands.json', 'resources\llama.cpp-LICENSE', 'README.md', 'ROADMAP.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $relative) -Destination $outputPath
}
Set-Content -LiteralPath (Join-Path $outputPath 'qt.conf') -Value "[Paths]`nPlugins=plugins" -Encoding ascii
Write-Output $outputPath
