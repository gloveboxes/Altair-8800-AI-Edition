#!/usr/bin/env pwsh

param(
	[Parameter(Mandatory)]
	[string] $Source,

	[string] $DiskImage,

	[string] $ToolsDiskImage,

	[string] $DccDirectory,

	[string] $Ntvcm = 'ntvcm',

	[switch] $BuildOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-CpmTool {
	param(
		[Parameter(Mandatory)]
		[string[]] $ToolArguments
	)

	$output = & $ntvcmPath @ToolArguments 2>&1
	$exitCode = $LASTEXITCODE
	$text = $output -join [Environment]::NewLine
	Write-Host $text
	if ($exitCode -ne 0) {
		throw "$($ToolArguments[0]) failed with exit code $exitCode"
	}
	return $text
}

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$sourceFile = Get-Item -LiteralPath $sourcePath
$outputBase = $sourceFile.BaseName.ToUpperInvariant()
if ($sourceFile.Extension -ine '.ASM' -or $outputBase -notmatch '^[A-Z0-9_]{1,8}$') {
	throw "Expected an 8.3-named .ASM source: $sourcePath"
}
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($DiskImage)) {
	$DiskImage = Join-Path $repoRoot 'disks' 'escape-posix.dsk'
}
$targetImages = @()
if (-not $BuildOnly) {
	$targetImages = @((Resolve-Path -LiteralPath $DiskImage).Path)
}
if (-not $BuildOnly -and $outputBase -eq 'FT') {
	if ([string]::IsNullOrWhiteSpace($ToolsDiskImage)) {
		$ToolsDiskImage = Join-Path $repoRoot 'disks' 'bdsc-v1.60.dsk'
	}
	$targetImages += (Resolve-Path -LiteralPath $ToolsDiskImage).Path
}

if ([string]::IsNullOrWhiteSpace($DccDirectory)) {
	$DccDirectory = Split-Path (Get-Command dccmake -CommandType Application -ErrorAction Stop).Source -Parent
}

# Native Z80 modules use m80c/l80c. Only DCC-generated modules require DCCRTL;
# self-contained modules marked "altair-z80 standalone" provide their startup.
$firstLine = Get-Content -LiteralPath $sourcePath -TotalCount 1
$isDccModule = $firstLine -match '^\s*;\s*dcc stage-\d+[a-z]? output\s*$'
$isNativeStandalone = $firstLine -match '^\s*;\s*altair-z80 standalone\s*$'
if ($isDccModule -or $isNativeStandalone) {
	$nativeTools = @{}
	$toolNames = @('m80c', 'l80c')
	if ($isDccModule) { $toolNames += 'dccrtlstrip' }
	foreach ($name in $toolNames) {
		$tool = Join-Path $DccDirectory $name
		if ($IsWindows) { $tool += '.exe' }
		if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
			throw "Required DCC tool not found: $tool"
		}
		$nativeTools[$name] = $tool
	}
	$runtime = Join-Path $DccDirectory 'DCCRTL.MAC'
	if ($isDccModule -and -not (Test-Path -LiteralPath $runtime -PathType Leaf)) {
		throw "DCC runtime not found: $runtime"
	}

	$temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) "altair-dcc-asm-$([guid]::NewGuid())"
	$null = New-Item -ItemType Directory -Path $temporaryDirectory
	try {
		Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $temporaryDirectory "$outputBase.MAC")
		if ($isDccModule) {
			Copy-Item -LiteralPath $runtime -Destination (Join-Path $temporaryDirectory 'DCCRTL.MAC')
		}
		Push-Location $temporaryDirectory
		try {
			$modules = @($outputBase)
			if ($isDccModule) {
				& $nativeTools['dccrtlstrip'] -r DCCRTL.MAC -o RTLMIN.MAC "$outputBase.MAC"
				if ($LASTEXITCODE -ne 0) { throw "dccrtlstrip failed with exit code $LASTEXITCODE" }
				$modules = @('RTLMIN', $outputBase)
			}
			foreach ($module in $modules) {
				& $nativeTools['m80c'] "=$module.MAC" /X /O /Z /L /C
				if ($LASTEXITCODE -ne 0) { throw "m80c failed for $module with exit code $LASTEXITCODE" }
			}
			$linkModules = $modules -join ','
			& $nativeTools['l80c'] "/P:100,$linkModules,$outputBase/N/E/Y"
			if ($LASTEXITCODE -ne 0) { throw "l80c failed with exit code $LASTEXITCODE" }
		}
		finally {
			Pop-Location
		}

		$builtFile = Join-Path $temporaryDirectory "$outputBase.COM"
		if (-not (Test-Path -LiteralPath $builtFile -PathType Leaf) -or (Get-Item -LiteralPath $builtFile).Length -eq 0) {
			throw "DCC M80/L80 did not produce a nonempty $outputBase.COM"
		}
		$outputFile = Join-Path $sourceFile.DirectoryName "$outputBase.COM"
		Copy-Item -LiteralPath $builtFile -Destination $outputFile -Force
		foreach ($image in ($targetImages | Select-Object -Unique)) {
			& (Join-Path $PSScriptRoot 'cpm-install.ps1') `
				-Image $image -HostFile $outputFile -CpmFile "0:$outputBase.COM"
			Write-Host "Installed $outputBase.COM into $image"
		}
	}
	finally {
		Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
	}
	return
}

$ntvcmPath = (Get-Command $Ntvcm -CommandType Application -ErrorAction Stop).Source
$toolPaths = @(foreach ($name in @('M80.COM', 'L80.COM')) {
	$tool = @(Get-ChildItem -LiteralPath $DccDirectory -File |
		Where-Object { $_.Name -ieq $name })
	if ($tool.Count -ne 1) {
		throw "$name not found in $DccDirectory; specify -DccDirectory."
	}
	$tool[0].FullName
})

$temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) "altair-asm-$([guid]::NewGuid())"
$null = New-Item -ItemType Directory -Path $temporaryDirectory
try {
	for ($index = 0; $index -lt $toolPaths.Count; $index++) {
		$name = @('M80.COM', 'L80.COM')[$index]
		Copy-Item -LiteralPath $toolPaths[$index] -Destination (Join-Path $temporaryDirectory $name)
	}

	# M80 defaults to relocatable code; these MAC-style apps use absolute ORG 100H.
	$text = "ASEG`r`n" + ([IO.File]::ReadAllText($sourcePath).TrimEnd([char]0x1a) -replace '\r?\n', "`r`n") + [char]0x1a
	[IO.File]::WriteAllText((Join-Path $temporaryDirectory "$outputBase.ASM"),
		$text, [Text.UTF8Encoding]::new($false))

	Push-Location $temporaryDirectory
	try {
		$assemblyOutput = Invoke-CpmTool -ToolArguments @('M80', "$outputBase,$outputBase=$outputBase.ASM")
		if ($assemblyOutput -notmatch 'No Fatal error\(s\)') {
			throw "M80 reported assembly errors for $sourcePath"
		}
		$linkOutput = Invoke-CpmTool -ToolArguments @('L80', "$outputBase,$outputBase/N/E")
		if ($linkOutput -match '(?im)^\s*\?|undefined|fatal error|origin below|overlap|multiply defined|disk full|insufficient') {
			throw "L80 reported link errors for $sourcePath"
		}
	}
	finally {
		Pop-Location
	}

	$builtFile = Join-Path $temporaryDirectory "$outputBase.COM"
	if (-not (Test-Path -LiteralPath $builtFile -PathType Leaf) -or (Get-Item -LiteralPath $builtFile).Length -eq 0) {
		throw "M80/L80 did not produce a nonempty $outputBase.COM"
	}
	$outputFile = Join-Path $sourceFile.DirectoryName "$outputBase.COM"
	Copy-Item -LiteralPath $builtFile -Destination $outputFile -Force
	foreach ($image in ($targetImages | Select-Object -Unique)) {
		& (Join-Path $PSScriptRoot 'cpm-install.ps1') `
			-Image $image -HostFile $outputFile -CpmFile "0:$outputBase.COM"
		Write-Host "Installed $outputBase.COM into $image"
	}
}
finally {
	Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
}
