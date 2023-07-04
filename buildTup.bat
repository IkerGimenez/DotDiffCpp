:: This simple .bat file acts as a build helper for this engine.
:: The goal was to move away from complex and hard to parse tools and simplify the build process.
:: This can be adapted to a more modern build system easily, but is manageable for my own personal projects.

:: Build specific variables


@echo off

set vsversionsList=(vs2022)
set configsList=(debug final)

IF "%1"=="clean" (
   GOTO clean
) ELSE (
   GOTO compile
)

:clean
echo Cleaning temp folder...
DEL /F /Q /S ".\\temp" > NUL
echo Cleaning bin folder...
DEL /F /Q /S ".\\bin" > NUL
echo Done
GOTO end

:compile
IF [%1]==[] GOTO help
IF [%2]==[] GOTO help

IF NOT EXIST .\bin mkdir .\bin
IF NOT EXIST .\temp mkdir .\temp
IF NOT EXIST .\temp\obj mkdir .\temp\obj

FOR %%x in %vsversionsList% DO (
    if %%x==%1 GOTO FoundVsVersion
)
GOTO help

:FoundVsVersion
FOR %%x in %configsList% DO (
    if %%x==%2 GOTO FoundConfig
)
GOTO help

:FoundConfig
set vsFolder=
IF %1==vs2022 set vsFolder="2022"
IF [%vsFolder%]==[] GOTO help

:: Initialize development environment only once, otherwise we pollute the PATH variable on subsequent calls to the batch file
IF NOT DEFINED VCVARSALL_INIT (
 call "C:\\Program Files\\Microsoft Visual Studio\\%vsFolder%\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64
 set VCVARSALL_INIT=1
 )

 :: Set environment variables for Tup to use later on

set BuildConfigName=
set BuildConfigSuffix=

if "%3"=="debug" (
    set BuildConfigName=Debug
    set BuildConfigSuffix=_debug
)

:: Call tup to build
echo Building with tup
echo tup
.\tup\tup.exe --debug-run

GOTO :end

:help
echo Usage:
echo build.bat clean - Cleans the temp directory and the bin directory
echo build.bat [vs20XX] config] - Builds the project using the specified Visual Studio version and Configuration
echo Example: build.bat vs2022 debug - This would build the project using Visual Studio 2022 in a debug configuration
echo Available VS Versions:
FOR %%x in %vsversionsList% DO (
    echo %%x
)
echo Available Configs:
FOR %%x in %configsList% DO (
    echo %%x
)

:end
PAUSE

