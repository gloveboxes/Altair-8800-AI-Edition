#!/usr/bin/env pwsh

[CmdletBinding()]
param(
    [switch] $AssemblyOnly,
    [string] $DccDirectory,
    [string] $Dccmake = 'dccmake',
    [string] $LibraryLinker = $env:DCC_L80CC,
    [string] $Ntvcm = 'ntvcm',
    [string] $DiskA,
    [string] $DiskB,
    [string] $DiskC,
    [string] $DiskD,
    [Alias('c')]
    [string] $Clone,
    [switch] $CloneOnly,
    [switch] $ListOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$appsRoot = $PSScriptRoot
$repoRoot = Split-Path $appsRoot -Parent
$scriptsRoot = Join-Path $repoRoot 'scripts'
$images = [ordered]@{
    A = $(if ($DiskA) { $DiskA } else { Join-Path $repoRoot 'disks/cpm63k.dsk' })
    B = $(if ($DiskB) { $DiskB } else { Join-Path $repoRoot 'disks/bdsc-v1.60.dsk' })
    C = $(if ($DiskC) { $DiskC } else { Join-Path $repoRoot 'disks/escape-posix.dsk' })
    D = $(if ($DiskD) { $DiskD } else { Join-Path $repoRoot 'disks/blank.dsk' })
}

function New-BuildJob {
    param($Kind, $Directory, $Source, $Output, $Targets, $Arguments = @(), $Members = @())
    [pscustomobject]@{
        Kind = $Kind; Directory = $Directory; Source = $Source
        Output = $Output; Targets = @($Targets); Arguments = @($Arguments)
        Members = @($Members)
    }
}

function Read-DccConfig {
    param([string] $Path)
    $settings = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        $active = ($line -split '#', 2)[0].Trim()
        if (-not $active) { continue }
        if ($active -notmatch '^([^=]+)=(.*)$') { throw "Expected key=value in $Path`: $line" }
        $settings[$Matches[1].Trim().ToLowerInvariant()] = $Matches[2].Trim()
    }
    return $settings
}

function Get-DccMembers {
    param([string] $Directory, $Targets, $Ancestors = @())
    $directoryPath = (Resolve-Path -LiteralPath $Directory).Path
    if ($directoryPath -in $Ancestors) { throw "Composite project cycle at $directoryPath" }
    $config = Join-Path $directoryPath 'dccmake.txt'
    if (-not (Test-Path -LiteralPath $config -PathType Leaf)) {
        throw "Child project has no dccmake.txt: $directoryPath"
    }
    $settings = Read-DccConfig $config
    if ($settings.ContainsKey('dcc-projects')) {
        if ($Ancestors.Count -ge 16) { throw 'Composite project nesting exceeds 16 levels' }
        $projects = @($settings['dcc-projects'].Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        if (-not $projects.Count) { throw "dcc-projects is empty in $config" }
        foreach ($project in $projects) {
            $expanded = [regex]::Replace($project, '\$\{([A-Za-z_][A-Za-z0-9_]*)\}', {
                param($match)
                $value = [Environment]::GetEnvironmentVariable($match.Groups[1].Value)
                if (-not $value) { throw "Project environment variable is not set: $($match.Groups[1].Value)" }
                $value
            })
            $childPath = if ([IO.Path]::IsPathRooted($expanded)) { $expanded } else { Join-Path $directoryPath $expanded }
            Get-DccMembers $childPath $Targets (@($Ancestors) + $directoryPath)
        }
    }
    else {
        if (-not $settings['dcc-output']) { throw "dcc-output is missing in $config" }
        $format = if ($settings['dcc-output-format']) { $settings['dcc-output-format'].ToLowerInvariant() } else { 'com' }
        if ($format -notin @('com', 'lib')) { throw "Invalid dcc-output-format in $config`: $format" }
        $output = ($settings['dcc-output'] -replace '(?i)\.(COM|LIB)$', '').ToUpperInvariant()
        $kind = if ($format -eq 'lib') { 'library' } else { 'dcc' }
        New-BuildJob $kind $directoryPath $config "$output.$($format.ToUpperInvariant())" $Targets
    }
}

# Inspect bucket roots and source directories at any depth, excluding nested
# tests, fixtures, generated directories and shared SDK sources.
$jobs = @(foreach ($bucket in @('all', 'a', 'b', 'c', 'd')) {
    $bucketPath = Join-Path $appsRoot "disk-$bucket-apps"
    if (-not (Test-Path -LiteralPath $bucketPath -PathType Container)) {
        throw "Application folder not found: $bucketPath"
    }
    $targets = if ($bucket -eq 'all') { @('A', 'B', 'C', 'D') } else { @($bucket.ToUpperInvariant()) }
    $directories = @((Get-Item -LiteralPath $bucketPath)) + @(
        Get-ChildItem -LiteralPath $bucketPath -Directory -Recurse |
            Where-Object {
                $parts = $_.FullName.Substring($bucketPath.Length).Split([IO.Path]::DirectorySeparatorChar)
                $excluded = @($parts | Where-Object { $_ -match '^(build(?:-[^/\\]+)?|generated|tests?|fixtures?|banksdk)$' }).Count -gt 0
                $ancestor = $_.Parent
                while (-not $excluded -and $ancestor -and $ancestor.FullName.StartsWith($bucketPath, [StringComparison]::OrdinalIgnoreCase)) {
                    $ancestorConfig = Join-Path $ancestor.FullName 'dccmake.txt'
                    if ((Test-Path -LiteralPath $ancestorConfig -PathType Leaf) -and
                        (Read-DccConfig $ancestorConfig).ContainsKey('dcc-projects')) {
                        $excluded = $true
                    }
                    $ancestor = $ancestor.Parent
                }
                -not $excluded
            } |
            Sort-Object FullName
    )
    foreach ($directory in $directories) {
        $files = @(Get-ChildItem -LiteralPath $directory.FullName -File | Sort-Object Name)
        $config = Join-Path $directory.FullName 'dccmake.txt'
        $cOutputs = @()
        if (Test-Path -LiteralPath $config -PathType Leaf) {
            $settings = Read-DccConfig $config
            if ($settings.ContainsKey('dcc-projects')) {
                if (-not $AssemblyOnly) {
                    $members = @(Get-DccMembers $directory.FullName $targets)
                    New-BuildJob 'composite' $directory.FullName $config ($members.Output -join ',') $targets @() $members
                }
                continue
            }
            $outputs = @(Get-Content -LiteralPath $config | ForEach-Object {
                if ($_ -match '^\s*dcc-output\s*=\s*([^#\s]+)') { $Matches[1] }
            })
            if (-not $outputs) { throw "dcc-output is missing in $config" }
            $formats = @(Get-Content -LiteralPath $config | ForEach-Object {
                if ($_ -match '^\s*dcc-output-format\s*=\s*([^#\s]+)') { $Matches[1] }
            })
            $format = if ($formats) { $formats[-1].ToLowerInvariant() } else { 'com' }
            if ($format -notin @('com', 'lib')) { throw "Invalid dcc-output-format in $config`: $format" }
            $output = ($outputs[-1] -replace '(?i)\.(COM|LIB)$', '').ToUpperInvariant()
            if ($format -eq 'com') { $cOutputs += $output }
            if (-not $AssemblyOnly) {
                $kind = if ($format -eq 'lib') { 'library' } else { 'dcc' }
                New-BuildJob $kind $directory.FullName $config "$output.$($format.ToUpperInvariant())" $targets
            }
        }
        else {
            foreach ($source in @($files | Where-Object Extension -IEQ '.c')) {
                $output = $source.BaseName.ToUpperInvariant()
                $cOutputs += $output
                if (-not $AssemblyOnly) {
                    New-BuildJob 'dcc' $directory.FullName $source.FullName "$output.COM" $targets @(
                        "dcc-input=$($source.Name)", "dcc-output=$output", 'dcc-build-dir=build'
                    )
                }
            }
        }
        foreach ($source in @($files | Where-Object Extension -IEQ '.asm')) {
            # C builds take precedence over alternate assembly implementations
            # with the same executable name, including in AssemblyOnly mode.
            if ($source.BaseName -in $cOutputs) { continue }
            New-BuildJob 'asm' $directory.FullName $source.FullName "$($source.BaseName.ToUpperInvariant()).COM" $targets
        }
        if (-not $AssemblyOnly) {
            foreach ($source in @($files | Where-Object Extension -IEQ '.bas')) {
                New-BuildJob 'basic' $directory.FullName $source.FullName ($source.Name.ToUpperInvariant()) $targets
            }
        }
    }
})

# Libraries generate the host sources; preserve discovery order within each group.
$jobs = @($jobs | Sort-Object -Stable @{Expression={ if ($_.Kind -eq 'library') { 0 } else { 1 } }})

# Reject duplicate destinations before compiling or changing images.
$destinations = @{}
foreach ($job in $jobs) {
    $artifacts = if ($job.Kind -eq 'composite') { $job.Members } else { @($job) }
    foreach ($artifact in $artifacts) {
        if ($artifact.Output -notmatch '^[A-Za-z0-9_]{1,8}\.(COM|BAS|LIB)$') {
            throw "Expected an 8.3 CP/M filename: $($artifact.Output)"
        }
        foreach ($drive in $artifact.Targets) {
            $key = "${drive}:$($artifact.Output)"
            if ($destinations.ContainsKey($key)) { throw "Duplicate application destination: $key" }
            $destinations[$key] = $true
        }
    }
}

if ($ListOnly) {
    $jobs | Select-Object Kind, Source, Output, @{Name='Drives'; Expression={$_.Targets -join ','}}
    return
}

if ($Clone) {
    $clonePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Clone)
    if (-not (Test-Path -LiteralPath $clonePath -PathType Container)) {
        throw "Clone destination folder not found: $clonePath"
    }
    $clonePath = (Resolve-Path -LiteralPath $clonePath).Path
}
elseif ($CloneOnly) {
    throw '-CloneOnly requires -c <destination-folder>'
}

if ($CloneOnly) {
    foreach ($drive in @($images.Keys)) {
        $sourcePath = (Resolve-Path -LiteralPath $images[$drive]).Path
        $destination = Join-Path $clonePath (Split-Path $sourcePath -Leaf)
        Copy-Item -LiteralPath $sourcePath -Destination $destination -Force
        Write-Host "Cloned $drive`: $sourcePath -> $destination"
    }
    return
}

# Resolve destination overrides relative to the caller before changing location.
foreach ($drive in @($images.Keys)) {
    $images[$drive] = (Resolve-Path -LiteralPath $images[$drive]).Path
}
if ($DccDirectory) {
    $DccDirectory = (Resolve-Path -LiteralPath $DccDirectory).Path
    if ($Dccmake -eq 'dccmake') {
        $Dccmake = Join-Path $DccDirectory $(if ($IsWindows) { 'dccmake.exe' } else { 'dccmake' })
    }
}
if (@($jobs | Where-Object { $_.Kind -in @('dcc', 'library', 'composite') }).Count) {
    $Dccmake = (Get-Command $Dccmake -CommandType Application -ErrorAction Stop).Source
}
if ($LibraryLinker -and @($jobs | Where-Object { $_.Kind -in @('library', 'composite') }).Count) {
    $LibraryLinker = (Get-Command $LibraryLinker -CommandType Application -ErrorAction Stop).Source
}

$timer = [Diagnostics.Stopwatch]::StartNew()
Write-Host "Building/installing $($jobs.Count) build units. Stop the emulator before updating its images."
for ($index = 0; $index -lt $jobs.Count; $index++) {
    $job = $jobs[$index]
    Write-Host "[$($index + 1)/$($jobs.Count)] $($job.Output) ($($job.Kind)) -> $($job.Targets -join ',')"
    $hostFile = Join-Path $job.Directory $job.Output
    switch ($job.Kind) {
        { $_ -in @('dcc', 'library', 'composite') } {
            $dccArguments = @($job.Arguments)
            if ($job.Kind -in @('library', 'composite') -and $LibraryLinker) {
                $dccArguments += "dcc-library-linker=$LibraryLinker"
            }
            if ([IO.Path]::GetExtension($job.Source) -ieq '.c') {
                $toolDirectory = Split-Path $Dccmake -Parent
                $dccArguments += @(
                    "dcc-runtime=$(Join-Path $toolDirectory 'DCCRTL.MAC')",
                    "dcc-include-directory=$toolDirectory"
                )
            }
            $previousDccDirectory = $env:DCC_DIR
            try {
                if ($DccDirectory) { $env:DCC_DIR = $DccDirectory }
                elseif (-not $env:DCC_DIR) { $env:DCC_DIR = Split-Path $Dccmake -Parent }
                if ($job.Kind -eq 'composite') {
                    foreach ($member in $job.Members) {
                        & (Join-Path $scriptsRoot 'build-dcc-app.ps1') -AppDirectory $member.Directory `
                            -Dccmake $Dccmake -BuildOnly -ValidateOnly
                    }
                    foreach ($member in $job.Members) {
                        $published = Join-Path $member.Directory $member.Output
                        if (Test-Path -LiteralPath $published) { Remove-Item -LiteralPath $published -Force }
                    }
                    Push-Location $job.Directory
                    try {
                        & $Dccmake @dccArguments
                        if ($LASTEXITCODE -ne 0) { throw "Composite build failed with exit code $LASTEXITCODE" }
                    }
                    finally {
                        Pop-Location
                    }
                    foreach ($member in $job.Members) {
                        & (Join-Path $scriptsRoot 'build-dcc-app.ps1') -AppDirectory $member.Directory `
                            -Dccmake $Dccmake -BuildOnly -CollectOnly
                    }
                }
                else {
                    & (Join-Path $scriptsRoot 'build-dcc-app.ps1') -AppDirectory $job.Directory `
                        -DccArguments $dccArguments -Dccmake $Dccmake -BuildOnly
                }
            }
            finally {
                $env:DCC_DIR = $previousDccDirectory
            }
        }
        'asm' {
            & (Join-Path $scriptsRoot 'build-asm-app.ps1') -Source $job.Source `
                -DccDirectory $DccDirectory -Ntvcm $Ntvcm -BuildOnly
        }
        'basic' { $hostFile = $job.Source }
    }
    $artifacts = if ($job.Kind -eq 'composite') { $job.Members } else { @($job) }
    foreach ($artifact in $artifacts) {
        if ($job.Kind -eq 'composite') { $hostFile = Join-Path $artifact.Directory $artifact.Output }
        foreach ($drive in $artifact.Targets) {
            & (Join-Path $scriptsRoot 'cpm-install.ps1') -Image $images[$drive] `
                -HostFile $hostFile -CpmFile "0:$($artifact.Output)"
        }
    }
}
if ($Clone) {
    foreach ($drive in @($images.Keys)) {
        $destination = Join-Path $clonePath (Split-Path $images[$drive] -Leaf)
        Copy-Item -LiteralPath $images[$drive] -Destination $destination -Force
        Write-Host "Cloned $drive`: $($images[$drive]) -> $destination"
    }
}
$timer.Stop()
Write-Host "BUILD ALL PASS: $($jobs.Count) build units ($($timer.ElapsedMilliseconds) ms)"
