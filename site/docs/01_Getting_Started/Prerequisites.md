# Prerequisites

> **Current version:** 1.6.0 (defined in `Version.h` and in
> `CMakeLists.txt project(WinDV VERSION 1.6.0)`).
> v1.6.0 adds `DVError.cpp/h` (DV error detection) and `sha256.c/h`
> (post-capture SHA-256 checksums).
> These files are included in both the CMake and VC6 builds without
> requiring any manual configuration.


## Compiler

**Visual C++ 6.0** (MSVC 6, `cl.exe` version 12.x).
The project files are `.dsp` / `.dsw` (Developer Studio Project /
Workspace), the native format for VC6.
No modern IDEs (Visual Studio 2005 and later) can open these files
natively; they require conversion.

## MFC

MFC version 6 is included with Visual C++ 6.0.
WinDV links MFC as a shared DLL (`/MD`, `/MDd`, `_AFXDLL` define).
The MFC 6 redistributable DLLs (`MFC42.DLL`, `MSVCRT.DLL`) ship with
Windows or the VC6 redistribution package.

## DirectShow BaseClasses

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

## Windows / Platform SDK

The WinDV `.dsp` file hard-codes include and library paths to:

```text
C:\Program Files\Microsoft SDK\Include\
C:\Program Files\Microsoft SDK\Lib\
C:\Program Files\Microsoft SDK\Samples\Multimedia\DirectShow\BaseClasses\Release\
```

If your SDK is installed elsewhere, see
[SDK path adjustment](Building.md#sdk-path-adjustment).

---

## Linked Libraries and Their Purposes

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

## Compiler Flags Reference

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

## Proven Build Environment

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

### Why DirectX 8.1 Specifically

The DirectShow BaseClasses supplied with the **DirectX 8.1 SDK** are
the latest version that ships pre-built VC6-compatible object files
and headers.
They use `DWORD`, `LONG`, and `int` exclusively -- types that are the
same width in both 32-bit and 64-bit contexts.

Later SDKs introduced `DWORD_PTR` and `LONG_PTR` as pointer-sized
integers.
These expand to 64-bit types when compiled for a 64-bit target, but
VC6 does not recognise them and will emit C2065 (undeclared identifier)
or C2146 (syntax error) across dozens of BaseClasses headers.

### SDKs Confirmed Incompatible with VC6

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

### Recommended Procedure

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
