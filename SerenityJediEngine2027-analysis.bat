@REM Profiling build of SerenityJediEngine2027 with the analysis profiler (UseTracy=ON).
@REM Makes its own folder, build-analysis, so your normal build folder is not touched.
@echo off
for %%X in (cmake.exe) do (set FOUND=%%~$PATH:X)
if not defined FOUND (
	echo CMake was not found on your system.
	pause
	exit /b 1
)
if not exist build-analysis\nul (mkdir build-tracy)
pushd build-tracy
cmake -G "Visual Studio 18 2026" -A X64 -D UseTracy=ON -D CMAKE_INSTALL_PREFIX=../install ..
popd
echo.
echo Done. Open the solution in the build-tracy folder, pick Release, and Rebuild All.
echo Then copy everything from build-tracy\Release (including TracyClient.dll) into your Mods folder.
pause
