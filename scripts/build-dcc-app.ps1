#!/usr/bin/env pwsh

param(
	[Parameter(Mandatory)]
	[string] $AppDirectory,

	[string[]] $DccArguments = @(),

	[string] $Dccmake = 'dccmake',

	[string] $DiskImage,

	[switch] $BuildOnly,

	[switch] $CollectOnly,

	[switch] $ValidateOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-DccSetting {
	param(
		[Parameter(Mandatory)]
		[string] $Path,

		[Parameter(Mandatory)]
		[string] $Name,

		[string[]] $Arguments = @(),

		[string] $DefaultValue
	)

	$pattern = '^\s*' + [regex]::Escape($Name) + '\s*=\s*([^#\s]+)'
	$values = @(foreach ($argument in $Arguments) {
		if ($argument -match $pattern) {
			$Matches[1]
		}
	})
	if ($values) {
		return $values[-1]
	}

	if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
		if ($PSBoundParameters.ContainsKey('DefaultValue')) { return $DefaultValue }
		throw "$Name must be supplied in DccArguments when $Path does not exist"
	}
	$values = @(foreach ($line in Get-Content -LiteralPath $Path) {
		if ($line -match $pattern) {
			$Matches[1]
		}
	})
	if (-not $values) {
		if ($PSBoundParameters.ContainsKey('DefaultValue')) { return $DefaultValue }
		throw "$Name is not set in $Path"
	}
	$values[-1]
}

function Invoke-NativeCommand {
	param(
		[Parameter(Mandatory)]
		[string] $Command,

		[string[]] $Arguments = @()
	)

	& $Command @Arguments
	if ($LASTEXITCODE -ne 0) {
		throw "$Command failed with exit code $LASTEXITCODE"
	}
}

$appPath = (Resolve-Path -LiteralPath $AppDirectory).Path
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$configPath = Join-Path $appPath 'dccmake.txt'

$outputBase = Get-DccSetting -Path $configPath -Name 'dcc-output' -Arguments $DccArguments
$format = Get-DccSetting -Path $configPath -Name 'dcc-output-format' -Arguments $DccArguments -DefaultValue 'com'
if ($format -notin @('com', 'lib')) {
	throw "Invalid dcc-output-format: $format"
}
$outputBase = ($outputBase -replace '(?i)\.(COM|LIB)$', '').ToUpperInvariant()
if ($outputBase -notmatch '^[A-Z0-9_]{1,8}$') {
	throw "Expected an 8-character CP/M output base name: $outputBase"
}
$outputFile = "$outputBase.$($format.ToUpperInvariant())"
$buildSetting = Get-DccSetting -Path $configPath -Name 'dcc-build-dir' -Arguments $DccArguments -DefaultValue 'build'
$buildDirectory = if ([IO.Path]::IsPathRooted($buildSetting)) {
	$buildSetting
}
else {
	Join-Path $appPath $buildSetting
}
$buildDirectory = [IO.Path]::GetFullPath($buildDirectory)
$buildPrefix = $buildDirectory.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if ($appPath -eq $buildDirectory -or
	$appPath.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase) -or
	$repoRoot -eq $buildDirectory -or
	$repoRoot.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
	throw "Build directory must not be the application, repository, or an ancestor: $buildDirectory"
}
if ([string]::IsNullOrWhiteSpace($DiskImage)) {
	$DiskImage = Join-Path $repoRoot 'disks' 'escape-posix.dsk'
}
if ($ValidateOnly) { return }

Push-Location $appPath
try {
	if (-not $CollectOnly) {
		if (Test-Path -LiteralPath $buildDirectory) {
			Remove-Item -LiteralPath $buildDirectory -Recurse -Force
		}
		$hostFile = Join-Path $appPath $outputFile
		if (Test-Path -LiteralPath $hostFile) {
			Remove-Item -LiteralPath $hostFile -Force
		}
		Invoke-NativeCommand -Command $Dccmake -Arguments $DccArguments
	}
	Move-Item -LiteralPath (Join-Path $buildDirectory $outputFile) -Destination $appPath -Force
	# Library intermediates can include generated sources consumed by host builds.
	if ($format -eq 'com') {
		Remove-Item -LiteralPath $buildDirectory -Recurse -Force
	}

	if (-not $BuildOnly) {
		& (Join-Path $PSScriptRoot 'cpm-install.ps1') `
			-Image $DiskImage `
			-HostFile (Join-Path $appPath $outputFile) `
			-CpmFile "0:$outputFile"
	}
}
finally {
	Pop-Location
}
