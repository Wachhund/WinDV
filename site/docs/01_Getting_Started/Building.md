# Building WinDV

This document covers everything needed to compile WinDV from source:
build steps for CMake and VC6, NMAKE command-line build, SDK path
configuration, and known issues with non-original toolchains.

For prerequisites (compiler, MFC, SDKs, libraries), see
[Prerequisites](Prerequisites.md).
For a description of what the build produces and how it fits together, see
[Architecture](../02_Architecture/_index.md).

---

## Building with CMake (primary)

CMake is the primary and recommended build system for new development.
It targets MSVC 2017 or later and produces binaries for Windows 7 and
above (Win7+).
XP-compatible builds require the `v141_xp` toolset (Visual Studio 2017).

### Prerequisites

- **Visual Studio 2017 or 2022** with the C++ desktop workload.
- CMake 3.16 or later.
- For XP builds: VS 2017 with the optional **Windows XP support for C++**
  component (`v141_xp` toolset).

### Standard Build (Win7+)

```bat
cd WinDV
cmake -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

Output: `build\Release\WinDV.exe`

### XP-compatible Build

```bat
cmake -B build -G "Visual Studio 15 2017" -A Win32 -T v141_xp
cmake --build build --config Release
```

### Static MFC (no MFC DLL dependency)

```bat
cmake -B build -G "Visual Studio 17 2022" -A Win32 -DSTATIC_MFC=ON
cmake --build build --config Release
```

### Notes on Mixed C/C++ Sources

The CMake project file declares `LANGUAGES C CXX` to support `sha256.c`,
which is a pure C file:

```cmake
project(WinDV VERSION 1.6.0 LANGUAGES C CXX)
```

`sha256.c` is compiled as C89 and is explicitly excluded from the
precompiled header mechanism:

```cmake
set_source_files_properties(sha256.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
```

All other `.cpp` files use `StdAfx.h` as the precompiled header.

### DirectShow BaseClasses (vendored)

The BaseClasses are vendored in the `baseclasses/` subdirectory and built
automatically as a CMake sub-project via `add_subdirectory(baseclasses)`.
No manual BaseClasses build step is needed when using CMake.

---

## Building from the Visual C++ 6.0 IDE

The VC6 project files (`WinDV.dsp` / `WinDV.dsw`) are retained for
compatibility with VC6-based XP environments.
They include `DVError.cpp` and `sha256.c` as source files.
`sha256.c` has the `/Y-` flag (`# SUBTRACT CPP /YX /Yc /Yu`) to disable
precompiled header processing for that file.

1. Open `WinDV\WinDV.dsw` in the Visual C++ 6.0 IDE.
2. Select **Build > Set Active Configuration** and choose one of:
   - `WinDV - Win32 Release` -- optimised build, links `strmbase.lib`.
   - `WinDV - Win32 Debug` -- debug info, no optimisation,
     links `strmbasd.lib`.
3. Press **F7** (Build) or choose **Build > Build WinDV.exe**.

Output location:

| Configuration | Output directory |
| --- | --- |
| Release | `WinDV\WinDV\Release\WinDV.exe` |
| Debug | `WinDV\WinDV\Debug\WinDV.exe` |

The build also produces `WinDV.pdb` (debug symbols) in the Debug
configuration.

---

## Building from the Command Line (NMAKE)

NMAKE requires a makefile exported from the IDE.
Export it once from **Project > Export Makefile**.
This produces `WinDV\WinDV\WinDV.mak`.

From the `WinDV\WinDV\` directory, run:

```bat
:: Release build
NMAKE /f "WinDV.mak" CFG="WinDV - Win32 Release"

:: Debug build
NMAKE /f "WinDV.mak" CFG="WinDV - Win32 Debug"
```

The NMAKE environment must have the VC6 toolchain on `PATH`.
The standard way to set this up is to run the VC6 `vcvars32.bat` first:

```bat
call "C:\Program Files\Microsoft Visual Studio\VC98\Bin\vcvars32.bat"
```

---

## SDK Path Adjustment

If the Platform SDK or DirectShow BaseClasses are not installed at
`C:\Program Files\Microsoft SDK\`, update the paths in one of two ways:

### Option A -- Edit the .dsp File

Open `WinDV\WinDV\WinDV.dsp` in a text editor and change the `/I` and
`/libpath:` arguments in the compiler and linker settings sections.

Search for occurrences of `Microsoft SDK` and replace the path prefix.

### Option B -- Set Environment Variables Before NMAKE

```bat
set INCLUDE=C:\YourSDK\Include;C:\YourSDK\Samples\Multimedia\DirectShow\BaseClasses;%INCLUDE%
set LIB=C:\YourSDK\Lib;C:\YourSDK\Samples\Multimedia\DirectShow\BaseClasses\Release;%LIB%
NMAKE /f "WinDV.mak" CFG="WinDV - Win32 Release"
```

For the Debug configuration, point `LIB` at the `BaseClasses\Debug\`
directory instead to pick up `strmbasd.lib`.

---

## Known Issues with Modern Toolchains

### Visual Studio 2005 and Later

The `.dsp` / `.dsw` project format is not supported.
Visual Studio 2005 can convert them to `.vcproj` / `.sln`, but the
resulting project requires manual correction of:

- Include and library paths (the old paths use backslash-only and will
  need updating regardless of SDK installation).
- The `/GX` flag, which was renamed to `/EHsc` in later MSVC versions.
- Deprecated functions: `strtok` (use `strtok_s`), `strftime`
  (no change needed for VC6 behaviour).
- MFC version: WinDV targets MFC 6; later Visual Studio versions
  ship MFC 9 and later, which are largely compatible but may produce
  warnings about deprecated MFC features.

### 64-bit Compilation

WinDV is a 32-bit application (`Win32` target).
It uses `int` where the code assumes 32-bit width and casts between
`long` and `REFERENCE_TIME` (`__int64`) at several points.
A 64-bit compilation (`x64` target) would require auditing all such casts.

### DirectShow BaseClasses Availability

The DirectShow BaseClasses were removed from the Windows SDK after
Windows SDK 7.1.
For modern SDK versions, obtain the BaseClasses from one of:

- The Windows SDK 7.1 samples package.
- The DirectShow BaseClasses repository on GitHub
  (search for `Windows-classic-samples` / DirectShow).

The BaseClasses must still be compiled with a compatible compiler
(VC6 or a version that produces compatible object files).

### Windows 10/11 FireWire Support

The Windows inbox `1394ohci.sys` driver on Windows 10 and 11 does not
expose the legacy `msdv.sys` DirectShow capture interface on all hardware.
On some systems it is necessary to replace `1394ohci.sys` with the legacy
`ohci1394.sys` driver or use a third-party OHCI driver that supports the
DV streaming interface that WinDV relies on.
This is a driver-level issue unrelated to the build.
