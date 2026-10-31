[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
	[string] $Label,
	[ValidateSet('Debug', 'Release')]
	[string] $Configuration = 'Debug',
	[ValidateNotNullOrEmpty()]
	[string] $Scenes = 'FMODDemo',
	[ValidateSet('Single', 'Multi')]
	[string] $Threading = 'Multi',
	[ValidateRange(1, 3600)]
	[int] $TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$runnerPath = Join-Path $repositoryRoot 'tests\rendering\golden_run.py'
$started = [DateTime]::UtcNow
& python $runnerPath --label $Label --config $Configuration.ToLowerInvariant() --scenes $Scenes --threading $Threading.ToLowerInvariant() --timeout $TimeoutSeconds
if ($LASTEXITCODE -ne 0) { throw "Golden capture failed: $Label" }
$outputRoot = Join-Path $repositoryRoot "bin\golden\$Label"
$summaryPath = Join-Path $outputRoot 'summary.json'
$summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
if (-not $summary.runs -or $summary.runs.Count -eq 0) { throw 'Golden runner produced no capture results.' }
foreach ($run in $summary.runs) {
	if ($run.status -ne 'OK' -or $run.validationMessages -ne 0) {
		throw "Golden capture $($run.name) failed or reported Vulkan validation messages; see $outputRoot"
	}
	# Status alone is not proof: the image and timing evidence must also belong to this run.
	$artifacts = @(Get-Item -LiteralPath (Join-Path $outputRoot ($run.name + '.status')), (Join-Path $outputRoot ($run.name + '.lximg')), (Join-Path $outputRoot ($run.name + '.perf.json')))
	foreach ($artifact in $artifacts) {
		if ($artifact.Length -eq 0 -or $artifact.LastWriteTimeUtc -lt $started.AddSeconds(-2)) {
			throw "Missing, empty or stale golden artifact: $($artifact.FullName)"
		}
	}
}
Write-Output "PASS: fresh golden captures, zero Vulkan validation messages. Evidence: $summaryPath"
