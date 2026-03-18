# WinDV — Project Overview

WinDV is a small Win32 utility for DV (FireWire / IEEE 1394)
digital video input and output.
It captures live DV streams from a camcorder or DV deck to AVI
files on disk, and plays AVI files back to a DV device over
FireWire.
The application is built on DirectShow and MFC and targets
32-bit Windows.

## Table of Contents

1. [History and Maintainership](#1-history-and-maintainership)
2. [Supported Platforms and Prerequisites](#2-supported-platforms-and-prerequisites)
3. [Build Instructions](#3-build-instructions)
4. [Architecture Overview](#4-architecture-overview)
5. [Module Reference](#5-module-reference)
6. [Command-Line Interface](#6-command-line-interface)
7. [Configuration and Settings](#7-configuration-and-settings)
8. [License](#8-license)

---

## 1. History and Maintainership

Petr Mourek (Czech Republic) wrote WinDV during 2002 and 2003.
In 2010 he released the source code at
[windv.mourek.cz](http://windv.mourek.cz/)
(archived at the
[Wayback Machine](https://web.archive.org/web/http://windv.mourek.cz/)).

Hunter Figgs (hfiggs) adopted the project as maintainer
circa 2023 to preserve Petr's work and keep the utility
functional on contemporary Windows versions.
The canonical repository is at
`https://github.com/hfiggs/WinDV`.

The codebase has received no significant functional changes
since the original release; the value of the project is the
preservation of a working DV capture tool that still handles
MiniDV tape archival on modern hardware.

---

## 2. Supported Platforms and Prerequisites

### Operating System

Windows 98 SE, 2000, ME, XP, Vista, 7, 8, 8.1, 10, and 11
(32-bit process on 64-bit OS).

### Hardware

| Requirement | Notes |
| --- | --- |
| FireWire (IEEE 1394) controller | Must be OHCI-compliant |
| DV camcorder or DV deck | DV-in port required for Record |

### Runtime Dependencies

| Component | Version |
| --- | --- |
| DirectX | 8.1 or later |
| MFC shared DLL | Ships with VC++ 6.0 redistributable |
| Common Controls | Version 6.0 (via manifest) |

### Build-Time Dependencies

| Component | Purpose |
| --- | --- |
| Visual C++ 6.0 | Compiler and IDE (`.dsp`/`.dsw`) |
| DirectShow BaseClasses | Custom filter implementation |
| Windows / Platform SDK | Header and import library paths |

---

## 3. Build Instructions

### Prerequisites

Before building, install:

1. **Visual C++ 6.0** (MSVC 6, `cl.exe` version 12.x).
2. **Microsoft Platform SDK** (or Windows SDK) that includes
   DirectShow BaseClasses source. The `.dsp` file hard-codes
   paths under `C:\Program Files\Microsoft SDK\`. Adjust the
   include and library paths if your SDK is installed elsewhere.
3. **Build the DirectShow BaseClasses** from
   `<SDK>\Samples\Multimedia\DirectShow\BaseClasses\`
   for both Release and Debug configurations before linking
   WinDV. This produces `strmbase.lib` (Release) and
   `strmbasd.lib` (Debug).

### Opening the Project

Open `WinDV\WinDV.dsw` in the Visual C++ 6.0 IDE.
The workspace contains the single project `WinDV`.

### Building from the IDE

Select **Build > Set Active Configuration** and choose either:

- `WinDV - Win32 Release` — optimised, links `strmbase.lib`
- `WinDV - Win32 Debug` — debug info, links `strmbasd.lib`

Then press **F7** (Build).
Output is placed in `WinDV\Release\` or `WinDV\Debug\`.

### Building from the Command Line (NMAKE)

First export the makefile from the IDE
(**Project > Export Makefile**),
then run from the `WinDV\WinDV\` directory:

```bat
:: Release build
NMAKE /f "WinDV.mak" CFG="WinDV - Win32 Release"

:: Debug build
NMAKE /f "WinDV.mak" CFG="WinDV - Win32 Debug"
```

### Compiler Flags (Summary)

| Flag | Meaning |
| --- | --- |
| `/MD` / `/MDd` | Link MFC as shared DLL (`_AFXDLL`) |
| `/W3` | Warning level 3 |
| `/GX` | Enable C++ exception handling |
| `/O2` (Release) | Optimise for speed |
| `/ZI /Od` (Debug) | Full debug info, no optimisation |
| `_WIN32_DCOM` | Enable DCOM COM initialisation |
| `_MBCS` | Multi-byte character set |

### SDK Path Adjustment

If your SDK is not at `C:\Program Files\Microsoft SDK\`,
update the two `/I` (include) directives and the `/libpath:`
linker directive in the `.dsp` file, or set environment
variables before invoking NMAKE:

```bat
set INCLUDE=C:\YourSDK\include;%INCLUDE%
set LIB=C:\YourSDK\...\BaseClasses\Release;%LIB%
```

### Linked Libraries

| Library | Purpose |
| --- | --- |
| `strmbase.lib` / `strmbasd.lib` | DirectShow BaseClasses |
| `quartz.lib` | DirectShow runtime |
| `winmm.lib` | Windows multimedia (timer) |
| `ole32.lib`, `olepro32.lib`, `oleaut32.lib` | COM / OLE |
| `uuid.lib` | COM interface GUIDs |
| `advapi32.lib` | Registry API |
| `version.lib` | Version resource API |
| `largeint.lib` | 64-bit integer helpers |
| `comctl32.lib` | Windows Common Controls |
| `kernel32.lib`, `user32.lib`, `gdi32.lib` | Core Win32 |
| `msvcrt.lib` / `msvcrtd.lib` | C runtime |

---

## 4. Architecture Overview

WinDV is a dialog-based MFC application.
There is no document/view pattern; the entire UI is one
resizable dialog (`CDVToolsDlg`).

### 4.1 Startup Sequence

```text
WinMain (MFC)
  +-- CWinDVApp::InitInstance()
        +-- SetPriorityClass(HIGH_PRIORITY_CLASS)
        +-- CoInitializeEx(COINIT_MULTITHREADED)
        +-- SetRegistryKey("Petr Mourek")
        +-- CDVToolsDlg::DoModal()
              +-- CoUninitialize()
```

The process runs at `HIGH_PRIORITY_CLASS` to minimise frame
drops. COM is initialised in multi-threaded mode
(`COINIT_MULTITHREADED`) because DirectShow filter graph
threads may call back on any thread.

### 4.2 The DirectShow Pipeline

WinDV builds DirectShow filter graphs to move DV frames
between a FireWire device and an AVI file (or vice-versa).
All graph classes live in `DShow.cpp` / `DShow.h`.

#### Capture pipeline (FireWire to AVI file)

```text
[FireWire Device (CDVInput)]
       |  DV interleaved frames
       v
[CInputGraph::CInputPin]  (custom CBaseInputPin sink)
       |  HandleFrame() callback
       v
[CDV::HandleFrame()]
       |  CDVQueue::Put()
       v
[CDVQueue]  (ring buffer, 100 slots)
       |  CDVQueue::Get() -- CapturingThread
       +-> [CMonitor]   (preview, throttled)
       +-> [CAVIWriter]  (AVI file output)
```

#### Record pipeline (AVI file to FireWire)

```text
[CAVIJoiner]  (sequences multiple CAVIReader instances)
       |  HandleFrame() callback
       v
[CDV::HandleFrame()]
       |  CDVQueue::Put()
       v
[CDVQueue]  (ring buffer, 100 slots)
       |  CDVQueue::Get() -- RecordingThread
       +-> [CMonitor]   (optional preview)
       +-> [CDVOutput]   (FireWire device)
```

#### Base class hierarchy

```text
CFilterGraph
  owns ICaptureGraphBuilder2, IGraphBuilder,
  IMediaControl, IMediaSeeking, IMediaEventEx

  CInputGraph       custom CBaseInputPin sink filter
    CAVIReader      AVI file source + AVI Splitter
    CDVInput        FireWire capture; mixes CDVControl

  COutputGraph      custom CBaseOutputPin source filter
    CAVIWriter      AVI Mux + file sink
    CDVOutput       FireWire output; mixes CDVControl
    CMonitor        DV decoder + IVideoWindow preview

CDVControl          IAMExtTransport tape transport
CDVQueue            thread-safe ring buffer
CAVIJoiner          sequences multiple CAVIReader instances
CDV                 orchestrator; owns entire pipeline
```

### 4.3 CDV State Machine

`CDV` is the central orchestrator.
It exposes a two-phase build/start model to allow the UI
to show a preview before the user clicks the action button.

```text
Idle
 +- BuildCapturing()  -> CapturePaused
 |    StartCapturing() -> Capturing
 |      StopCapturing() -> CapturePaused
 |      (timed capture expires) -> Finished
 +- BuildRecording()  -> RecordPaused
      StartRecording() -> Recording
        StopRecording() -> RecordPaused
        (source exhausted) -> Finished
```

`Destroy()` resets any state back to `Idle` and tears down
all pipeline objects. It signals the queue end-of-stream,
waits for the worker thread to exit, then deletes all
pipeline objects in dependency order.

### 4.4 Threading Model

Five distinct threads may be active simultaneously:

| Thread | Class | Priority |
| --- | --- | --- |
| Main (UI) | `CDVToolsDlg` | `HIGH_PRIORITY_CLASS` (process) |
| Capturing | `CDV` | `THREAD_PRIORITY_NORMAL` |
| Recording | `CDV` | `THREAD_PRIORITY_NORMAL` |
| Monitoring | `CMonitor` | `THREAD_PRIORITY_BELOW_NORMAL` |
| Joiner | `CAVIJoiner` | `THREAD_PRIORITY_NORMAL` |

**Thread roles:**

- **Main** — Message pump, timer updates
- **CapturingThread** — Drains `CDVQueue`, writes frames to
  `CAVIWriter`, manages file splits
- **RecordingThread** — Drains `CDVQueue`, pushes frames to
  `CDVOutput`
- **MonitoringThread** — Throttled delivery of preview frames;
  sleeps up to 200 ms between frames
- **JoinerThread** — Switches from one `CAVIReader` to the
  next on end-of-stream

All threads are created with
`AfxBeginThread(..., CREATE_SUSPENDED)` and resumed
immediately. `m_bAutoDelete = FALSE` is set on every thread
so the `CWinThread` object can be explicitly deleted after
`WaitForSingleObject`.

**Synchronisation primitives:**

| Primitive | Purpose |
| --- | --- |
| `CCritSec` / `CAutoLock` | Protect queue counters |
| `CEvent` (m_evGet/m_evPut) | Wake consumer/producer |
| `CEvent` (CAVIJoiner) | Signal file transition |
| `CEvent` (CMonitor) | Trigger preview delivery |
| `WaitForSingleObject` | Block until worker exits |

Frame arrival on the DirectShow graph thread calls
`CDV::HandleFrame()`, which calls `CDVQueue::Put()`.
The UI is updated via
`PostMessage(WM_DV_TIMECHANGE, 0, dvTime)` from the
worker thread, keeping all window manipulation on the
UI thread.

### 4.5 AVI Output Modes

Two AVI types are supported, selectable per-session:

| Type | Description |
| --- | --- |
| Type 1 | Raw DV interleaved stream; smaller |
| Type 2 | Separate video + audio; broadly compatible |

Type 1 graph: `OutputFilter -> AVI Mux -> file`

Type 2 graph:
`OutputFilter -> DVSplitter -> (video+audio) -> AVI Mux -> file`

### 4.6 Preview Rendering

`CMonitor` runs a dedicated `MonitoringThread` that:

1. Requests a delivery buffer from the output pin.
2. Sleeps for up to 200 ms (adaptive) to throttle preview.
3. Waits on `m_ev` for `HandleFrame()` to copy frame data.
4. Delivers the sample to the DV decoder + `IVideoWindow`.

The DV decoder is set to `DVDECODERRESOLUTION_360x240`
(half-resolution, lower CPU cost). The preview window
maintains a 4:3 aspect ratio.

During capture, preview is skipped when the queue is more
than half full (to avoid starving the AVI writer).
During recording, preview is skipped when the queue is less
than half full (frames delivered faster than they arrive).

### 4.7 Automatic File Splitting

During capture, `CDV::CapturingThread()` creates a new
`CAVIWriter` (new output file) when:

- The current file has reached `m_maxAVIFrames` frames
  (default: 25 fps x 60 s x 15 min = 22,500 frames), or
- The DV recording timestamp jumps by more than
  `m_discontinuityTreshold` seconds (default: 1 s),
  indicating a tape cut or a new recording segment.

The new filename is generated by `GetCaptureFilename()`:
it appends a date/time string (from `m_dtformat`) and an
auto-incrementing numeric suffix (zero-padded to `m_ndigits`
digits) to the base filename.

Files are first written to a temporary name prefixed with `~`
and renamed atomically on close, so interrupted captures do
not leave partially-written files with their final name.

### 4.8 Runtime Safety Features

Several defensive features protect long unattended capture sessions.

#### HRESULT checking (v1.2.5)

All DirectShow pipeline calls are guarded with either the `CHECK_HR()`
macro or explicit `SUCCEEDED()` tests.
Previously unchecked call sites — including
`CInputGraph::Run()`, `CInputGraph::GetMediaType()`,
the `CAVIReader` and `CMonitor` constructors, and
`CMonitor::HandleFrame()` — now throw `CDShowException` on failure
rather than silently proceeding with an invalid state.
`COutputGraph::HandleFrame()` emits a `TRACE` message when
`Deliver()` fails, preserving diagnostic output in Debug builds
without interrupting the pipeline.

#### Low disk space warning (v1.2.6)

During capture, `CapturingThread` checks `GetDiskFreeSpaceEx()` on
the capture destination drive roughly once per minute (~1500 frames).
When free space drops below 500 MB, it posts `WM_DV_LOWDISKSPACE`
(`WM_USER + 202`) to the dialog.
`CDVToolsDlg::OnDVLowDiskSpace()` displays
`"WARNING: Low disk space! X MB remaining"` in the status bar and
sounds `MessageBeep(MB_ICONEXCLAMATION)`.
Capture is not stopped automatically; the operator decides whether
to intervene.

#### End-of-signal auto-stop (v1.2.7)

`CDVQueue::GetWithTimeout()` wraps the normal `Get()` consumer with
a `WaitForSingleObject` timeout.
When `CDV::m_autoStopTimeout` is greater than zero (default: 5000 ms),
`CapturingThread` uses this variant.
If no frame arrives within the timeout window and no EOS marker is
present, the thread interprets the condition as a lost FireWire signal,
posts `WM_DV_SIGNALLOST` (`WM_USER + 203`), transitions the pipeline
to `Finished`, and exits.
`CDVToolsDlg::OnDVSignalLost()` shows
`"Signal lost - capture stopped."` and beeps.
Set `m_autoStopTimeout = 0` to disable the feature.

---

## 5. Module Reference

All source files reside under `WinDV\WinDV\`.

### Source Files

| File | Class(es) | Role |
| --- | --- | --- |
| `WinDV.cpp` | `CWinDVApp` | App entry point, COM init |
| `WinDV.h` | `CWinDVApp` | App class declaration |
| `DVToolsDlg.cpp` | `CDVToolsDlg` | Main UI dialog |
| `DVToolsDlg.h` | `CDVToolsDlg` | Dialog declaration |
| `DShow.cpp` | (all pipeline) | DirectShow pipeline impl |
| `DShow.h` | (all pipeline) | Pipeline declarations |
| `DV.cpp` | — | DV frame timestamp parser |
| `DV.h` | — | `GetDVRecordingTime()` decl |
| `CaptureCfg.cpp` | `CCaptureCfg` | Capture config page |
| `CaptureCfg.h` | `CCaptureCfg` | Capture config decl |
| `RecordCfg.cpp` | `CRecordCfg` | Record config page |
| `RecordCfg.h` | `CRecordCfg` | Record config decl |
| `DropFilesEdit.cpp` | `CDropFilesEdit` | Drag-drop edit ctrl |
| `DropFilesEdit.h` | `CDropFilesEdit` | Drop edit decl |
| `ToolTab.cpp` | `CToolTab` | Owner-draw tab control |
| `ToolTab.h` | `CToolTab` | Tab control decl |
| `VideoDeviceSel.cpp` | `CVideoDeviceSel` | Device picker dialog |
| `VideoDeviceSel.h` | `CVideoDeviceSel` | Device picker decl |
| `StdAfx.cpp` | — | Precompiled header unit |
| `StdAfx.h` | — | Precompiled header |
| `WinDV.rc` | — | Resource script |
| `Resource.h` | — | Resource ID definitions |
| `WinDV.exe.manifest` | — | Common Controls 6.0 |

### Key Module Details

- **`DVToolsDlg.cpp`** — Handles tab switching, device
  selection, file selection, Capture/Record button logic,
  configuration dialog, timer-based status updates, registry
  persistence on close, and command-line parsing.
- **`DShow.cpp`** — All graph construction, threading, frame
  routing, device enumeration, file splitting, and the `CDV`
  orchestrator live here (~1300 lines).
- **`DV.cpp`** — `GetDVRecordingTime()` extracts recording
  timestamps from SSYB subcode packs (0x62=date, 0x63=time).
  Supports NTSC (120,000 bytes) and PAL (144,000 bytes).
- **`DropFilesEdit.cpp`** — `CEdit` subclass that accepts
  file drops with configurable separator and optional
  transform callback.

### Resource Files

| File | Contents |
| --- | --- |
| `res\WinDV.ico` | Application icon (main frame) |
| `res\DVTool.ico` | Alternative DV tool icon |
| `res\icon1.ico` | Additional icon resource |
| `res\WinDVlogo.ico` | Logo icon (About dialog) |
| `res\WinDV.rc2` | Additional resource script |

---

## 6. Command-Line Interface

WinDV supports unattended operation via command-line
arguments, parsed in `CDVToolsDlg::OnInitDialog()`.

```bat
WinDV.exe capture [-exit] <duration> <filename>
WinDV.exe record  [-exit] <file> [<file> ...]
```

### Subcommands

#### `capture`

Captures live DV from the last-selected FireWire input
device for a fixed duration.

| Argument | Description |
| --- | --- |
| `-exit` | Close app when capture finishes |
| `<duration>` | How long to capture (see below) |
| `<filename>` | Base path for output AVI (no extension) |

**Example — capture 15 minutes and exit:**

```bat
WinDV.exe capture -exit 15:00 C:\Tapes\tape001
```

Output files will be named `tape001.01.avi`,
`tape001.02.avi`, etc. (actual suffix format depends on
configured datetime format and suffix digit count).

#### `record`

Plays one or more AVI files back to the last-selected
FireWire output device. Multiple filenames are joined into
a single continuous playback sequence.
Glob patterns (`*`, `?`) are accepted and expanded in
sorted order.

| Argument | Description |
| --- | --- |
| `-exit` | Close app when playback finishes |
| `<file>` | Path to AVI file; may include wildcards |

**Example — record two files to tape:**

```bat
WinDV.exe record -exit clip1.avi clip2.avi
```

The configured AVI prefix and suffix lists (from the Record
configuration) are prepended and appended to the file list.

### Duration Format

```text
[HH:]MM:SS[.microseconds]
SS[.microseconds]
```

Microseconds are a decimal fraction:
`.5` = 500,000 us = 0.5 s.

| Example | Meaning |
| --- | --- |
| `30` | 30 seconds |
| `1:30` | 1 minute 30 seconds |
| `1:30:00` | 1 hour 30 minutes |
| `90.5` | 90.5 seconds |

The duration is converted to `REFERENCE_TIME` (100 ns units):
`t = ((HH*60 + MM)*60 + SS) * 10000000 + microseconds`.

### Notes on Device Selection

The command-line modes use whichever device names were saved
in the registry from the last interactive session.
There is no command-line argument to override the device name.
Use the interactive UI at least once to select the correct
device, then rely on the command line for automation.

---

## 7. Configuration and Settings

All settings persist to the Windows Registry under:

```text
HKEY_CURRENT_USER\Software\Petr Mourek\WinDV
```

The registry key root is set by
`SetRegistryKey("Petr Mourek")` in
`CWinDVApp::InitInstance()`.

### Main Window

Registry section: `MainWindow`

| Value | Type | Default |
| --- | --- | --- |
| `X` | `DWORD` | 0 |
| `Y` | `DWORD` | 0 |
| `W` | `DWORD` | 0 |
| `H` | `DWORD` | 0 |
| `SelectedTool` | `DWORD` | 0 |
| `DVControlEnabled` | `DWORD` | 0 |
| `WorkingDirectory` | `SZ` | `.` |

- `X`, `Y`, `W`, `H` — Window position and size
- `SelectedTool` — Last active tab (0=Capture, 1=Record)
- `DVControlEnabled` — Tape transport control checkbox
- `WorkingDirectory` — Current directory for relative paths

When `W` and `H` are both zero (first run), the About dialog
is shown automatically.

### Capture Settings

Registry section: `Capture`

| Value | Type | Default |
| --- | --- | --- |
| `DVDevice` | `SZ` | `Microsoft DV Camera and VCR` |
| `File` | `SZ` | (empty) |
| `Type2AVI` | `DWORD` | 1 |
| `DiscontinuityTreshold` | `DWORD` | 1 |
| `MaxAVIFrames` | `DWORD` | 22500 |
| `EveryNth` | `DWORD` | 1 |
| `DateTimeFormat` | `SZ` | `%y-%m-%d_%H-%M` |
| `DateTimeFormatHistory` | `SZ` | (presets) |
| `SuffixDigits` | `DWORD` | 2 |

- `Type2AVI` — 1=Type 2 (separate video+audio), 0=Type 1
- `DiscontinuityTreshold` — Timestamp jump (seconds) that
  triggers a new file; 0=disabled
- `MaxAVIFrames` — Max frames per file (25fps x 15min)
- `EveryNth` — Frame decimation (1=all, 2=every other)
- `DateTimeFormat` — `strftime` format for filename suffix
- `SuffixDigits` — Zero-padded digit width for auto-number

The `DateTimeFormat` is applied to the DV recording timestamp
embedded in the frame (when available) or to the wall-clock
time at the moment recording starts.

### Record Settings

Registry section: `Record`

| Value | Type | Default |
| --- | --- | --- |
| `DVDevice` | `SZ` | `Microsoft DV Camera and VCR` |
| `File` | `SZ` | (empty) |
| `AVIPrefix` | `SZ` | (empty) |
| `AVISuffix` | `SZ` | (empty) |
| `Preview` | `DWORD` | 1 |

- `AVIPrefix` / `AVISuffix` — Files played before/after the
  main file list (pipe-separated)
- `Preview` — 1=show preview during recording, 0=no preview

### DV Tape Transport Control

The "DV Ctrl" checkbox (`IDC_DVCTRL`) enables automatic tape
transport control via `IAMExtTransport`:

- **Capture** — Play on start, Freeze/Pause on stop
- **Record** — Record-Pause on build, Record on start
- **Destroy** — Stop

If the device does not expose `IAMExtTransport`, the checkbox
has no effect (`QueryInterface` silently fails).

### Capture Filename Generation

Output filenames are constructed by `GetCaptureFilename()`:

```text
<base>.<datetime>.<N>.avi
```

Where:

- `<base>` is the path entered in the capture file field
- `<datetime>` is the formatted DV recording timestamp
- `<N>` is a zero-padded auto-incrementing integer

The collision check scans the directory for existing files
matching `<base>.<datetime>.*.avi` and picks the next value.
During active capture the file is written to a temporary name
prefixed with `~` and renamed to the final name on close.

---

## 8. License

WinDV is distributed under the MIT License.

```text
MIT License

Copyright (c) 2002-2003 Petr Mourek
Copyright (c) 2023 Hunter Figgs

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or
sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.
```

See `WinDV\LICENSE` for the canonical copy.
