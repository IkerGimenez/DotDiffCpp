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
- debug - Unomptimized, meant to be used while in development or debugging
- final - Optimized, meant for distribution
