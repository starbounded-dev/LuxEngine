[CmdletBinding()]
param(
	[string] $BankDirectory,
	[ValidateSet('Debug', 'Release', 'Dist')]
	[string] $Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not $BankDirectory) {
	$BankDirectory = Join-Path $repositoryRoot 'Editor\LuxSampleProject\Assets\Audio\SampleProject\Build\Desktop'
}
$bankPath = (Get-Item -LiteralPath $BankDirectory -ErrorAction Stop).FullName
$configurationRoot = Join-Path $repositoryRoot "bin\$Configuration-windows-x86_64"
$validatorPath = Join-Path $configurationRoot 'Lux-AudioBankValidation\Lux-AudioBankValidation.exe'
if (-not (Test-Path -LiteralPath $validatorPath)) {
	throw 'Build Lux-AudioBankValidation first (scripts\Lux-BuildTest.ps1 builds it with the solution).'
}
$reportDirectory = Join-Path $repositoryRoot ('bin\audio-validation\' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
# Runtime is built in every configuration, including Dist (where Editor is excluded).
$previousPath = $env:PATH
try {
	$env:PATH = (Join-Path $configurationRoot 'Lux-Runtime') + [System.IO.Path]::PathSeparator + $previousPath
	& $validatorPath $bankPath $reportDirectory
	if ($LASTEXITCODE -ne 0) { throw "Built-bank catalog validation failed; see $reportDirectory" }
}
finally { $env:PATH = $previousPath }
Write-Output "PASS: built-bank catalog validation. Reports: $reportDirectory"
