#Requires -Version 5.1
# Shared implementation for ReleaseVcDeploy.ps1 / DebugVcDeploy.ps1.
# Expects caller to set:
#   $Script:VcDeployToolName
#   $Script:VcDeployConfiguration  ('Release' | 'Debug')
#   $Script:VcDeployShowHelpEnv

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $Script:VcDeployConfiguration) {
    throw 'VcDeployCommon.ps1 must be dot-sourced after setting $Script:VcDeployConfiguration.'
}

$Script:ReleaseRuntimePatterns = @(
    '^msvcp140(_[\w]+)?\.dll$'
    '^vcruntime140(_[\w]+)?\.dll$'
    '^concrt140\.dll$'
    '^ucrtbase\.dll$'
)

$Script:DebugRuntimePatterns = @(
    '^msvcp140.*d\.dll$'
    '^vcruntime140.*d\.dll$'
    '^concrt140d\.dll$'
    '^ucrtbase.*d\.dll$'
)

$Script:ReleaseFallbackDlls = @(
    'msvcp140.dll'
    'vcruntime140.dll'
    'vcruntime140_1.dll'
    'msvcp140_1.dll'
    'msvcp140_2.dll'
    'msvcp140_atomic_wait.dll'
    'msvcp140_codecvt_ids.dll'
    'concrt140.dll'
    'ucrtbase.dll'
)

$Script:DebugFallbackDlls = @(
    'msvcp140d.dll'
    'vcruntime140d.dll'
    'vcruntime140_1d.dll'
    'msvcp140_1d.dll'
    'msvcp140_2d.dll'
    'msvcp140_atomic_waitd.dll'
    'msvcp140_codecvt_idsd.dll'
    'concrt140d.dll'
    'ucrtbased.dll'
)

function Write-Info([string]$Message) {
    Write-Host $Message
}

function Write-Warn([string]$Message) {
    Write-Warning $Message
}

function Get-HelpText {
    $config = $Script:VcDeployConfiguration
    $tool = $Script:VcDeployToolName
    $ucrtName = if ($config -eq 'Debug') { 'ucrtbased.dll' } else { 'ucrtbase.dll' }
    $crtHint = if ($config -eq 'Debug') {
        'msvcp140d.dll, vcruntime140d.dll, vcruntime140_1d.dll, ucrtbased.dll'
    } else {
        'msvcp140.dll, vcruntime140.dll, vcruntime140_1.dll, ucrtbase.dll'
    }

    return @"
$tool - Deploy MSVC / UCRT runtime libraries for $config builds (windeployqt-style)

Usage:
  $tool.bat --dir <targetDir> <program.exe> [program2.exe ...]
  $tool.bat --help

Options:
  --dir <path>     Output directory (created if missing). Same role as windeployqt --dir.
  --help, -h, /?   Show this help.
  --no-ucrt        Do not copy $ucrtName (UCRT).
  --no-system      Do not fall back to System32 when redist folders are missing.

Examples:
  windeployqt --dir NDI .\build\bin\$config\NDIReceiver.exe
  $tool.bat --dir NDI .\build\bin\$config\NDIReceiver.exe .\build\bin\$config\NDISender.exe

Notes:
  - Parses PE imports via Visual Studio dumpbin when available.
  - Copies from VC++ Redist and Windows SDK UCRT redist when found.
  - Typical $config files: $crtHint
  - Debug runtimes are non-redistributable; use DebugVcDeploy for local/dev machines only.
"@
}

function Show-Help {
    Write-Host (Get-HelpText)
}

function Test-RuntimeDllName([string]$Name) {
    $lower = $Name.ToLowerInvariant()
    $patterns = if ($Script:VcDeployConfiguration -eq 'Debug') {
        $Script:DebugRuntimePatterns
    } else {
        $Script:ReleaseRuntimePatterns
    }

    if ($Script:VcDeployConfiguration -eq 'Release') {
        if ($lower -match '(msvcp140.*d\.dll|vcruntime140.*d\.dll|ucrtbase.*d\.dll|concrt140d\.dll)$') {
            return $false
        }
    } else {
        if ($lower -match '(^msvcp140\.dll$|^vcruntime140\.dll$|^vcruntime140_1\.dll$|^ucrtbase\.dll$|^concrt140\.dll$)') {
            return $false
        }
    }

    foreach ($pattern in $patterns) {
        if ($lower -match $pattern) {
            return $true
        }
    }
    return $false
}

function Get-VsInstallPath {
    $candidates = New-Object 'System.Collections.Generic.List[string]'

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $path = & $vswhere -latest -products * -property installationPath 2>$null
        if (-not [string]::IsNullOrWhiteSpace($path)) {
            $candidates.Add($path.Trim())
        }
    }

    if ($env:VSINSTALLDIR) {
        $candidates.Add($env:VSINSTALLDIR.TrimEnd('\', '/'))
    }
    if ($env:VCToolsInstallDir) {
        $toolsRoot = Split-Path (Split-Path $env:VCToolsInstallDir.TrimEnd('\', '/') -Parent) -Parent
        if ($toolsRoot) {
            $candidates.Add($toolsRoot)
        }
    }

    foreach ($candidate in $candidates) {
        foreach ($root in Get-VsRootCandidates $candidate) {
            $redistRoot = Join-Path $root 'VC\Redist\MSVC'
            if (Test-Path -LiteralPath $redistRoot) {
                return $root
            }
        }
    }

    return $null
}

function Get-VsRootCandidates([string]$Path) {
    $results = New-Object 'System.Collections.Generic.List[string]'
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return @()
    }

    $current = $Path.TrimEnd('\', '/')
    [void]$results.Add($current)

    if ($current -match '(?i)\\Common7\\IDE$') {
        [void]$results.Add((Split-Path (Split-Path $current -Parent) -Parent))
    } elseif ($current -match '(?i)\\IDE$') {
        [void]$results.Add((Split-Path $current -Parent))
    }

    return @($results.ToArray() | Select-Object -Unique)
}

function Get-LatestChildDirectory([string]$Root) {
    if (-not (Test-Path -LiteralPath $Root)) {
        return $null
    }
    return Get-ChildItem -LiteralPath $Root -Directory |
        Sort-Object Name -Descending |
        Select-Object -First 1
}

function Get-DumpBinPath([string]$VsInstallPath) {
    if (-not $VsInstallPath) {
        return $null
    }

    $msvcRoot = Join-Path $VsInstallPath 'VC\Tools\MSVC'
    $toolset = Get-LatestChildDirectory $msvcRoot
    if (-not $toolset) {
        return $null
    }

    $dumpbin = Join-Path $toolset.FullName 'bin\Hostx64\x64\dumpbin.exe'
    if (Test-Path -LiteralPath $dumpbin) {
        return $dumpbin
    }
    return $null
}

function Get-VcCrtRedistDirectories([string]$VsInstallPath) {
    $dirs = New-Object 'System.Collections.Generic.List[string]'
    if (-not $VsInstallPath) {
        return @()
    }

    $redistRoot = Join-Path $VsInstallPath 'VC\Redist\MSVC'
    $redistVersion = Get-LatestChildDirectory $redistRoot
    if (-not $redistVersion) {
        return @()
    }

    if ($Script:VcDeployConfiguration -eq 'Debug') {
        $debugNonRedist = Join-Path $redistVersion.FullName 'debug_nonredist\x64'
        if (Test-Path -LiteralPath $debugNonRedist) {
            $dirs.Add($debugNonRedist) | Out-Null
        }

        $x64Root = Join-Path $redistVersion.FullName 'x64'
        if (Test-Path -LiteralPath $x64Root) {
            Get-ChildItem -LiteralPath $x64Root -Directory |
                Where-Object { $_.Name -like 'Microsoft.VC*.DebugCRT' } |
                ForEach-Object { $dirs.Add($_.FullName) | Out-Null }
        }
    } else {
        $x64Root = Join-Path $redistVersion.FullName 'x64'
        if (Test-Path -LiteralPath $x64Root) {
            Get-ChildItem -LiteralPath $x64Root -Directory |
                Where-Object { $_.Name -like 'Microsoft.VC*.CRT' -and $_.Name -notlike '*.DebugCRT' } |
                ForEach-Object { $dirs.Add($_.FullName) | Out-Null }
        }
    }

    return @($dirs.ToArray())
}

function Get-UcrtRedistDirectory {
    $candidates = New-Object 'System.Collections.Generic.List[string]'

    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Redist'
    $kitVersion = Get-LatestChildDirectory $kitsRoot
    if ($kitVersion) {
        $candidates.Add((Join-Path $kitVersion.FullName 'ucrt\DLLs\x64'))
    }

    if ($env:WindowsSdkDir) {
        $candidates.Add((Join-Path $env:WindowsSdkDir 'Redist\ucrt\DLLs\x64'))
    }

    $ucrtFile = if ($Script:VcDeployConfiguration -eq 'Debug') { 'ucrtbased.dll' } else { 'ucrtbase.dll' }

    foreach ($dir in ($candidates.ToArray() | Select-Object -Unique)) {
        if (Test-Path -LiteralPath (Join-Path $dir $ucrtFile)) {
            return $dir
        }
    }

    return $null
}

function Get-PeDependencies([string]$ExePath, [string]$DumpBinPath) {
    $deps = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)

    if ($DumpBinPath) {
        $output = @(& $DumpBinPath /nologo /dependents $ExePath 2>$null)
        foreach ($line in $output) {
            $trimmed = $line.Trim()
            if ($trimmed -match '\.dll$') {
                [void]$deps.Add($trimmed)
            }
        }
        return $deps
    }

    $fallback = if ($Script:VcDeployConfiguration -eq 'Debug') {
        $Script:DebugFallbackDlls
    } else {
        $Script:ReleaseFallbackDlls
    }

    $fallback | ForEach-Object { [void]$deps.Add($_) }
    Write-Warn "dumpbin not found; using default $($Script:VcDeployConfiguration) MSVC x64 runtime set for '$ExePath'."
    return $deps
}

function Resolve-RuntimeDllPath {
    param(
        [string]$DllName,
        [string[]]$VcCrtDirs,
        [string]$UcrtDir,
        [switch]$AllowSystemFallback
    )

    $lower = $DllName.ToLowerInvariant()
    $searchDirs = @()

    if (($lower -eq 'ucrtbase.dll' -or $lower -eq 'ucrtbased.dll') -and $UcrtDir) {
        $searchDirs += $UcrtDir
    }
    $searchDirs += $VcCrtDirs

    foreach ($dir in $searchDirs) {
        $candidate = Join-Path $dir $DllName
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    if ($AllowSystemFallback) {
        $systemDir = Join-Path $env:WINDIR 'System32'
        $candidate = Join-Path $systemDir $DllName
        if (Test-Path -LiteralPath $candidate) {
            Write-Warn "Using system copy for $DllName from $systemDir"
            return $candidate
        }
    }

    return $null
}

function Parse-Arguments([string[]]$CliArguments) {
    $CliArguments = @($CliArguments)

    $result = [ordered]@{
        TargetDir = $null
        Binaries = New-Object 'System.Collections.Generic.List[string]'
        IncludeUcrt = $true
        AllowSystemFallback = $true
        ShowHelp = $false
    }

    $i = 0
    while ($i -lt $CliArguments.Count) {
        $arg = $CliArguments[$i]
        switch -Regex ($arg) {
            '^(--help|-h|/\?|\/?)$' {
                $result.ShowHelp = $true
            }
            '^--dir$' {
                $i++
                if ($i -ge $CliArguments.Count) {
                    throw "Missing value for --dir"
                }
                $result.TargetDir = $CliArguments[$i]
            }
            '^--no-ucrt$' {
                $result.IncludeUcrt = $false
            }
            '^--no-system$' {
                $result.AllowSystemFallback = $false
            }
            default {
                if ($arg.StartsWith('-')) {
                    throw "Unknown option: $arg"
                }
                $result.Binaries.Add((Resolve-Path -LiteralPath $arg).Path)
            }
        }
        $i++
    }

    return [pscustomobject]$result
}

function Invoke-VcDeployMain {
    param(
        [string[]]$CliArgs = @()
    )

    $CliArgs = @($CliArgs)

    if ($CliArgs.Count -eq 0) {
        Show-Help
        return 1
    }

    $parsed = Parse-Arguments $CliArgs
    if ($parsed.ShowHelp) {
        Show-Help
        return 0
    }

    if (-not $parsed.TargetDir) {
        Write-Error "Missing required option --dir <targetDir>"
        Show-Help
        return 1
    }

    if ($parsed.Binaries.Count -eq 0) {
        Write-Error "At least one .exe path is required."
        Show-Help
        return 1
    }

    $targetDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($parsed.TargetDir)
    if (-not (Test-Path -LiteralPath $targetDir)) {
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    }

    $vsPath = Get-VsInstallPath
    $dumpbin = Get-DumpBinPath $vsPath
    $vcCrtDirs = @(Get-VcCrtRedistDirectories $vsPath)
    $ucrtDir = Get-UcrtRedistDirectory

    if (@($vcCrtDirs).Count -eq 0) {
        Write-Warn "VC++ CRT redist folder not found under Visual Studio installation."
    }
    if (-not $ucrtDir) {
        Write-Warn "Windows SDK UCRT redist folder not found."
    }

    $required = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($binary in $parsed.Binaries) {
        if ($binary -notmatch '\.exe$') {
            Write-Warn "Skipping non-exe input: $binary"
            continue
        }
        Write-Info "Analyzing: $binary"
        $deps = Get-PeDependencies -ExePath $binary -DumpBinPath $dumpbin
        foreach ($dep in $deps) {
            if (Test-RuntimeDllName $dep) {
                [void]$required.Add($dep)
            }
        }
    }

    if (-not $parsed.IncludeUcrt) {
        if ($Script:VcDeployConfiguration -eq 'Debug') {
            [void]$required.Remove('ucrtbased.dll')
        } else {
            [void]$required.Remove('ucrtbase.dll')
        }
    }

    if (@($required).Count -eq 0) {
        $otherTool = if ($Script:VcDeployConfiguration -eq 'Debug') { 'ReleaseVcDeploy' } else { 'DebugVcDeploy' }
        Write-Warn "No $($Script:VcDeployConfiguration) MSVC/UCRT runtime dependencies detected."
        Write-Warn "Use $otherTool.bat if the target was built with a different runtime (/MD vs /MDd)."
        return 0
    }

    Write-Info ""
    Write-Info "Deploying $($Script:VcDeployConfiguration) runtime to: $targetDir"
    $copied = 0
    $missing = New-Object 'System.Collections.Generic.List[string]'

    foreach ($dll in ($required | Sort-Object)) {
        $source = Resolve-RuntimeDllPath -DllName $dll -VcCrtDirs $vcCrtDirs -UcrtDir $ucrtDir -AllowSystemFallback:$parsed.AllowSystemFallback
        if (-not $source) {
            $missing.Add($dll)
            continue
        }

        $dest = Join-Path $targetDir $dll
        Copy-Item -LiteralPath $source -Destination $dest -Force
        Write-Info "  copied: $dll"
        $copied++
    }

    Write-Info ""
    Write-Info "Done. $copied runtime file(s) copied."
    if (@($missing).Count -gt 0) {
        Write-Warn "Missing runtime file(s): $($missing -join ', ')"
        return 2
    }
    return 0
}

function Start-VcDeploy {
    param(
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$ArgumentList = @()
    )

    $helpFlag = [Environment]::GetEnvironmentVariable($Script:VcDeployShowHelpEnv)
    if ($helpFlag -eq '1') {
        Show-Help
        exit 0
    }

    exit (Invoke-VcDeployMain -CliArgs @($ArgumentList))
}
