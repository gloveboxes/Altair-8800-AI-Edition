#!/usr/bin/env pwsh

param(
	[Parameter(Mandatory)]
	[string] $Image,

	[Parameter(Mandatory)]
	[string] $HostFile,

	[string] $CpmFile
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$sectorBytes = 137
$sectorsPerTrack = 32
$tracks = 77
$payloadBytes = 128
$minimumImageBytes = $tracks * $sectorsPerTrack * $sectorBytes

function Invoke-CpmTool {
	param(
		[Parameter(Mandatory)]
		[string] $Command,

		[Parameter(Mandatory)]
		[string[]] $Arguments
	)

	& $Command @Arguments
	if ($LASTEXITCODE -ne 0) {
		throw "$Command failed with exit code $LASTEXITCODE"
	}
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$imagePath = (Resolve-Path -LiteralPath $Image).Path
$hostFilePath = (Resolve-Path -LiteralPath $HostFile).Path
if ([string]::IsNullOrWhiteSpace($CpmFile)) {
	$CpmFile = "0:$([IO.Path]::GetFileName($hostFilePath))"
}

$null = Get-Command cpmrm -ErrorAction Stop
$null = Get-Command cpmcp -ErrorAction Stop

$temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) "altair-cpm-$([guid]::NewGuid())"
$null = New-Item -ItemType Directory -Path $temporaryDirectory
$packedImage = Join-Path $temporaryDirectory 'packed.dsk'
$updatedImage = Join-Path $temporaryDirectory 'updated.dsk'

try {
	[byte[]] $imageBytes = [IO.File]::ReadAllBytes($imagePath)
	if ($imageBytes.Length -lt $minimumImageBytes) {
		throw "$imagePath is shorter than a 77-track Altair image"
	}

	[byte[]] $packedBytes = [byte[]]::new($tracks * $sectorsPerTrack * $payloadBytes)
	for ($track = 0; $track -lt $tracks; $track++) {
		for ($packedSector = 0; $packedSector -lt $sectorsPerTrack; $packedSector++) {
			$physicalSector = if ($track -lt 6) {
				$packedSector
			}
			else {
				($packedSector * 17) % $sectorsPerTrack
			}
			$payloadOffset = if ($track -lt 6) { 3 } else { 7 }
			$sourceOffset = ($track * $sectorsPerTrack + $physicalSector) * $sectorBytes + $payloadOffset
			$destinationOffset = ($track * $sectorsPerTrack + $packedSector) * $payloadBytes
			[Array]::Copy($imageBytes, $sourceOffset, $packedBytes, $destinationOffset, $payloadBytes)
		}
	}
	[IO.File]::WriteAllBytes($packedImage, $packedBytes)

	Push-Location $repoRoot
	try {
		Invoke-CpmTool -Command 'cpmrm' -Arguments @('-f', 'altair88', '-T', 'raw', $packedImage, $CpmFile)
		Invoke-CpmTool -Command 'cpmcp' -Arguments @('-f', 'altair88', '-T', 'raw', $packedImage, $hostFilePath, $CpmFile)
	}
	finally {
		Pop-Location
	}

	$packedBytes = [IO.File]::ReadAllBytes($packedImage)
	[byte[]] $updatedBytes = $imageBytes.Clone()
	for ($track = 0; $track -lt $tracks; $track++) {
		for ($packedSector = 0; $packedSector -lt $sectorsPerTrack; $packedSector++) {
			$physicalSector = if ($track -lt 6) {
				$packedSector
			}
			else {
				($packedSector * 17) % $sectorsPerTrack
			}
			$frameOffset = ($track * $sectorsPerTrack + $physicalSector) * $sectorBytes
			$sourceOffset = ($track * $sectorsPerTrack + $packedSector) * $payloadBytes
			$payloadOffset = if ($track -lt 6) { 3 } else { 7 }
			[Array]::Copy($packedBytes, $sourceOffset, $updatedBytes, $frameOffset + $payloadOffset, $payloadBytes)

			$checksum = 0
			for ($index = 0; $index -lt $payloadBytes; $index++) {
				$checksum += $packedBytes[$sourceOffset + $index]
			}
			if ($track -lt 6) {
				$updatedBytes[$frameOffset + 131] = 0xff
				$updatedBytes[$frameOffset + 132] = $checksum -band 0xff
			}
			else {
				$checksum += $updatedBytes[$frameOffset + 2]
				$checksum += $updatedBytes[$frameOffset + 3]
				$checksum += $updatedBytes[$frameOffset + 5]
				$checksum += $updatedBytes[$frameOffset + 6]
				$updatedBytes[$frameOffset + 4] = $checksum -band 0xff
				$updatedBytes[$frameOffset + 135] = 0xff
				$updatedBytes[$frameOffset + 136] = 0
			}
		}
	}

	[IO.File]::WriteAllBytes($updatedImage, $updatedBytes)
	if (-not $IsWindows) {
		$mode = [IO.File]::GetUnixFileMode($imagePath)
		[IO.File]::SetUnixFileMode($updatedImage, $mode)
	}
	[IO.File]::Move($updatedImage, $imagePath, $true)
}
finally {
	Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force -ErrorAction SilentlyContinue
}