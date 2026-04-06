# Coding Standards

Coding standards for WinDV -- a Win32/MFC/DirectShow desktop application
from 2002, maintained with dual VC6/CMake builds. Balances modern best
practices with hard C++98/VC6 compatibility constraints.

Derived from the C++ Core Guidelines, adapted to the project's legacy
constraints.

---

## Hard Constraints

These override all other guidelines:

| Constraint | Reason |
| --- | --- |
| **C++98 only** | VC6 (cl.exe 12.x) is a supported compiler |
| **No C++11+ features** | No `auto`, `nullptr`, `enum class`, `constexpr`, lambdas, range-for, smart pointers, `override`, `std::string_view`, uniform init `{}` |
| **`_MBCS` character set** | Unicode migration (WDV-2) is planned but not done |
| **Win32 API XP SP3 minimum** | Newer APIs only via `GetProcAddress()` with NULL fallback |
| **Precompiled header** | `StdAfx.h` must be first `#include` in every `.cpp` |
| **Pure C modules** | `.c` files must NOT include `StdAfx.h`; use `extern "C"` in headers; C89 only |

---

## Philosophy

The C++ Core Guidelines principles still apply -- adapted to C++98:

1. **RAII where possible** -- Use MFC types (`CString`, `CArray`, `CEvent`) and destructor cleanup. No `std::unique_ptr`, but `delete` in destructors is acceptable.
2. **Immutability by default** -- Use `const` on parameters, local variables, and member functions wherever possible. No `constexpr` available; use `#define` or `static const`.
3. **Type safety** -- Prefer typed structs over bare `DWORD`/`int` bags. Use explicit casts over implicit narrowing.
4. **Express intent** -- Names and comments should communicate purpose. MFC Hungarian notation (`m_`, `dw`, `sz`) is the project convention -- follow it.
5. **Minimize complexity** -- Keep functions short. One function, one job.
6. **Value semantics** -- Prefer stack objects. Heap allocation only when necessary (DirectShow COM objects, variable-size buffers).

---

## Naming Conventions

WinDV uses **MFC/Win32 Hungarian notation** -- this is the project standard:

```cpp
/* Member variables: m_ prefix */
DWORD m_dwFrameCount;
CString m_captureFilename;
bool m_enableSHA256;

/* Local variables: type prefix or plain descriptive */
DWORD dwRead;
int seqCount;
char szHash[65];

/* Constants: ALL_CAPS for #define, mixed for static const */
#define BLOCKS_PER_SEQ  150
static const int kMaxRetries = 3;

/* Functions: PascalCase (MFC convention) or camelCase */
void GetMediaType(CMediaType *type);
int GetDVRecordingTime(BYTE *buf, int len);

/* Structs: PascalCase */
struct FrameErrorInfo { ... };
struct CaptureStats { ... };

/* Classes: C prefix (MFC convention) */
class CDV : public CStatic { ... };
class CCaptureCfg : public CPropertyPage { ... };
```

---

## File Organization

### Headers (.h)

```cpp
/* Include guard -- use #ifndef/#define, not #pragma once alone (VC6 compat).
 * #pragma once is acceptable as a supplement for MSVC optimization. */
#ifndef DVERROR_H
#define DVERROR_H

/* For headers that use Win32 types (DWORD, BYTE, BOOL) but might be included
 * outside the PCH chain: rely on StdAfx.h being included first (project convention).
 * Only add #include <windows.h> if the header is designed for standalone use. */

struct FrameErrorInfo {
    DWORD dwVideoErrorBlocks;
    DWORD dwAudioErrorBlocks;
};

FrameErrorInfo AnalyzeDVFrame(const BYTE *data, int len);

#endif /* DVERROR_H */
```

### Source Files (.cpp)

```cpp
#include "stdafx.h"    /* MUST be first -- precompiled header */

#include "MyModule.h"  /* own header */
#include "DV.h"        /* project headers */
#include <stdio.h>     /* system headers last */
```

### Pure C Modules (.c)

```c
/* NO #include "stdafx.h" -- pure C, no MFC */
#include <string.h>
#include "sha256.h"

/* C89 only: no // comments, no mixed declarations,
 * no uint32_t (use unsigned int), no size_t in public API */
```

### Pure C Headers (.h with extern "C")

```c
#ifndef SHA256_H
#define SHA256_H

#ifdef __cplusplus
extern "C" {
#endif

void sha256_init(SHA256_CTX *ctx);

#ifdef __cplusplus
}
#endif

#endif /* SHA256_H */
```

---

## Resource Management

### MFC Types as RAII

MFC types handle their own cleanup -- prefer them over raw Win32 handles:

| Resource | Use | Avoid |
| --- | --- | --- |
| Strings | `CString` | `char*` with `malloc`/`free` |
| Arrays | `CArray<T,T&>` | `T*` with `new[]`/`delete[]` |
| Sync | `CAutoLock lock(&m_cs)` | Manual `EnterCriticalSection`/`Leave` |
| Events | `CEvent` | Raw `CreateEvent`/`CloseHandle` |
| COM | `CHECK_HR()` macro | Unchecked `HRESULT` |

### Manual Cleanup Pattern

When RAII is not available (Win32 handles, COM interfaces):

```cpp
HANDLE hFile = CreateFile(...);
if (hFile == INVALID_HANDLE_VALUE) return FALSE;

/* ... use hFile ... */

CloseHandle(hFile);  /* cleanup before every return path */
```

### Newer Win32 APIs -- GetProcAddress Pattern

```cpp
/* Load dynamically so the binary runs on older Windows versions.
 * Pattern established in WinDV.cpp for SetProcessDPIAware. */
typedef BOOL (WINAPI *PFN_SetProcessDPIAware)(void);
HMODULE hUser32 = GetModuleHandle("user32.dll");
if (hUser32) {
    PFN_SetProcessDPIAware pfn = (PFN_SetProcessDPIAware)
        GetProcAddress(hUser32, "SetProcessDPIAware");
    if (pfn) pfn();
}
```

For repeated use, wrap in an `__inline` helper in `StdAfx.h` (see `SafeAttachConsole()`).

---

## Error Handling

WinDV uses MFC exception handling, not C++ exceptions:

```cpp
/* Throwing: use CDShowException via helper */
ThrowDShowException(CDShowException::error, "Graph build failed");

/* Catching: MFC TRY/CATCH_ALL pattern */
TRY {
    BuildCapturing(vsrc);
}
CATCH_ALL(e) {
    Exception2Status(e);
}
END_CATCH_ALL

/* HRESULT checking: use the CHECK_HR macro */
CHECK_HR(pGraph->AddFilter(pFilter, L"MyFilter"));
```

Do NOT use C++ `try`/`catch`/`throw` -- MFC exceptions are heap-allocated with `b_AutoDelete`.

---

## Threading

```cpp
/* Create threads via MFC (suspended, then resumed) */
m_thread = AfxBeginThread(::CapturingThread, this,
    THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
m_thread->ResumeThread();

/* Synchronization: always RAII via CAutoLock */
{
    CAutoLock lock(&m_cs);
    m_errorStats = newStats;  /* protected access */
}

/* UI notification from worker threads: PostMessage only, never SendMessage */
GetParent()->PostMessage(WM_DV_CHECK_COMPLETE, (WPARAM)result, 0);
```

---

## Expressions and Statements

### Initialization

```cpp
/* Always initialize -- uninitialized variables are undefined behavior */
DWORD dwCount = 0;
char szHash[65] = "";
FrameErrorInfo info;
memset(&info, 0, sizeof(info));  /* acceptable for POD structs */
```

### Casts

```cpp
/* Prefer C++ casts in new code (VC6 supports them) */
UINT nFrames = (UINT)rawDelta;       /* C-style: acceptable in existing patterns */
const BYTE *block = data + offset;    /* pointer arithmetic: no cast needed */

/* Never cast away const */
```

### Signed/Unsigned

```cpp
/* Avoid mixing signed and unsigned in comparisons (C4018) */
UINT nFrames = 0;                     /* use UINT if comparing with UINT members */
long rawDelta = a - b;                /* signed arithmetic in temp */
if (rawDelta < 0) rawDelta = -rawDelta;
UINT delta = (UINT)rawDelta;          /* cast after ensuring non-negative */
```

### Magic Numbers

```cpp
/* Name your constants */
#define BLOCKS_PER_SEQ    150
#define BYTES_PER_BLOCK    80
#define SEQ_SIZE          (BLOCKS_PER_SEQ * BYTES_PER_BLOCK)

/* Avoid */
offset = i * 150 * 80 + b * 80 + 3;  /* what are 150, 80, 3? */
```

---

## Console Output from GUI App

WinDV is a GUI application (`/SUBSYSTEM:WINDOWS`) that also supports CLI commands:

```cpp
/* 1. Try inherited stdout first (works with cmd redirection and CI) */
HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

/* 2. Fall back to parent console for interactive use */
if (hOut == NULL || hOut == INVALID_HANDLE_VALUE) {
    SafeAttachConsole();  /* GetProcAddress wrapper in StdAfx.h */
    hOut = GetStdHandle(STD_OUTPUT_HANDLE);
}

/* 3. Write via WriteFile (not printf -- stdout may not be connected) */
if (hOut != NULL && hOut != INVALID_HANDLE_VALUE) {
    DWORD written;
    WriteFile(hOut, line, len, &written, NULL);
}

/* 4. Never AllocConsole -- it replaces inherited handles */
```

---

## MFC ClassWizard Blocks

Preserve `//{{AFX_...}}` markers exactly as-is:

```cpp
//{{AFX_DATA_MAP(CCaptureCfg)
DDX_Text(pDX, IDC_MAX_FRAMES, m_maxAVIFrames);
//}}AFX_DATA_MAP
/* Add new DDX calls OUTSIDE the AFX block */
DDX_Check(pDX, IDC_CHK_SHA256, m_enableSHA256);
```

---

## Settings Persistence

```cpp
/* Registry (normal mode) */
AfxGetApp()->WriteProfileInt("Capture", "SHA256", m_enableSHA256);
m_enableSHA256 = AfxGetApp()->GetProfileInt("Capture", "SHA256", TRUE) != 0;

/* INI file (portable mode) is handled transparently by MFC when
 * CWinApp::m_pszProfileName points to a .ini file instead of registry. */
```

---

## Anti-Patterns (Project-Specific)

| Avoid | Do Instead |
| --- | --- |
| `new`/`delete` in hot paths | Stack objects, `CArray`, ring buffers |
| `SendMessage` from worker thread | `PostMessage` (avoids deadlock) |
| `AllocConsole()` | `SafeAttachConsole()` + `GetStdHandle` |
| `#include <windows.h>` in `.h` | Rely on StdAfx.h PCH chain |
| C++11 `auto`, `nullptr`, `override` | Explicit types, `NULL`, no keyword |
| `std::vector`, `std::string` | `CArray`, `CString` (MFC types) |
| `using namespace` | Explicit qualification |
| `//` comments in `.c` files | `/* */` only (C89) |

---

## Quick Reference Checklist

Before marking WinDV C++ work complete:

- [ ] `StdAfx.h` is first include in every `.cpp` (not in `.c`)
- [ ] All variables initialized at declaration
- [ ] `const` on parameters and locals where possible
- [ ] No C++11+ features (compiles with VC6)
- [ ] No Win32 APIs above XP SP3 without `GetProcAddress` fallback
- [ ] Hungarian notation follows project convention (`m_`, `dw`, `sz`)
- [ ] `//{{AFX_...}}` blocks preserved unchanged
- [ ] Thread sync via `CAutoLock`/`CEvent`, UI via `PostMessage`
- [ ] No signed/unsigned comparison warnings (use matching types or explicit cast)
- [ ] HRESULT checked via `CHECK_HR()` macro
- [ ] Pure C modules: C89 only, `extern "C"` in header, no PCH
- [ ] New Resource IDs added to `Resource.h` with `_APS_NEXT_CONTROL_VALUE` incremented
