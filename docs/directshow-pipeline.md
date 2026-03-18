# DirectShow Pipeline

This document explains how WinDV uses DirectShow, why custom filter
wrappers are needed, and the exact filter graph topology for every
operating mode.

Related reading:
[architecture.md](architecture.md) for the class hierarchy,
[threading.md](threading.md) for how frames are routed between threads.

---

## Table of Contents

1. [Why WinDV uses DirectShow](#1-what-directshow-is-and-why-windv-uses-it)
2. [CFilterGraph — base wrapper](#2-cfiltergraph--base-wrapper)
3. [Push-model bridge](#3-bridging-the-push-model-cinputgraph-and-coutputgraph)
4. [Capture pipeline](#4-capture-pipeline-cdvinput-to-caviwriter)
5. [Record pipeline](#5-record-pipeline-cavijoiner-to-cdvoutput)
6. [AVI Type 1 vs Type 2](#6-avi-type-1-vs-type-2)
7. [CAVIReader fallback](#7-cavireader-and-the-type-1type-2-fallback)
8. [Preview: CMonitor](#8-preview-cmonitor)
9. [Error handling](#9-error-handling)

---

## 1. What DirectShow is and why WinDV uses it

DirectShow is Microsoft's COM-based multimedia framework included in
Windows since Windows 98.
It provides:

- A **filter graph**: a directed graph of COM objects (filters) connected
  by pins, each transforming or transporting a media stream.
- System filters for common tasks: AVI file source/sink, AVI Splitter,
  AVI Mux, DV decoder, DV Splitter, DV Mux.
- Device enumeration and capture graph construction helpers
  (`ICreateDevEnum`, `ICaptureGraphBuilder2`).
- Transport control interfaces such as `IMediaControl` (run/pause/stop)
  and `IAMExtTransport` (tape deck control).

WinDV uses DirectShow because:

1. The Windows FireWire DV driver exposes itself as a DirectShow
   capture filter.
   There is no other standard API to read raw DV frames from IEEE 1394.
2. AVI file writing and reading via AVI Mux and AVI Splitter are
   provided by DirectShow's system filters, avoiding the need to
   implement AVI container logic.
3. The DV decoder (hardware-accelerated on many systems) and video
   renderer are standard DirectShow filters, making preview trivial.

---

## 2. CFilterGraph — base wrapper

`CFilterGraph` (declared in `DShow.h`, implemented in `DShow.cpp`) is the
base class for every filter graph variant in WinDV.
Its constructor creates and wires five COM interfaces, all owned by the
same underlying filter graph COM object:

| Member | Interface | Purpose |
| --- | --- | --- |
| `m_GB` | `ICaptureGraphBuilder2` | Helper for connecting capture topologies |
| `m_FG` | `IGraphBuilder` | Filter graph manager; owns all filters |
| `m_MC` | `IMediaControl` | Run / Pause / Stop the graph |
| `m_MS` | `IMediaSeeking` | Duration and position queries |
| `m_ME` | `IMediaEventEx` | Completion events (`EC_COMPLETE`) |

`m_GB` is initialised with `m_GB->SetFiltergraph(m_FG)` so that
`ICaptureGraphBuilder2::RenderStream()` operates on the same graph as
direct `IGraphBuilder` calls.

The destructor calls `m_MC->Stop()` before releasing interfaces to ensure
no frames are delivered after the subclass's pipeline objects are gone.

---

## 3. Bridging the push model: CInputGraph and COutputGraph

DirectShow is a **push model**: the source filter produces data and pushes
it downstream through pin connections.
WinDV needs to intercept that data and route it through its own ring buffer
and worker threads, which operate outside the DirectShow graph.

Two custom filter wrappers solve this:

### CInputGraph — custom sink filter

`CInputGraph` builds a filter graph that **terminates** in a custom sink
filter (`CInputFilter`) containing one input pin (`CInputPin`).

```text
[upstream source filters]
         |
         v
   CInputFilter / CInputPin   (CBaseInputPin)
         |  CInputPin::Receive()
         v
   CInputGraph::m_handler->HandleFrame()
```

`CInputPin::Receive()` is the hot path, called once per frame at 25 or
30 Hz on DirectShow's streaming thread.
It extracts the frame duration and raw bytes from `IMediaSample` and calls
the registered `CFrameHandler`.

`CInputFilter` uses `CLSID_NULL` as its CLSID because it is never
registered in the system registry; it exists only within the process's
graph.

`CheckMediaType()` on `CInputPin` accepts any stream typed as
`MEDIATYPE_Interleaved`, which covers both raw DV from FireWire and the
interleaved DV stream produced by a DV Mux when reading Type-2 AVI files.

### COutputGraph — custom source filter

`COutputGraph` builds a filter graph that **originates** from a custom
source filter (`COutputFilter`) containing one output pin (`COutputPin`).

```text
   COutputGraph::HandleFrame()
         |  acquires IMediaSample, copies data, sets timestamps
         v
   COutputFilter / COutputPin   (CBaseOutputPin)
         |
         v
   [downstream sink filters]
```

`HandleFrame()` packages raw DV bytes into an `IMediaSample`, assigns a
monotonically increasing presentation timestamp range
`[m_time, m_time + duration]`, marks it as a sync point (all DV frames are
independently decodable), and calls `Deliver()`.

When `m_queue > 0`, `COutputPin::Active()` creates a `COutputQueue` — a
DirectShow helper that moves sample delivery onto a dedicated thread
running at `THREAD_PRIORITY_ABOVE_NORMAL`.
This prevents `HandleFrame()` on the recording thread from blocking if
the downstream filter (AVI Mux or DV device) is momentarily slow.
`CDVOutput` requests a queue depth of 10 to absorb FireWire timing jitter.
`CAVIWriter` uses synchronous delivery (queue depth 0).

---

## 4. Capture pipeline: CDVInput to CAVIWriter

### CDVInput

`CDVInput` is a `CInputGraph` + `CDVControl` subclass.

**Graph topology:**

```text
DV capture filter (FireWire, friendly name = vsrc)
         |  MEDIATYPE_Interleaved
         v
   CInputFilter (registered as "InputFilter" in m_FG)
```

Construction steps (`CDVInput::CDVInput`):

1. `EnumVideoDevices()` enumerates `CLSID_VideoInputDeviceCategory` via
   `ICreateDevEnum` / `IEnumMoniker` and binds the named device to
   `IBaseFilter *pVSRC`.
2. `CtrlAttach(pVSRC)` queries `IAMExtTransport` from the device filter
   so that tape transport commands (play/pause/stop) work if supported.
3. `m_FG->AddFilter(pVSRC, L"DVin")` registers the capture filter.
4. `m_GB->RenderStream(NULL, &MEDIATYPE_Interleaved,
   pVSRC, NULL, m_inputFilter)` connects the capture pin
   to `CInputFilter`.
5. `IAMDroppedFrames` is queried from the capture pin's upstream output
   pin so dropped frame counts can be reported to the UI.

### CDV::HandleFrame and CDVQueue

`CDV` implements `CFrameHandler`.
`CDV::HandleFrame()` is the only method: it calls
`CDVQueue::Put(duration, data, len)`.
This is the handoff from the DirectShow streaming thread to the
application's own worker thread.
See [threading.md](threading.md) for queue details.

### CapturingThread

`CDV::CapturingThread()` is the consumer.
For each frame dequeued it:

1. Calls `GetDVRecordingTime()` to extract the camcorder timestamp.
2. Posts `WM_DV_TIMECHANGE` to the parent window if the timestamp
   changed.
3. Calls `CMonitor::HandleFrame()` when the queue is below half
   capacity (preview throttling).
4. Creates a new `CAVIWriter` if none is open or if a split condition
   is met (frame count limit or DV timestamp discontinuity).
5. Calls `CAVIWriter::HandleFrame()` for every `m_everyNth` frame.
6. Updates `m_counter`, `m_time`, and checks the timed capture limit.

**Disk space monitoring (v1.2.6):**
Every ~1500 frames the thread checks free disk space via
`GetDiskFreeSpaceEx()` on the drive root of `m_captureFilename`.
If less than 500 MB remains it posts `WM_DV_LOWDISKSPACE`
(`WM_USER + 202`) to the dialog so the user can be warned without
interrupting the capture in progress.

**End-of-signal auto-stop (v1.2.7):**
When `CDV::m_autoStopTimeout > 0` (default 5000 ms), the thread uses
`CDVQueue::GetWithTimeout()` rather than the blocking `Get()`.
A timeout without an EOS marker indicates the FireWire signal was lost.
The thread posts `WM_DV_SIGNALLOST` (`WM_USER + 203`), transitions the
state to `Finished`, and exits cleanly without requiring manual
intervention.

### CAVIWriter

`CAVIWriter` is a `COutputGraph` subclass.

**Graph topology — Type-2 AVI (default):**

```text
COutputFilter (COutputGraph)
         |  MEDIATYPE_Interleaved
         v
   DV Splitter (CLSID_DVSplitter)
    |         |
    | video   | audio
    v         v
         AVI Mux (MEDIASUBTYPE_Avi)
         |
         v
   File Sink (IFileSinkFilter2, AM_FILE_OVERWRITE)
```

**Graph topology — Type-1 AVI:**

```text
COutputFilter (COutputGraph)
         |  MEDIATYPE_Interleaved
         v
   AVI Mux (MEDIASUBTYPE_Avi)
         |
         v
   File Sink
```

**Temporary file strategy:**
The AVI file is initially written to a path with a `~` prefix inserted
into the datetime portion of the filename (e.g. `clip.~23-01-15_12-30.01.avi`).
On destruction, `CAVIWriter` sends `DeliverEndOfStream()`, waits up to
5 seconds for `EC_COMPLETE` (so the AVI Mux writes the file index), then
calls `MoveFile()` to atomically rename to the final path.
An aborted capture therefore leaves a `~`-prefixed file rather than a
corrupt file with its final name.

---

## 5. Record pipeline: CAVIJoiner to CDVOutput

### CAVIJoiner

`CAVIJoiner` implements `CFrameSource` and `CFrameHandler`.
It sequences multiple `CAVIReader` instances into a single logical stream.

Construction:

1. The pipe-delimited `filenames` string is split on `|`.
2. Each token is glob-expanded via `CFileFind`.
3. Each glob result is sorted lexicographically with `qsort` /
   `CompareStrings` so that split files (`clip.01.avi`, `clip.02.avi`)
   play in the correct order.
4. All resolved paths are collected into `m_filenames[]`.
5. The first `CAVIReader` is opened immediately so `GetMediaType()` is
   available before `Run()` is called.

At runtime, `JoinerThread` waits on `m_ev`.
When the current `CAVIReader` reaches end-of-stream, `HandleFrame()` is
called with `data == NULL`, which signals `m_ev`.
`JoinerThread` then destroys the old reader and opens the next file.
When all files are exhausted, `JoinerThread` sends an EOS frame
(`duration = -1, data = NULL`) to `m_joinHandler` (which is `CDV`) and
exits.

### CDVOutput

`CDVOutput` is a `COutputGraph` + `CDVControl` subclass.

**Graph topology:**

```text
COutputFilter (COutputGraph, async queue depth 10)
         |
         v
   DV output filter (FireWire device, friendly name = vdst)
```

The graph is started immediately in the constructor.
The destructor sends `DeliverEndOfStream()` and waits up to 5 seconds
for `EC_COMPLETE` to ensure all queued frames reach the device before
the graph is torn down.

### RecordingThread

`CDV::RecordingThread()` consumes frames from `CDVQueue` and calls
`CDVOutput::HandleFrame()` for each frame when `m_state == Recording`.
While `m_state == RecordPaused`, frames are consumed but not forwarded.
When `CDVQueue::Get()` returns `false` (EOS), the state transitions to
`Finished` and the thread exits.

---

## 6. AVI Type 1 vs Type 2

DV content can be stored in AVI containers in two ways:

| Feature | Type 1 | Type 2 |
| --- | --- | --- |
| Video stream | Single interleaved stream | Separate `vids` (DV) stream |
| Audio stream | Interleaved within the DV stream | Separate `auds` stream |
| File size | Slightly smaller | Slightly larger |
| Compatibility | Some editing apps struggle | Broadly compatible |
| WinDV default | No | Yes (m_type2AVI = true) |

**Type-1 capture graph:**

```text
COutputFilter --> AVI Mux --> File Sink
```

The AVI Mux receives the raw interleaved DV stream and stores it as a
single video track.
Some decoders and editing applications cannot read Type-1 AVI because they
expect separate audio and video streams.

**Type-2 capture graph:**

```text
COutputFilter --> DV Splitter --> AVI Mux --> File Sink
                  |                (video)
                  +-(audio) -----> AVI Mux
```

The DV Splitter (`CLSID_DVSplitter`) separates the interleaved DV stream
into a video stream and one or more audio streams.
The AVI Mux receives both and writes them as separate tracks.
This is the format produced by most DV camcorders' direct computer
connection and is the most widely supported for editing.

The `m_type2AVI` flag on `CDV` is persisted to the registry as
`Capture\Type2AVI` and toggled from the configuration dialog
(`CCaptureCfg`).

---

## 7. CAVIReader and the Type-1/Type-2 fallback

When reading an AVI file back for the Record pipeline, WinDV must accept
both Type-1 and Type-2 files.
`CAVIReader` handles this with a two-attempt strategy:

**Attempt 1 — Type-1 (direct interleaved output):**

```text
FileSource --> AVI Splitter --(MEDIATYPE_Interleaved)--> CInputFilter
```

`m_GB->RenderStream(NULL, &MEDIATYPE_Interleaved, pAVI, NULL, m_inputFilter)`
succeeds if the AVI Splitter exposes a `MEDIATYPE_Interleaved` output pin
(a Type-1 file).

**Attempt 2 — Type-2 (DV Mux re-interleave):**

```text
FileSource --> AVI Splitter --(video)--> DV Mux --> CInputFilter
                             --(audio)->
```

If the first attempt fails or leaves `CInputPin` unconnected, a
`CLSID_DVMux` is inserted.
The DV Mux recombines the separate video and audio streams back into an
interleaved DV stream before delivery to `CInputFilter`.
This restores the format that `CDV::HandleFrame()` expects regardless of
the source file type.

---

## 8. Preview: CMonitor

`CMonitor` is a `COutputGraph` subclass that renders frames to a preview
window.
Its topology is built automatically by `ICaptureGraphBuilder2::RenderStream()`
with a `NULL` sink:

```text
COutputFilter --> (auto-connected) DV Video Decoder --> Video Renderer
```

DirectShow selects and connects the appropriate decoder and renderer
automatically.
After graph construction, `SetDVDecoding(m_FG, 0)` sets the DV Video
Decoder to `DVDECODERRESOLUTION_360x240` (half-D1) to reduce CPU load.

The renderer window is embedded as a `WS_CHILD` subwindow of the
`CDV` control's `HWND` via `IVideoWindow::put_Owner()`.
`CMonitor::Resize()` recalculates its position to maintain the DV 4:3
aspect ratio whenever the parent control is resized.

Preview delivery is rate-limited by `MonitoringThread` to approximately
5 fps (200 ms interval).
See [threading.md § MonitoringThread](threading.md) for the full algorithm.

---

## 9. Error handling

All DirectShow `HRESULT` values are tested with the `CHECK_HR()` inline
function:

```cpp
inline void CHECK_HR(HRESULT hr,
                     LPCSTR message = "Error",
                     int cause = CDShowException::error)
{
    if (hr != S_OK) ThrowDShowException(cause, message);
}
```

`ThrowDShowException()` allocates a `CDShowException` (a `CException`
subclass with `b_AutoDelete = TRUE`) and throws it via MFC `THROW()`.
All call sites in `DVToolsDlg.cpp` wrap DirectShow operations in
`TRY { ... } CATCH_ALL(e) { ... } END_CATCH_ALL` blocks that either
display the error in `m_status` or call `InitVideo()` to reset the
pipeline to a known idle state.

`CDShowException::m_cause` distinguishes between:

| Cause | Value | Meaning |
| --- | --- | --- |
| `none` | 0 | No error (unused) |
| `deviceNotFound` | 1 | Named FireWire device not present |
| `error` | 2 | Generic DirectShow HRESULT failure |
