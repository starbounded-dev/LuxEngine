[CmdletBinding()]
param(
	[ValidateSet('Debug', 'Release', 'Dist')]
	[string[]] $Configurations = @('Debug', 'Release', 'Dist')
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$pythonPath = (Get-Command python -ErrorAction Stop).Source
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) {
	throw 'Visual Studio Installer/vswhere is missing; install the C++ build tools.'
}
$installationPath = (& $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if ($LASTEXITCODE -ne 0 -or -not $installationPath) {
	throw 'No Visual Studio installation with the C++ build tools was found.'
}
$msbuildPath = Join-Path $installationPath.Trim() 'MSBuild\Current\Bin\MSBuild.exe'
$developerCommand = Join-Path $installationPath.Trim() 'Common7\Tools\VsDevCmd.bat'
$runDirectory = Join-Path $repositoryRoot ('bin\soundwave-tests\build-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null

function Get-NewestInputTime([string[]] $Roots, [string[]] $Files) {
	$inputs = @($Files | ForEach-Object { Get-Item -LiteralPath (Join-Path $repositoryRoot $_) })
	foreach ($inputRoot in $Roots) {
		$inputs += Get-ChildItem -LiteralPath (Join-Path $repositoryRoot $inputRoot) -Recurse -File |
			Where-Object { $_.Extension -in @('.h', '.hpp', '.c', '.cpp') }
	}
	return ($inputs | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc
}

$coreInputTime = Get-NewestInputTime @('Core\Source', 'Core\Platform\Windows', 'Core\vendor\soundwave') @()
$editorInputTime = Get-NewestInputTime @('Editor\Source') @('Editor\premake5.lua')
$runtimeInputTime = Get-NewestInputTime @('Lux-Runtime') @('Lux-Runtime\premake5.lua')

Push-Location (Join-Path $repositoryRoot 'scripts')
try {
	& $pythonPath 'Win-GenProjects.py' --last
	if ($LASTEXITCODE -ne 0) { throw 'Visual Studio project generation failed.' }
}
finally { Pop-Location }

# Match the regression executable's compiler to Core's selected platform toolset.
$toolsetVersion = (& $msbuildPath (Join-Path $repositoryRoot 'Core\Core.vcxproj') /nologo /p:Configuration=Debug /p:Platform=x64 -getProperty:VCToolsVersion)
if ($LASTEXITCODE -ne 0 -or -not $toolsetVersion) { throw 'Could not resolve the Core C++ toolset version.' }
$toolsetVersion = $toolsetVersion.Trim()
$validatorInputTime = Get-NewestInputTime @('tools\AudioBankValidation') @('tools\AudioBankValidation\premake5.lua')
$results = @()
foreach ($configuration in $Configurations) {
	$buildLog = Join-Path $runDirectory ($configuration.ToLowerInvariant() + '.log')
	& $msbuildPath (Join-Path $repositoryRoot 'Lux.sln') /t:Build /m:4 /nologo /v:minimal "/p:Configuration=$configuration" '/p:Platform=Mixed Platforms' /p:CL_MPCount=4 "/flp:logfile=$buildLog;verbosity=normal"
	if ($LASTEXITCODE -ne 0) { throw "$configuration build failed; see $buildLog" }
	$outputRoot = Join-Path $repositoryRoot "bin\$configuration-windows-x86_64"
	# One artifact check per build. An up-to-date output is valid only when it postdates its inputs.
	$artifactPaths = @((Join-Path $outputRoot 'Core\Core.lib'), (Join-Path $outputRoot 'Lux-Runtime\Lux-Runtime.exe'))
	# The solution intentionally excludes Editor in Dist; an old Editor.exe is not Dist evidence.
	if ($configuration -ne 'Dist') { $artifactPaths += Join-Path $outputRoot 'Editor\Editor.exe' }
	$artifactPaths += Join-Path $outputRoot 'Lux-AudioBankValidation\Lux-AudioBankValidation.exe'
	$artifacts = @(Get-Item -LiteralPath $artifactPaths)
	$minimumTimes = @($coreInputTime, [DateTime]::MaxValue)
	$minimumTimes[1] = @($runtimeInputTime, $artifacts[0].LastWriteTimeUtc) | Sort-Object -Descending | Select-Object -First 1
	if ($configuration -ne 'Dist') {
		$minimumTimes += @($editorInputTime, $artifacts[0].LastWriteTimeUtc) | Sort-Object -Descending | Select-Object -First 1
	}
	$minimumTimes += @($validatorInputTime, $artifacts[0].LastWriteTimeUtc) | Sort-Object -Descending | Select-Object -First 1
	for ($index = 0; $index -lt $artifacts.Count; ++$index) {
		$artifact = $artifacts[$index]
		if ($artifact.Length -eq 0 -or $artifact.LastWriteTimeUtc -lt $minimumTimes[$index]) {
			throw "Missing, empty or stale build artifact: $($artifact.FullName)"
		}
		$results += [PSCustomObject]@{ Configuration = $configuration; Path = $artifact.FullName; Bytes = $artifact.Length; LastWriteTimeUtc = $artifact.LastWriteTimeUtc }
	}
}

foreach ($testName in @('run_soundwave.py', 'run_occlusion_windows.py')) {
	$testPath = Join-Path $repositoryRoot "tests\audio\$testName"
	$testOutput = Join-Path $runDirectory ([System.IO.Path]::GetFileNameWithoutExtension($testName))
	$testCommand = 'call "{0}" -no_logo -arch=x64 -host_arch=x64 -vcvars_ver={4} >nul && "{1}" "{2}" --output "{3}"' -f $developerCommand, $pythonPath, $testPath, $testOutput, $toolsetVersion
	& $env:ComSpec /d /c $testCommand
	if ($LASTEXITCODE -ne 0) { throw "Audio regression check failed: $testName; see $testOutput" }
}
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runDirectory 'artifacts.json') -Encoding utf8
Write-Output "PASS: Windows builds and Soundwave/occlusion checks. Evidence: $runDirectory"
