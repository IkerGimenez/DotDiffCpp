# DotDiffCpp
C++ version of https://github.com/fiveisgreen/Dotdiff

# How to build DotDiffCpp
## Build Dependencies
- Visual Studio C++ Build Tools
    - Compatible with VS2019 Build Tools
    - Compatible with VS2022 Build Tools

Everything else is currently included in the repository

## Hardware Requirements
Mostly unknown at the moment, but at least a Windows PC compatible with DirectX 11 

## Build steps
For convenience, the repository includes the file `builTup.bat`. This batch file automatically sets up the necessary environment variables and calls tup (included in the repository) with the appropriate arguments to build what the user requests. 

From the help text for `buildTup.bat`:

```
Usage:
buildTup.bat [vsVersion] [config] - Builds the project targeting the specific configuration
Example: buildTup.bat 16 debug - This would build the project in a debug configuration using the Visual Studio 2019 tools
```

The VS Version has to use the version string, rather than the year (this may change to make it easier to work with).
- Version 16 is VS 2019
- Version 17 is VS 2022

There's two build configurations available:
- `debug` - Unomptimized, meant to be used while in development or debugging
- `final` - Optimized, meant for distribution

For convenience, the `all` argument can be passed to the batch script to have it build all configurations simultaneously

Once `buildTup.bat` has been called with the appropriate arguments, the built executable can be found under that configuration's specific folder. As an example, for a debug build, you can find the executable at `<REPO_ROOT>/build-debug/bin`. 

## Build scripts
The project is using tup as its build system. See https://gittup.org/tup/ for specific details on how it works.

The main files to look at are:
- `Tupfile` at the root directory which handles linking step after compiling
- `Tuprules.tup` at the root directory which sets up some common values for all builds
- `tup.config` at the root directory which sets up some build constants that can be set separately for each configuration
- `src/Tupfile` which handles compiling the main Win32 application
- `extern/dearimgui/Tupfile` which handles compiling the imgui files which are common to all imgui code
- `extern/dearimgui/backends/Tupfile` which handles compiling the imgui files relevant to Win32 and DirectX applications
- `build-debug/tup.config` and `build-final/tup.config` which handle setting build constants for their respective configuration

