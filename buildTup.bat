:: This simple .bat file acts as a build helper for this engine.
:: The goal was to move away from complex and hard to parse tools and simplify the build process.
:: This can be adapted to a more modern build system easily, but is manageable for my own personal projects.

:: Build specific variables

@echo off

set configsList=(debug final all)
set vsVersionsList=(16 17)

IF "%1"=="help" (
    GOTO :help
) ELSE IF "%1"=="clean" (
    GOTO :clean
) ELSE (
    GOTO :compile
)
GOTO :end

:clean
echo TODO(Iker^) - Update clean command
:: echo Cleaning temp folder...
:: DEL /F /Q /S ".\\temp" > NUL
:: echo Cleaning bin folder...
:: DEL /F /Q /S ".\\bin" > NUL
:: echo Done
EXIT /B

:compile
IF [%1]==[] GOTO help
IF [%2]==[] GOTO help

echo Compiling with args %1 %2 ...
IF NOT EXIST .\bin mkdir .\bin
IF NOT EXIST .\temp mkdir .\temp
IF NOT EXIST .\temp\obj mkdir .\temp\obj
IF EXIST .\.tup\tmp (
    DEL /F /Q /S ".\\.tup\\tmp"
    rmdir ".\\.tup\\tmp"
)

FOR %%x in %vsversionsList% DO (
    if %%x==%1 GOTO :FoundVsVersion
)
GOTO help

:FoundVsVersion
FOR %%x in %configsList% DO (
    if %%x==%2 GOTO :FoundConfig
)
GOTO help

:FoundConfig

:: setlocal EnableDelayedExpansion
set vsDir=
set /a maxVersion=%1 + 1
echo Verson string is [%1,%maxVersion%)
echo Searching for appropriate Visual Studio installation location...
for /f "usebackq tokens=*" %%i in (`vswhere -version [%1^,%maxVersion%^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set vsDir=%%i
)

echo Found %vsDir%

if exist %vsDir%\VC\Auxiliary\Build\vcvarsall.bat (
    echo Using %vsDir%\VC\Auxiliary\Build\vcvarsall.bat

    :: Initialize development environment only once, otherwise we pollute the PATH variable on subsequent calls to the batch file
    IF NOT DEFINED VCVARSALL_INIT (
        echo "%vsDir%"\VC\Auxiliary\Build\vcvarsall.bat x64
        call "%vsDir%\VC\Auxiliary\Build\vcvarsall.bat" x64
        set VCVARSALL_INIT=1
    )

    GOTO :Build
)
echo vcvarsall.bat not found. Unable to initialize Visual Studio dev console.
echo Check to make sure you have a valid installation of the C++ build tools.
echo Use the help command to print a valid list of versions.
GOTO :end

:: Initialize development environment only once, otherwise we pollute the PATH variable on subsequent calls to the batch file

:: Set environment variables for Tup to use later on

:: Call tup to build
:Build
echo Building with tup

IF ["%2"]==["all"] (
    echo .\tup\tup.exe
    .\tup\tup.exe
) ELSE (
    echo .\tup\tup.exe build-%2
    .\tup\tup.exe build-%2
)
GOTO :end

:help
echo Usage:
echo buildTup.bat [vsVersion] [config] - Builds the project targeting the specific configuration
echo Example: buildTup.bat 16 debug - This would build the project in a debug configuration using the Visual Studio 2019 tools
echo:
echo **************
echo Compatible VS Versions:
FOR %%x in %vsVersionsList% DO (
    echo %%x
)
echo:
echo VS Versions key:
echo Visual Studio 2019 == 16
echo Visual Studio 2022 == 17
echo **************
echo Available Configs:
FOR %%x in %configsList% DO (
    echo %%x
)
echo:
echo Configs key:
echo debug - Unoptimized for development and debugging
echo final - Optimized for distribution
echo all - Build all available configurations
echo:

:end
PAUSE

