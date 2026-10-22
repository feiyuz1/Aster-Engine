param(
    [string]$MaintenanceScript = (Join-Path $PSScriptRoot '../maintenance/organize_repository.ps1'),
    [ValidateSet('All','ActiveProcess','InterruptedMove')][string]$Case = 'All'
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $MaintenanceScript)) { throw 'Maintenance script is missing' }
$testBase = Join-Path ([System.IO.Path]::GetTempPath()) ('TinyEngineLayout-' + [guid]::NewGuid().ToString('N'))
$testRoot = Join-Path $testBase 'Engine'
$outside = Join-Path $testBase 'Other checkout'
New-Item -ItemType Directory -Path $testRoot,$outside -Force | Out-Null
Set-Content (Join-Path $testRoot 'CMakeLists.txt') 'project(Aster-Engine)'
Set-Content (Join-Path $testRoot 'Aster-Engine.tinyproject') 'project.name=Test'
function Cache([string]$name, [string]$owner) {
    $dir = Join-Path $testRoot $name
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    Set-Content (Join-Path $dir 'CMakeCache.txt') ('CMAKE_HOME_DIRECTORY:INTERNAL=' + $owner.Replace('\','/'))
}
function Assert([bool]$condition, [string]$message) { if (!$condition) { throw $message } }
function MustFail([scriptblock]$action, [string]$message) {
    $failed = $false
    try { & $action } catch { $failed = $true }
    Assert $failed $message
}
if ($Case -eq 'All') {
MustFail { & $MaintenanceScript -RepositoryRoot $outside -Apply } 'Wrong root accepted'
Cache 'build' $testRoot
& $MaintenanceScript -RepositoryRoot $testRoot -DependencyRoots @($testRoot,$outside)
Assert (Test-Path (Join-Path $testRoot 'build/CMakeCache.txt')) 'Preview moved a directory'
New-Item -ItemType Directory -Path (Join-Path $testRoot 'intermediate/LegacyBuilds/build') -Force | Out-Null
MustFail { & $MaintenanceScript -RepositoryRoot $testRoot -Apply -DependencyRoots @($testRoot,$outside) } 'Destination collision accepted'
Remove-Item -LiteralPath (Join-Path $testRoot 'intermediate/LegacyBuilds/build')
Cache 'build-foreign' $outside
Set-Content (Join-Path $outside 'CMakeCache.txt') ('FETCHCONTENT_SOURCE_DIR_SDL2:PATH=' + $testRoot.Replace('\','/') + '/build/_deps/sdl2-src')
& $MaintenanceScript -RepositoryRoot $testRoot -Apply -DependencyRoots @($testRoot,$outside)
Assert (Test-Path (Join-Path $testRoot 'build/CMakeCache.txt')) 'Moved externally referenced cache'
Assert (Test-Path (Join-Path $testRoot 'build-foreign/CMakeCache.txt')) 'Moved foreign cache'
Set-Content (Join-Path $outside 'CMakeCache.txt') 'NO_EXTERNAL_REFERENCE=1'
& $MaintenanceScript -RepositoryRoot $testRoot -Apply -DependencyRoots @($testRoot,$outside)
Assert (!(Test-Path (Join-Path $testRoot 'build'))) 'Safe cache was not archived'
$manifest = Get-ChildItem (Join-Path $testRoot 'saved/RepositoryMaintenance/*.json') | Sort-Object Name | Select-Object -Last 1
& $MaintenanceScript -RepositoryRoot $testRoot -RollbackManifest $manifest.FullName
Assert (Test-Path (Join-Path $testRoot 'build/CMakeCache.txt')) 'Rollback did not restore original cache'
$badManifest = Join-Path $testRoot 'saved/bad-manifest.json'
@{ RepositoryRoot = $testRoot; Moves = @(@{ Source = $outside; Destination = (Join-Path $testRoot 'intermediate/LegacyBuilds/build'); Status = 'Moved' }) } |
    ConvertTo-Json -Depth 6 | Set-Content $badManifest
MustFail { & $MaintenanceScript -RepositoryRoot $testRoot -RollbackManifest $badManifest } 'Out-of-root rollback accepted'
Assert (Test-Path $outside) 'External directory changed'
}
if ($Case -eq 'All' -or $Case -eq 'ActiveProcess') {
    Cache 'build-active' $testRoot
    $activePath = Join-Path $testRoot 'build-active'
    $helper = Join-Path $testBase 'ActiveBuild.ps1'
    Set-Content $helper 'param([string]$BuildDirectory); Start-Sleep -Seconds 60'
    $helperProcess = Start-Process -FilePath (Get-Process -Id $PID).Path -WindowStyle Hidden -PassThru -ArgumentList @(
        '-NoProfile','-File',('"' + $helper + '"'),'-BuildDirectory',('"' + $activePath + '"'))
    try {
        & $MaintenanceScript -RepositoryRoot $testRoot -Apply -DependencyRoots @($testRoot,$outside)
        Assert (Test-Path (Join-Path $activePath 'CMakeCache.txt')) 'Archived a cache referenced by an active non-engine process'
    } finally { Stop-Process -Id $helperProcess.Id -ErrorAction SilentlyContinue }
}
if ($Case -eq 'All' -or $Case -eq 'InterruptedMove') {
    Cache 'build-recovery' $testRoot
    & $MaintenanceScript -RepositoryRoot $testRoot -Apply -DependencyRoots @($testRoot,$outside)
    $manifest = Get-ChildItem (Join-Path $testRoot 'saved/RepositoryMaintenance/*.json') | Sort-Object Name | Select-Object -Last 1
    $record = Get-Content $manifest.FullName -Raw | ConvertFrom-Json
    foreach ($entry in $record.Moves) { $entry.Status = 'Pending' }
    $record | ConvertTo-Json -Depth 6 | Set-Content $manifest.FullName
    & $MaintenanceScript -RepositoryRoot $testRoot -RollbackManifest $manifest.FullName
    Assert (Test-Path (Join-Path $testRoot 'build-recovery/CMakeCache.txt')) 'Interrupted move was invisible to rollback'
    foreach ($entry in $record.Moves) { $entry.Status = 'Moved' }
    $record | ConvertTo-Json -Depth 6 | Set-Content $manifest.FullName
    & $MaintenanceScript -RepositoryRoot $testRoot -RollbackManifest $manifest.FullName
    Assert (Test-Path (Join-Path $testRoot 'build-recovery/CMakeCache.txt')) 'Interrupted rollback did not recover'
}
Write-Host "PASS: preview, root, collision, owner, dependency and rollback guards ($testBase)"
