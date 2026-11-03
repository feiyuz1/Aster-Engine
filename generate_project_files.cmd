@echo off
setlocal
set "generate_result=0"

pushd "%~dp0"
if errorlevel 1 (
    echo ERROR: Cannot open the engine directory.
    set "generate_result=1"
    goto finish
)

where.exe cmake.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake was not found. Install CMake 3.21 or newer and add it to PATH.
    set "generate_result=1"
    goto cleanup
)

echo Generating Aster-Engine Visual Studio 2022 project files...
cmake.exe --preset editor
set "generate_result=%errorlevel%"
if not "%generate_result%"=="0" (
    echo.
    echo ERROR: Project generation failed. See the CMake output above.
    goto cleanup
)

if not exist "intermediate\Build\Editor\Aster-Engine.sln" (
    echo ERROR: CMake did not produce the expected Aster-Engine.sln.
    set "generate_result=1"
    goto cleanup
)

echo.
echo Project files generated successfully:
echo   "%CD%\intermediate\Build\Editor\Aster-Engine.sln"
echo Open the solution in Visual Studio to build and debug the engine.

:cleanup
popd
:finish
if /i not "%~1"=="--no-pause" pause
exit /b %generate_result%
