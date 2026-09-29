param(
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [string]$OutputDirectory,
    [switch]$BuildShipping
)

$ErrorActionPreference = 'Stop'
$pluginRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$enginePath = (Resolve-Path -LiteralPath $EngineRoot).Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $env:TEMP ('WorldMapSystemTests-' + [guid]::NewGuid().ToString('N'))
}
$validationPath = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $validationPath) {
    throw "Output directory must not already exist: $validationPath. No files were deleted."
}
$validationPlugin = Join-Path $validationPath 'Plugins\WorldMapSystem'
New-Item -ItemType Directory -Path $validationPlugin -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $pluginRoot 'Source') -Destination $validationPlugin -Recurse
Copy-Item -LiteralPath (Join-Path $pluginRoot 'WorldMapSystem.uplugin') -Destination $validationPlugin
$projectPath = Join-Path $validationPath 'WorldMapValidation.uproject'
@{ FileVersion = 3; Plugins = @(@{ Name = 'WorldMapSystem'; Enabled = $true }) } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $projectPath -Encoding utf8
$buildCommand = Join-Path $enginePath 'Engine\Build\BatchFiles\Build.bat'
$editorCommand = Join-Path $enginePath 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$pluginFile = Join-Path $validationPlugin 'WorldMapSystem.uplugin'

Write-Output "Validation project: $validationPath"
& $buildCommand UnrealEditor Win64 Development "-Project=$projectPath" "-Plugin=$pluginFile" -NoHotReload -NoUBTMakefiles -DisableLiveCoding *> (Join-Path $validationPath 'Build-Editor.log')
if ($LASTEXITCODE -ne 0) { throw "Editor build failed. See $validationPath\Build-Editor.log" }

if ($BuildShipping) {
    & $buildCommand UnrealGame Win64 Shipping "-Project=$projectPath" "-Plugin=$pluginFile" -NoHotReload -NoUBTMakefiles *> (Join-Path $validationPath 'Build-Shipping.log')
    if ($LASTEXITCODE -ne 0) { throw "Shipping build failed. See $validationPath\Build-Shipping.log" }
}

$reportPath = Join-Path $validationPath 'TestReport'
& $editorCommand $projectPath -unattended -nop4 -nosplash -nullrhi -nosound '-ExecCmds=Automation RunTests WorldMap;Quit' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$reportPath" "-abslog=$validationPath\Automation.log" *> (Join-Path $validationPath 'AutomationConsole.log')
if ($LASTEXITCODE -ne 0) { throw "Automation failed. See $validationPath\Automation.log" }
$report = Get-Content -Raw -LiteralPath (Join-Path $reportPath 'index.json') | ConvertFrom-Json
if ($report.failed -ne 0 -or $report.succeeded -lt 1 -or $report.notRun -gt 0) {
    throw "Invalid/incomplete test result. Inspect $reportPath\index.json"
}
Write-Output "Passed: $($report.succeeded); failed: $($report.failed). Report: $reportPath"
