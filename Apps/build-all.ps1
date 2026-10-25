#!/usr/bin/env pwsh

[CmdletBinding()]
param(
    [switch] $AssemblyOnly,
    [string] $DccDirectory,
    [string] $Dccmake = 'dccmake',
    [string] $Ntvcm = 'ntvcm',
    [string] $DiskA,
    [string] $DiskB,
    [string] $DiskC,
    [string] $DiskD,
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
    param($Kind, $Directory, $Source, $Output, $Targets, $Arguments = @())
    [pscustomobject]@{
        Kind = $Kind; Directory = $Directory; Source = $Source
        Output = $Output; Targets = @($Targets); Arguments = @($Arguments)
    }
}

# Inspect bucket roots and immediate app directories, excluding nested tests,
# fixtures and generated build directories.
$jobs = @(foreach ($bucket in @('all', 'a', 'b', 'c', 'd')) {
    $bucketPath = Join-Path $appsRoot "disk-$bucket-apps"
    if (-not (Test-Path -LiteralPath $bucketPath -PathType Container)) {
        throw "Application folder not found: $bucketPath"
    }
    $targets = if ($bucket -eq 'all') { @('A', 'B', 'C', 'D') } else { @($bucket.ToUpperInvariant()) }
    $directories = @((Get-Item -LiteralPath $bucketPath)) + @(Get-ChildItem -LiteralPath $bucketPath -Directory | Sort-Object Name)
    foreach ($directory in $directories) {
        $files = @(Get-ChildItem -LiteralPath $directory.FullName -File | Sort-Object Name)
        $config = Join-Path $directory.FullName 'dccmake.txt'
        $cOutputs = @()
        if (Test-Path -LiteralPath $config -PathType Leaf) {
            $outputs = @(Get-Content -LiteralPath $config | ForEach-Object {
                if ($_ -match '^\s*dcc-output\s*=\s*([^#\s]+)') { $Matches[1] }
            })
            if (-not $outputs) { throw "dcc-output is missing in $config" }
            $output = $outputs[-1] -replace '(?i)\.COM$', ''
            $cOutputs += $output
            if (-not $AssemblyOnly) {
                New-BuildJob 'dcc' $directory.FullName $config "$output.COM" $targets
                # Preserve both executables previously built by the ISAM wrapper.
                if ($directory.Name -eq 'ISAM') {
                    New-BuildJob 'dcc' $directory.FullName $config 'DOCGEN.COM' $targets @(
                        'dcc-input=docgen.c,isamdb.c', 'dcc-output=DOCGEN', 'dcc-build-dir=build-docgen'
                    )
                }
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

# Reject duplicate destinations before compiling or changing images.
$destinations = @{}
foreach ($job in $jobs) {
    if ($job.Output -notmatch '^[A-Za-z0-9_]{1,8}\.(COM|BAS)$') {
        throw "Expected an 8.3 CP/M filename: $($job.Output)"
    }
    foreach ($drive in $job.Targets) {
        $key = "${drive}:$($job.Output)"
        if ($destinations.ContainsKey($key)) { throw "Duplicate application destination: $key" }
        $destinations[$key] = $true
    }
}

if ($ListOnly) {
    $jobs | Select-Object Kind, Source, Output, @{Name='Drives'; Expression={$_.Targets -join ','}}
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
if (@($jobs | Where-Object Kind -EQ 'dcc').Count) {
    $Dccmake = (Get-Command $Dccmake -CommandType Application -ErrorAction Stop).Source
}

$timer = [Diagnostics.Stopwatch]::StartNew()
Write-Host "Building/installing $($jobs.Count) applications. Stop the emulator before updating its images."
for ($index = 0; $index -lt $jobs.Count; $index++) {
    $job = $jobs[$index]
    Write-Host "[$($index + 1)/$($jobs.Count)] $($job.Output) ($($job.Kind)) -> $($job.Targets -join ',')"
    $hostFile = Join-Path $job.Directory $job.Output
    switch ($job.Kind) {
        'dcc' {
            $dccArguments = @($job.Arguments)
            if ([IO.Path]::GetExtension($job.Source) -ieq '.c') {
                $toolDirectory = Split-Path $Dccmake -Parent
                $dccArguments += @(
                    "dcc-runtime=$(Join-Path $toolDirectory 'DCCRTL.MAC')",
                    "dcc-include-directory=$toolDirectory"
                )
            }
            & (Join-Path $scriptsRoot 'build-dcc-app.ps1') -AppDirectory $job.Directory `
                -DccArguments $dccArguments -Dccmake $Dccmake -BuildOnly
        }
        'asm' {
            & (Join-Path $scriptsRoot 'build-asm-app.ps1') -Source $job.Source `
                -DccDirectory $DccDirectory -Ntvcm $Ntvcm -BuildOnly
        }
        'basic' { $hostFile = $job.Source }
    }
    foreach ($drive in $job.Targets) {
        & (Join-Path $scriptsRoot 'cpm-install.ps1') -Image $images[$drive] `
            -HostFile $hostFile -CpmFile "0:$($job.Output)"
    }
}
$timer.Stop()
Write-Host "BUILD ALL PASS: $($jobs.Count) applications ($($timer.ElapsedMilliseconds) ms)"
