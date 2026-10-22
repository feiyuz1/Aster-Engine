param(
    [string]$BuildDirectory = "intermediate/Build/Editor",
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$build = Join-Path $root $BuildDirectory
cmake --build $build --config $Configuration --target tinyengine_tests tinyengine_commandlet package_game -- /m:1 /nodeReuse:false
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
ctest --test-dir $build -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Regression tests failed with exit code $LASTEXITCODE" }
$commandlet = Join-Path $build "$Configuration\tinyengine_commandlet.exe"
& $commandlet ValidateAssets (Join-Path $root "assets")
if ($LASTEXITCODE -ne 0) { throw "Asset validation failed with exit code $LASTEXITCODE" }
& $commandlet ValidateScenes (Join-Path $root "assets")
if ($LASTEXITCODE -ne 0) { throw "Scene validation failed with exit code $LASTEXITCODE" }
$package = Join-Path $build "Package\Aster-Engine"
$executable = Join-Path $package "Aster-Engine.exe"
$project = Join-Path $package "Aster-Engine.tinyproject"
& $executable $project --frames=3
if ($LASTEXITCODE -ne 0) { throw "Packaged runtime smoke test failed with exit code $LASTEXITCODE" }
Write-Host "Aster-Engine release smoke test passed: $package"
