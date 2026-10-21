param(
    [string]$RepositoryRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [switch]$Apply,
    [string[]]$DependencyRoots = @(),
    [string]$RollbackManifest
)
$ErrorActionPreference = 'Stop'
$repoRoot = [System.IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\','/')
if (!(Test-Path -LiteralPath (Join-Path $repoRoot 'Aster-Engine.tinyproject')) -or
    !(Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt')) -or
    (Get-Content -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt') -Raw) -notmatch 'project\(Aster-Engine\b') {
    throw "Not an Aster-Engine repository root: $repoRoot"
}
function CheckedPath([string]$path) {
    $full = [System.IO.Path]::GetFullPath($path)
    if (!$full.StartsWith($repoRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path escapes repository: $full"
    }
    $ancestor = $full
    while ($ancestor -and $ancestor -ne $repoRoot) {
        if (Test-Path -LiteralPath $ancestor) {
            if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
                throw "Refusing a symbolic link or junction: $ancestor"
            }
        }
        $ancestor = Split-Path -Parent $ancestor
    }
    return $full
}
function SaveManifest([string]$path, $record) {
    $temporary = $path + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    [System.IO.File]::WriteAllText($temporary, ($record | ConvertTo-Json -Depth 6), [System.Text.UTF8Encoding]::new())
    if (Test-Path -LiteralPath $path) { [System.IO.File]::Replace($temporary, $path, $path + '.previous') }
    else { [System.IO.File]::Move($temporary, $path) }
}
if ($RollbackManifest) {
    $manifestPath = CheckedPath $RollbackManifest
    $record = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($record.RepositoryRoot -ne $repoRoot) { throw 'Manifest belongs to a different repository' }
    $undo = @()
    foreach ($entry in @($record.Moves)) {
        $null = CheckedPath $entry.Source
        $null = CheckedPath $entry.Destination
        $sourceExists = Test-Path -LiteralPath $entry.Source
        $destinationExists = Test-Path -LiteralPath $entry.Destination
        if ($sourceExists -eq $destinationExists) { throw "Ambiguous rollback paths: $($entry.Source)" }
        if ($destinationExists) { $undo += $entry }
        else { $entry.Status = 'Restored' }
    }
    [array]::Reverse($undo)
    SaveManifest $manifestPath $record
    foreach ($entry in $undo) {
        $entry.Status = 'Restoring'
        SaveManifest $manifestPath $record
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $entry.Source) | Out-Null
        Move-Item -LiteralPath $entry.Destination -Destination $entry.Source
        $entry.Status = 'Restored'
        SaveManifest $manifestPath $record
    }
    Write-Host "Restored $($undo.Count) paths using $manifestPath"
    return
}
if ($DependencyRoots.Count -eq 0) {
    $DependencyRoots = @($repoRoot)
    $worktrees = & git -C $repoRoot worktree list --porcelain 2>$null
    foreach ($line in $worktrees) {
        if ($line.StartsWith('worktree ')) { $DependencyRoots += $line.Substring(9) }
    }
}
$caches = @()
foreach ($dependencyRoot in ($DependencyRoots | Select-Object -Unique)) {
    if (!(Test-Path -LiteralPath $dependencyRoot)) { continue }
    $caches += Get-ChildItem -LiteralPath $dependencyRoot -Filter CMakeCache.txt -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '[\\/]Intermediate[\\/]LegacyBuilds[\\/]|[\\/]Saved[\\/]Archive[\\/]' }
}
$processes = @(Get-CimInstance Win32_Process)
$engineProcesses = @($processes | Where-Object Name -like 'tinyengine*')
function InUse([string]$path) {
    $prefix = $path.TrimEnd('\','/') + '\'
    $pattern = [regex]::Escape($path.Replace('\','/')) + '(?=[/\s";]|$)'
    foreach ($process in $processes) {
        if ($process.ProcessId -eq $PID) { continue }
        if ($process.ExecutablePath -and $process.ExecutablePath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
        if ($process.CommandLine -and $process.CommandLine.Replace('\','/') -match $pattern) { return $true }
        # CMake may receive a relative build path; refuse rather than guessing its working directory.
        if ($process.Name -ieq 'cmake.exe' -and $process.CommandLine -match '--build\s+(?:"([^"]+)"|(\S+))') {
            $buildArgument = if ($Matches[1]) { $Matches[1] } else { $Matches[2] }
            if (![System.IO.Path]::IsPathRooted($buildArgument)) { return $true }
        }
        # Idle MSBuild worker nodes remain after a build and carry no project.
        if ($process.Name -match '^(cl|link|ninja|make|devenv|MSBuild)\.exe$' -and $process.CommandLine -notmatch '/nodemode:') {
            if (!$process.CommandLine -or $process.CommandLine -notmatch '(?:"|\s)[A-Za-z]:[/\\].*\.(?:sln|vcxproj|cpp|obj)') { return $true }
        }
    }
    return $false
}
$moves = @()
function AddMove([string]$source, [string]$destination) {
    $source = CheckedPath $source
    $destination = CheckedPath $destination
    if (Test-Path -LiteralPath $destination) { throw "Destination already exists: $destination" }
    $script:moves += [pscustomobject]@{ Source = $source; Destination = $destination; Status = 'Pending' }
}
foreach ($directory in (Get-ChildItem -LiteralPath $repoRoot -Directory | Where-Object Name -match '^build(?:[-_].*)?$')) {
    $source = CheckedPath $directory.FullName
    $cachePath = Join-Path $source 'CMakeCache.txt'
    if (!(Test-Path -LiteralPath $cachePath)) { Write-Host "Keep unknown build directory: $source"; continue }
    $cache = Get-Content -LiteralPath $cachePath -Raw
    $homeMatch = [regex]::Match($cache, '(?m)^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)\r?$')
    if (!$homeMatch.Success -or [System.IO.Path]::GetFullPath($homeMatch.Groups[1].Value.Trim()).TrimEnd('\','/') -ne $repoRoot) {
        Write-Host "Keep foreign CMake cache: $source"; continue
    }
    if (InUse $source) { Write-Host "Keep active build directory: $source"; continue }
    $referencePattern = [regex]::Escape($source.Replace('\','/')) + '(?=[/;\r\n]|$)'
    $references = @($caches | Where-Object {
        !$_.FullName.StartsWith($source + '\', [System.StringComparison]::OrdinalIgnoreCase) -and
        (Get-Content -LiteralPath $_.FullName -Raw).Replace('\','/') -match $referencePattern
    })
    if ($references.Count) { Write-Host "Keep referenced build directory: $source ($($references[0].FullName))"; continue }
    AddMove $source (Join-Path $repoRoot "Intermediate/LegacyBuilds/$($directory.Name)")
}
foreach ($name in @('Logs','Crashes','Dist','imgui.ini')) {
    $source = Join-Path $repoRoot $name
    if (!(Test-Path -LiteralPath $source)) { continue }
    if ($engineProcesses.Count -and $name -ne 'Dist') { Write-Host "Keep possible active runtime output: $source"; continue }
    if (InUse $source) {
        Write-Host "Keep running package: $source"; continue
    }
    AddMove $source (Join-Path $repoRoot "Saved/Archive/$name")
}
$docRoot = Join-Path $repoRoot 'docs'
if (Test-Path -LiteralPath (Join-Path $repoRoot '.git')) {
    foreach ($file in (Get-ChildItem -LiteralPath $docRoot -Filter '*.md' -File)) {
        if ($file.Name -eq 'README.md' -or $file.Name -eq 'AGENTS.md') { continue }
        & git -C $repoRoot ls-files --error-unmatch -- "docs/$($file.Name)" 2>$null | Out-Null
        if ($LASTEXITCODE -eq 0) { continue }
        AddMove $file.FullName (Join-Path $repoRoot "docs/Reports/Local/$($file.Name)")
    }
    $images = Join-Path $docRoot 'images'
    if ((Test-Path -LiteralPath $images) -and !(& git -C $repoRoot ls-files -- docs/images)) {
        AddMove $images (Join-Path $repoRoot 'docs/Reports/Local/images')
    }
    foreach ($name in @('渲染架构图.html','渲染架构图.png','TinyEngine引擎项目交互架构说明.docx')) {
        $localDocument = Join-Path $docRoot $name
        if (!(Test-Path -LiteralPath $localDocument)) { continue }
        & git -C $repoRoot ls-files --error-unmatch -- "docs/$name" 2>$null | Out-Null
        if ($LASTEXITCODE -ne 0) { AddMove $localDocument (Join-Path $repoRoot "docs/Architecture/Local/$name") }
    }
}
if (!$moves.Count) { Write-Host 'No safe local moves are needed.'; return }
$moves | Format-Table Source,Destination -AutoSize
if (!$Apply) { Write-Host 'Preview only. Use -Apply after verifying the new build.'; return }
$manifestDirectory = CheckedPath (Join-Path $repoRoot 'Saved/RepositoryMaintenance')
New-Item -ItemType Directory -Force -Path $manifestDirectory | Out-Null
$manifestPath = Join-Path $manifestDirectory ((Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.json')
$record = [pscustomobject]@{ RepositoryRoot = $repoRoot; Moves = $moves }
SaveManifest $manifestPath $record
foreach ($entry in $moves) {
    $null = CheckedPath $entry.Source
    $null = CheckedPath $entry.Destination
    if (Test-Path -LiteralPath $entry.Destination) { throw "Destination appeared during migration: $($entry.Destination)" }
    $entry.Status = 'Moving'
    SaveManifest $manifestPath $record
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $entry.Destination) | Out-Null
    Move-Item -LiteralPath $entry.Source -Destination $entry.Destination
    $entry.Status = 'Moved'
    SaveManifest $manifestPath $record
}
Write-Host "Archived $($moves.Count) paths. Rollback manifest: $manifestPath"
