# Building WinDV

This document covers everything needed to compile WinDV from source:
prerequisites, IDE build steps, NMAKE command-line build, SDK path
configuration, and known issues with non-original toolchains.

For a description of what the build produces and how it fits together, see
[architecture.md](architecture.md).

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Building with CMake (primary)](#2-building-with-cmake-primary)
3. [Building from the Visual C++ 6.0 IDE](#3-building-from-the-visual-c-60-ide)
4. [Command-line build (NMAKE)](#4-building-from-the-command-line-nmake)
5. [SDK path adjustment](#5-sdk-path-adjustment)
6. [Linked libraries and their purposes](#6-linked-libraries-and-their-purposes)
7. [Compiler flags reference](#7-compiler-flags-reference)
8. [Known issues with modern toolchains](#8-known-issues-with-modern-toolchains)
9. [Proven build environment](#9-proven-build-environment)

---

## 1. Prerequisites

> **Current version:** 1.6.0 (defined in `Version.h` and in
> `CMakeLists.txt project(WinDV VERSION 1.6.0)`).
> v1.6.0 adds `DVError.cpp/h` (DV error detection) and `sha256.c/h`
> (post-capture SHA-256 checksums).
> These files are included in both the CMake and VC6 builds without
> requiring any manual configuration.


### Compiler

**Visual C++ 6.0** (MSVC 6, `cl.exe` version 12.x).
The project files are `.dsp` / `.dsw` (Developer Studio Project /
Workspace), the native format for VC6.
No modern IDEs (Visual Studio 2005 and later) can open these files
natively; they require conversion.

### MFC

MFC version 6 is included with Visual C++ 6.0.
WinDV links MFC as a shared DLL (`/MD`, `/MDd`, `_AFXDLL` define).
The MFC 6 redistributable DLLs (`MFC42.DLL`, `MSVCRT.DLL`) ship with
Windows or the VC6 redistribution package.

### DirectShow BaseClasses

The DirectShow BaseClasses are the static library counterpart to the
DirectShow runtime.
They provide the `CBaseFilter`, `CBaseInputPin`, `CBaseOutputPin`,
`COutputQueue`, and related classes that WinDV's custom filters inherit
from.

Location in a typical Platform SDK installation:

```text
C:\Program Files\Microsoft SDK\Samples\Multimedia\DirectShow\BaseClasses\
```

Build the BaseClasses **before** building WinDV:

1. Open `BaseClasses\baseclasses.dsw` in VC6.
2. Build both `Release` and `Debug` configurations.

This produces:

- `BaseClasses\Release\strmbase.lib`
- `BaseClasses\Debug\strmbasd.lib`

### Windows / Platform SDK

The WinDV `.dsp` file hard-codes include and library paths to:

```text
C:\Program Files\Microsoft SDK\Include\
C:\Program Files\Microsoft SDK\Lib\
C:\Program Files\Microsoft SDK\Samples\Multimedia\DirectShow\BaseClasses\Release\
```

If your SDK is installed elsewhere, see
[SDK path adjustment](#4-sdk-path-adjustment).

---

## 2. Building with CMake (primary)

CMake is the primary and recommended build system for new development.
It targets MSVC 2017 or later and produces binaries for Windows 7 and
above (Win7+).
XP-compatible builds require the `v141_xp` toolset (Visual Studio 2017).

### Prerequisites

- **Visual Studio 2017 or 2022** with the C++ desktop workload.
- CMake 3.16 or later.
- For XP builds: VS 2017 with the optional **Windows XP support for C++**
  component (`v141_xp` toolset).

### Standard build (Win7+)

```bat
cd WinDV
cmake -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

Output: `build\Release\WinDV.exe`

### XP-compatible build

```bat
cmake -B build -G "Visual Studio 15 2017" -A Win32 -T v141_xp
cmake --build build --config Release
```

### Static MFC (no MFC DLL dependency)

```bat
cmake -B build -G "Visual Studio 17 2022" -A Win32 -DSTATIC_MFC=ON
cmake --build build --config Release
```

### Notes on mixed C/C++ sources

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

## 3. Building from the Visual C++ 6.0 IDE

The VC6 project files (`WinDV.dsp` / `WinDV.dsw`) are retained for
compatibility with VC6-based XP environments.
They include `DVError.cpp` and `sha256.c` as source files.
`sha256.c` has the `/Y-` flag (`# SUBTRACT CPP /YX /Yc /Yu`) to disable
precompiled header processing for that file.

1. Open `WinDV\WinDV.dsw` in the Visual C++ 6.0 IDE.
2. Select **Build > Set Active Configuration** and choose one of:
   - `WinDV - Win32 Release` — optimised build, links `strmbase.lib`.
   - `WinDV - Win32 Debug` — debug info, no optimisation,
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

## 4. Building from the command line (NMAKE)

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

## 5. SDK path adjustment

If the Platform SDK or DirectShow BaseClasses are not installed at
`C:\Program Files\Microsoft SDK\`, update the paths in one of two ways:

### Option A — edit the .dsp file

Open `WinDV\WinDV\WinDV.dsp` in a text editor and change the `/I` and
`/libpath:` arguments in the compiler and linker settings sections.

Search for occurrences of `Microsoft SDK` and replace the path prefix.

### Option B — set environment variables before NMAKE

```bat
set INCLUDE=C:\YourSDK\Include;C:\YourSDK\Samples\Multimedia\DirectShow\BaseClasses;%INCLUDE%
set LIB=C:\YourSDK\Lib;C:\YourSDK\Samples\Multimedia\DirectShow\BaseClasses\Release;%LIB%
NMAKE /f "WinDV.mak" CFG="WinDV - Win32 Release"
```

For the Debug configuration, point `LIB` at the `BaseClasses\Debug\`
directory instead to pick up `strmbasd.lib`.

---

## 6. Linked libraries and their purposes

| Library | Configuration | Purpose |
| --- | --- | --- |
| `strmbase.lib` | Release | DirectShow BaseClasses (CBaseFilter, etc.) |
| `strmbasd.lib` | Debug | DirectShow BaseClasses (debug build) |
| `quartz.lib` | Both | DirectShow runtime (`IGraphBuilder`, etc.) |
| `winmm.lib` | Both | Windows multimedia (used internally by DirectShow) |
| `ole32.lib` | Both | COM (`CoInitializeEx`, `CoCreateInstance`, `IUnknown`) |
| `olepro32.lib` | Both | OLE automation support |
| `oleaut32.lib` | Both | OLE automation (`VARIANT`, `BSTR`, `SysFreeString`) |
| `uuid.lib` | Both | CLSID / IID GUIDs for all COM interfaces |
| `advapi32.lib` | Both | Registry API (`RegOpenKey`, etc. via MFC wrappers) |
| `version.lib` | Both | Version resource API |
| `largeint.lib` | Both | 64-bit integer helpers (`REFERENCE_TIME` arithmetic) |
| `comctl32.lib` | Both | Windows Common Controls (tab control, list box) |
| `kernel32.lib` | Both | Core Win32 (threads, memory, files) |
| `user32.lib` | Both | Window management, messages, timers |
| `gdi32.lib` | Both | GDI drawing functions |
| `msvcrt.lib` | Release | C runtime (Release) |
| `msvcrtd.lib` | Debug | C runtime (Debug) |

`largeint.lib` provides 64-bit integer helper routines needed on platforms
where the compiler does not natively support 64-bit arithmetic.
VC6 targeting Windows 98/NT uses this for `REFERENCE_TIME` (which is
`__int64`, 100-nanosecond units).

---

## 7. Compiler flags reference

| Flag | Build | Meaning |
| --- | --- | --- |
| `/MD` | Release | Link MFC and C runtime as shared DLL |
| `/MDd` | Debug | Link MFC and C runtime as shared DLL (debug) |
| `/W3` | Both | Warning level 3 |
| `/GX` | Both | Enable C++ exceptions (`/EHsc` in modern MSVC) |
| `/O2` | Release | Optimise for speed |
| `/ZI` | Debug | Edit-and-continue debug information |
| `/Od` | Debug | Disable optimisation |
| `_WIN32_DCOM` | Both | Enable DCOM-extended COM initialisation |
| `_AFXDLL` | Both | Link MFC as shared DLL (set implicitly by `/MD`) |
| `_MBCS` | Both | Multi-byte character set (ANSI, not Unicode) |
| `WIN32` | Both | Standard Win32 define |
| `NDEBUG` | Release | Disable assertions |
| `_DEBUG` | Debug | Enable assertions and debug heap |

---

## 8. Known issues with modern toolchains

### Visual Studio 2005 and later

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

### 64-bit compilation

WinDV is a 32-bit application (`Win32` target).
It uses `int` where the code assumes 32-bit width and casts between
`long` and `REFERENCE_TIME` (`__int64`) at several points.
A 64-bit compilation (`x64` target) would require auditing all such casts.

### DirectShow BaseClasses availability

The DirectShow BaseClasses were removed from the Windows SDK after
Windows SDK 7.1.
For modern SDK versions, obtain the BaseClasses from one of:

- The Windows SDK 7.1 samples package.
- The DirectShow BaseClasses repository on GitHub
  (search for `Windows-classic-samples` / DirectShow).

The BaseClasses must still be compiled with a compatible compiler
(VC6 or a version that produces compatible object files).

### Windows 10/11 FireWire support

The Windows inbox `1394ohci.sys` driver on Windows 10 and 11 does not
expose the legacy `msdv.sys` DirectShow capture interface on all hardware.
On some systems it is necessary to replace `1394ohci.sys` with the legacy
`ohci1394.sys` driver or use a third-party OHCI driver that supports the
DV streaming interface that WinDV relies on.
This is a driver-level issue unrelated to the build.

---

## 9. Proven build environment

The VC6 configuration below has been verified to produce a working
`WinDV.exe` (Release and Debug) with all features through v1.6.0.
For CMake builds with MSVC 2017 or 2022, no special environment is needed
beyond the standard Visual Studio C++ desktop workload.

| Component | Version / Source |
| --- | --- |
| Host OS | Windows XP (VM) |
| Compiler | Visual C++ 6.0 (MSVC 12.x) |
| DirectX SDK | **DirectX 8.1 SDK** (archived at archive.org) |
| BaseClasses | Bundled with the DirectX 8.1 SDK |

### Why DirectX 8.1 specifically

The DirectShow BaseClasses supplied with the **DirectX 8.1 SDK** are
the latest version that ships pre-built VC6-compatible object files
and headers.
They use `DWORD`, `LONG`, and `int` exclusively — types that are the
same width in both 32-bit and 64-bit contexts.

Later SDKs introduced `DWORD_PTR` and `LONG_PTR` as pointer-sized
integers.
These expand to 64-bit types when compiled for a 64-bit target, but
VC6 does not recognise them and will emit C2065 (undeclared identifier)
or C2146 (syntax error) across dozens of BaseClasses headers.

### SDKs confirmed incompatible with VC6

| SDK | Reason incompatible |
| --- | --- |
| DirectX 9.0+ SDK (any version) | BaseClasses use `DWORD_PTR`, `LONG_PTR` |
| Windows SDK 7.1 BaseClasses | Same `DWORD_PTR`/`LONG_PTR` issue |
| DirectX June 2010 SDK Extras | Same `DWORD_PTR`/`LONG_PTR` issue |

> **Note:** If you must use a later SDK for other reasons,
> you may be able to substitute the BaseClasses source from the
> DirectX 8.1 SDK into the later SDK's directory tree.
> However, mixing SDK headers and BaseClasses from different SDK
> generations is not tested and may introduce subtle ABI mismatches.

### Recommended procedure

1. Install Visual C++ 6.0 on a Windows XP host (physical or VM).
2. Obtain the DirectX 8.1 SDK from a trusted archive
   (e.g. the Internet Archive).
3. Install the DirectX 8.1 SDK; note the installation path.
4. Build the BaseClasses from
   `<DX81SDK>\Samples\Multimedia\DirectShow\BaseClasses\`
   for both Release and Debug.
5. Update the WinDV `.dsp` include and library paths to point at the
   DirectX 8.1 SDK installation if it differs from
   `C:\Program Files\Microsoft SDK\`.
6. Build WinDV.
