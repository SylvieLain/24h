param(
    [string]$ProjectRoot = (Get-Location).Path,
    [string]$Syscfg,
    [string]$OutDir,
    [string]$Compiler = "gcc",
    [string]$SysConfigRoot = $env:SYSCONFIG_ROOT,
    [string]$Mspm0SdkRoot = $env:MSPM0_SDK_ROOT,
    [string]$SysConfigCli = $env:SYSCONFIG_CLI,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Resolve-ExistingPath {
    param(
        [Parameter(Mandatory = $true)][string]$PathValue,
        [Parameter(Mandatory = $true)][string]$Name
    )

    if (-not (Test-Path -LiteralPath $PathValue)) {
        throw "$Name not found: $PathValue"
    }

    return (Resolve-Path -LiteralPath $PathValue).Path
}

function Get-FileSystemDriveRoots {
    Get-PSDrive -PSProvider FileSystem |
        Where-Object { Test-Path -LiteralPath $_.Root } |
        ForEach-Object { $_.Root.TrimEnd('\') }
}

function Get-SysConfigCliCandidates {
    param([string]$RequestedRoot, [string]$RequestedCli)

    if ($RequestedCli) {
        $RequestedCli
    }

    if ($RequestedRoot) {
        Join-Path $RequestedRoot "sysconfig_cli.bat"
    }

    foreach ($driveRoot in Get-FileSystemDriveRoots) {
        $toolRoots = @(
            (Join-Path $driveRoot "TI"),
            (Join-Path $driveRoot "ti")
        )

        foreach ($toolRoot in $toolRoots) {
            if (Test-Path -LiteralPath $toolRoot) {
                Get-ChildItem -LiteralPath $toolRoot -Directory -ErrorAction SilentlyContinue |
                    Where-Object { $_.Name -like "sysconfig_*" } |
                    Sort-Object Name -Descending |
                    ForEach-Object { Join-Path $_.FullName "sysconfig_cli.bat" }
            }
        }

        Get-ChildItem -LiteralPath ($driveRoot + "\") -Directory -ErrorAction SilentlyContinue |
            ForEach-Object {
                $ccsUtils = Join-Path $_.FullName "ccs\utils"
                if (Test-Path -LiteralPath $ccsUtils) {
                    Get-ChildItem -LiteralPath $ccsUtils -Directory -ErrorAction SilentlyContinue |
                        Where-Object { $_.Name -like "sysconfig_*" } |
                        Sort-Object Name -Descending |
                        ForEach-Object { Join-Path $_.FullName "sysconfig_cli.bat" }
                }
            }
    }
}

function Find-SysConfigCli {
    param([string]$RequestedRoot, [string]$RequestedCli)

    $candidates = Get-SysConfigCliCandidates -RequestedRoot $RequestedRoot -RequestedCli $RequestedCli
    $match = $candidates |
        Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
        Select-Object -First 1

    if (-not $match) {
        throw "SysConfig CLI not found. Set SYSCONFIG_ROOT or SYSCONFIG_CLI."
    }

    return (Resolve-Path -LiteralPath $match).Path
}

function Get-Mspm0SdkRootCandidates {
    param([string]$RequestedRoot)

    if ($RequestedRoot) {
        $RequestedRoot
    }

    foreach ($driveRoot in Get-FileSystemDriveRoots) {
        $commonRoots = @(
            $driveRoot,
            (Join-Path $driveRoot "TI"),
            (Join-Path $driveRoot "ti")
        )

        foreach ($root in $commonRoots) {
            if (Test-Path -LiteralPath $root) {
                Get-ChildItem -LiteralPath $root -Directory -Force -ErrorAction SilentlyContinue |
                    Where-Object { $_.Name -like "mspm0_sdk_*" } |
                    Sort-Object Name -Descending |
                    Select-Object -ExpandProperty FullName
            }
        }

        Get-ChildItem -LiteralPath ($driveRoot + "\") -Directory -Force -ErrorAction SilentlyContinue |
            ForEach-Object {
                Get-ChildItem -LiteralPath $_.FullName -Directory -Force -ErrorAction SilentlyContinue |
                    Where-Object { $_.Name -like "mspm0_sdk_*" } |
                    Sort-Object Name -Descending |
                    Select-Object -ExpandProperty FullName
            }
    }
}

function Find-Mspm0SdkRoot {
    param([string]$RequestedRoot)

    $match = Get-Mspm0SdkRootCandidates -RequestedRoot $RequestedRoot |
        Where-Object {
            $_ -and (Test-Path -LiteralPath (Join-Path $_ ".metadata\product.json"))
        } |
        Select-Object -First 1

    if (-not $match) {
        throw "MSPM0 SDK not found. Set MSPM0_SDK_ROOT."
    }

    return (Resolve-Path -LiteralPath $match).Path
}

function Find-SyscfgFile {
    param([string]$Root, [string]$RequestedSyscfg)

    if ($RequestedSyscfg) {
        $path = $RequestedSyscfg
        if (-not [System.IO.Path]::IsPathRooted($path)) {
            $path = Join-Path $Root $path
        }

        return Resolve-ExistingPath -PathValue $path -Name "syscfg file"
    }

    $matches = @(Get-ChildItem -LiteralPath $Root -Filter "*.syscfg" -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object {
            $_.FullName -notmatch '\\(\.git|\.eide-sync|build|Debug|Release)\\'
        } |
        Select-Object -ExpandProperty FullName)

    if ($matches.Count -eq 0) {
        throw "No .syscfg file found under: $Root"
    }

    if ($matches.Count -gt 1) {
        $list = ($matches | ForEach-Object { "  $_" }) -join [Environment]::NewLine
        throw "Multiple .syscfg files found. Pass -Syscfg explicitly:$([Environment]::NewLine)$list"
    }

    return $matches[0]
}

function Resolve-OutputDir {
    param([string]$SyscfgPath, [string]$RequestedOutDir)

    if ($RequestedOutDir) {
        if ([System.IO.Path]::IsPathRooted($RequestedOutDir)) {
            return $RequestedOutDir
        }

        return Join-Path (Split-Path -Parent $SyscfgPath) $RequestedOutDir
    }

    $syscfgDir = Split-Path -Parent $SyscfgPath
    if ((Split-Path -Leaf $syscfgDir) -ieq "source") {
        return $syscfgDir
    }

    return Join-Path $syscfgDir "source"
}

function Format-CommandPart {
    param([string]$Value)

    if ($Value -match '[\s"`&|<>]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }

    return $Value
}

$projectRootPath = Resolve-ExistingPath -PathValue $ProjectRoot -Name "project root"
$syscfgPath = Find-SyscfgFile -Root $projectRootPath -RequestedSyscfg $Syscfg
$outDirPath = Resolve-OutputDir -SyscfgPath $syscfgPath -RequestedOutDir $OutDir
$sysConfigCliPath = Find-SysConfigCli -RequestedRoot $SysConfigRoot -RequestedCli $SysConfigCli
$sdkRootPath = Find-Mspm0SdkRoot -RequestedRoot $Mspm0SdkRoot
$productJsonPath = Resolve-ExistingPath -PathValue (Join-Path $sdkRootPath ".metadata\product.json") -Name "SDK product.json"

$commandArgs = @(
    "-s", $productJsonPath,
    "--script", $syscfgPath,
    "-o", $outDirPath,
    "--compiler", $Compiler
)

Write-Host "SysConfig CLI: $sysConfigCliPath"
Write-Host "MSPM0 SDK:     $sdkRootPath"
Write-Host "Syscfg:        $syscfgPath"
Write-Host "Output:        $outDirPath"
Write-Host "Compiler:      $Compiler"

if ($DryRun) {
    Write-Host "Dry run only. Command was not executed."
    $printableArgs = $commandArgs | ForEach-Object { Format-CommandPart $_ }
    Write-Host "& $(Format-CommandPart $sysConfigCliPath) $($printableArgs -join ' ')"
    exit 0
}

if (-not (Test-Path -LiteralPath $outDirPath)) {
    New-Item -ItemType Directory -Path $outDirPath | Out-Null
}

& $sysConfigCliPath @commandArgs
exit $LASTEXITCODE
