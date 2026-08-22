@echo off
rem Configure + build Tactix, optionally run something afterwards.
rem
rem   scripts\build.bat                          build Release
rem   scripts\build.bat -r                       build, then run the GUI
rem   scripts\build.bat -t                       build, then run the tests
rem   scripts\build.bat -b --agents 10000 --json build, then run the benchmark
rem   scripts\build.bat -d -r                    Debug build (build-debug\), then run
rem
rem Anything after the flags is forwarded to the binary being run.
setlocal enabledelayedexpansion
pushd "%~dp0.."

set "CONFIG=Release"
set "DIR=build"
set "RUN="
set "REST="

:parse
if "%~1"=="" goto build
if /i "%~1"=="-d" (set "CONFIG=Debug" & set "DIR=build-debug" & shift & goto parse)
if /i "%~1"=="--debug" (set "CONFIG=Debug" & set "DIR=build-debug" & shift & goto parse)
if /i "%~1"=="-r" (set "RUN=tactix" & shift & goto parse)
if /i "%~1"=="--run" (set "RUN=tactix" & shift & goto parse)
if /i "%~1"=="-b" (set "RUN=tactix_bench" & shift & goto parse)
if /i "%~1"=="--bench" (set "RUN=tactix_bench" & shift & goto parse)
if /i "%~1"=="-t" (set "RUN=test" & shift & goto parse)
if /i "%~1"=="--test" (set "RUN=test" & shift & goto parse)
if /i "%~1"=="-h" goto help
if /i "%~1"=="--help" goto help

:rest
if "%~1"=="" goto build
set "REST=!REST! %1"
shift
goto rest

:build
cmake -B %DIR% -DCMAKE_BUILD_TYPE=%CONFIG% || goto fail
cmake --build %DIR% --config %CONFIG% --parallel || goto fail

if "%RUN%"=="" goto done
if "%RUN%"=="test" (
    ctest --test-dir %DIR% -C %CONFIG% --output-on-failure || goto fail
    goto done
)

rem Multi-config generators (Visual Studio) nest by config; Ninja/Makefiles don't.
set "BIN=%DIR%\%CONFIG%\%RUN%.exe"
if not exist "%BIN%" set "BIN=%DIR%\%RUN%.exe"
"%BIN%" !REST! || goto fail

:done
popd
exit /b 0

:help
echo Usage: scripts\build.bat [-d] [-r or -b or -t] [args forwarded to the binary]
echo   -d  Debug build (build-debug\)   -r  run the GUI
echo   -t  run the tests                -b  run the benchmark
popd
exit /b 0

:fail
popd
exit /b 1
