#!/usr/bin/env pwsh

param(
	[Parameter(Mandatory)]
	[string] $AppDirectory,

	[string[]] $DccArguments = @(),

	[string] $Dccmake = 'dccmake',

	[string] $DiskImage
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-DccSetting {
	param(
		[Parameter(Mandatory)]
		[string] $Path,

		[Parameter(Mandatory)]
		[string] $Name,

		[string[]] $Arguments = @()
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

	$values = @(foreach ($line in Get-Content -LiteralPath $Path) {
		if ($line -match $pattern) {
			$Matches[1]
		}
	})
	if (-not $values) {
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
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
	throw "dccmake configuration not found: $configPath"
}

$outputBase = Get-DccSetting -Path $configPath -Name 'dcc-output' -Arguments $DccArguments
$outputFile = if ($outputBase.EndsWith('.COM', [StringComparison]::OrdinalIgnoreCase)) {
	$outputBase
}
else {
	"$outputBase.COM"
}
$buildSetting = Get-DccSetting -Path $configPath -Name 'dcc-build-dir' -Arguments $DccArguments
$buildDirectory = if ([IO.Path]::IsPathRooted($buildSetting)) {
	$buildSetting
}
else {
	Join-Path $appPath $buildSetting
}
if ([string]::IsNullOrWhiteSpace($DiskImage)) {
	$DiskImage = Join-Path $repoRoot 'disks' 'escape-posix.dsk'
}

Push-Location $appPath
try {
	Remove-Item -LiteralPath $buildDirectory -Recurse -Force -ErrorAction SilentlyContinue
	Remove-Item -LiteralPath (Join-Path $appPath $outputFile) -Force -ErrorAction SilentlyContinue
	Invoke-NativeCommand -Command $Dccmake -Arguments $DccArguments
	Move-Item -LiteralPath (Join-Path $buildDirectory $outputFile) -Destination $appPath -Force
	Remove-Item -LiteralPath $buildDirectory -Recurse -Force

	& (Join-Path $PSScriptRoot 'cpm-install.ps1') `
		-Image $DiskImage `
		-HostFile (Join-Path $appPath $outputFile) `
		-CpmFile "0:$outputFile"
}
finally {
	Pop-Location
}