# Architecture

This document describes the high-level structure of WinDV: how the application
starts, how the major classes relate to one another, and how the CDV
orchestrator's state machine drives the pipeline.

For a component-level description of the DirectShow filter
graphs, see [directshow-pipeline.md](directshow-pipeline.md).
For thread roles and synchronization, see
[threading.md](threading.md).

---

## Architecture at a Glance

```text
 CWinDVApp (WinDV.cpp)
   COM init, process priority, registry key
   '-> CDVToolsDlg::DoModal()
 --------------------------------------------------
 CDVToolsDlg (DVToolsDlg.cpp)
   MFC Dialog, Tab-UI, Resize-Engine, Registry I/O
   CLI-Parser, Timer-based status update
   '- owns: CDV m_video
 --------------------------------------------------
 CDV (DShow.h/cpp) -- Orchestrator & CStatic
   State Machine:
     Idle <-> CapturePaused <-> Capturing
     Idle <-> RecordPaused  <-> Recording
                            --> Finished
   Owns: CDVInput/CAVIJoiner, CDVQueue,
         CAVIWriter/CDVOutput, CMonitor
   Threads: CapturingThread, RecordingThread
 --------------------------------------------------
 DirectShow Abstraction (DShow.h/cpp)
   CFilterGraph
     '-> CInputGraph -> CAVIReader, CDVInput
     '-> COutputGraph -> CAVIWriter, CDVOutput,
                         CMonitor
   CDVQueue    (ring buffer)
   CDVControl  (IAMExtTransport)
   CAVIJoiner  (multi-file sequencing)
 --------------------------------------------------
 DV.cpp          SSYB subcode parser (BCD time)
 DropFilesEdit   Drag-and-drop edit control
 ToolTab         Owner-draw tab control
 VideoDeviceSel  Device picker dialog
 CaptureCfg      Capture config property page
 RecordCfg       Record config property page
```

---

## Table of Contents

1. [Application model](#1-application-model)
2. [Startup sequence](#2-startup-sequence)
3. [COM apartment model](#3-com-apartment-model)
4. [Class hierarchy](#4-class-hierarchy)
5. [CDV state machine](#5-cdv-state-machine)
6. [How the pieces fit together](#6-how-the-pieces-fit-together)

---

## 1. Application model

WinDV uses the **MFC dialog-based** application pattern.
There is no document/view; the entire UI is a single resizable dialog
(`CDVToolsDlg`).
The application class (`CWinDVApp`) does almost nothing beyond COM
initialisation and registry key configuration before handing control to
`CDVToolsDlg::DoModal()`.

The dialog owns one instance of `CDV`, which is a `CStatic` subclass placed
directly on the dialog template as a picture control.
This dual role — MFC window and pipeline orchestrator — lets `CDV` host the
preview window as a child of its own `HWND` without needing a separate
container window.

---

## 2. Startup sequence

```text
WinMain (MFC framework)
  +-- CWinDVApp::InitInstance()
        +-- SetPriorityClass(HIGH_PRIORITY_CLASS)
        |     Raises the process priority to reduce frame drop risk.
        |
        +-- CoInitializeEx(NULL, COINIT_MULTITHREADED)
        |     Initialises COM in multi-threaded apartment (MTA).
        |     Required because DirectShow streaming threads call
        |     back on arbitrary threads.
        |
        +-- SetRegistryKey("Petr Mourek")
        |     All GetProfileInt/WriteProfileInt calls use
        |     HKCU\Software\Petr Mourek\WinDV as their root.
        |
        +-- CDVToolsDlg::DoModal()
        |     Runs the message pump until the dialog closes.
        |
        +-- CoUninitialize()
```

`HIGH_PRIORITY_CLASS` is set on the whole process, not just individual
threads.
Worker threads then run at `THREAD_PRIORITY_NORMAL` relative to that base,
while the monitoring thread runs at `THREAD_PRIORITY_BELOW_NORMAL`.
See [threading.md](threading.md) for the full priority table.

---

## 3. COM apartment model

COM is initialised with `COINIT_MULTITHREADED` (multi-threaded apartment,
MTA).
This means:

- Any thread may call any COM interface pointer without marshalling.
- DirectShow's internal filter-graph streaming thread delivers samples
  directly to `CInputPin::Receive()` on its own thread.
- The application is responsible for its own thread safety
  (see [threading.md](threading.md)).

If `CoInitializeEx` were called with `COINIT_APARTMENTTHREADED` instead,
every cross-thread COM call would be marshalled through the main thread's
message pump, which would serialise frame delivery and cause dropped frames.

---

## 4. Class hierarchy

```text
CFilterGraph
  Owns: ICaptureGraphBuilder2 (m_GB)
        IGraphBuilder         (m_FG)
        IMediaControl         (m_MC)
        IMediaSeeking         (m_MS)
        IMediaEventEx         (m_ME)
  |
  +-- CInputGraph   (CFrameSource)
  |     Inner class CInputFilter  (CBaseFilter, one CInputPin)
  |     Inner class CInputPin     (CBaseInputPin)
  |     |
  |     +-- CAVIReader
  |     |     Builds: FileSource -> AVI Splitter -> [DV Mux] -> CInputFilter
  |     |
  |     +-- CDVInput              (also CDVControl)
  |           Builds: DV capture filter -> CInputFilter
  |
  +-- COutputGraph  (CFrameHandler)
        Inner class COutputFilter (CBaseFilter, one COutputPin)
        Inner class COutputPin    (CBaseOutputPin, optional COutputQueue)
        |
        +-- CAVIWriter
        |     Builds: COutputFilter -> [DV Splitter] -> AVI Mux -> File Sink
        |
        +-- CDVOutput             (also CDVControl)
        |     Builds: COutputFilter -> DV output filter
        |
        +-- CMonitor
              Builds: COutputFilter -> (auto) DV Decoder -> Video Renderer

CDVControl
  Wraps IAMExtTransport (tape transport commands)
  Mixed into: CDVInput, CDVOutput

CDVQueue
  Thread-safe ring buffer (producer/consumer, 100 slots by default)

CAVIJoiner                        (CFrameSource, CFrameHandler)
  Sequences multiple CAVIReader instances

CDV                               (CStatic, CFrameHandler)
  Top-level orchestrator.
  Owns: CDVInput or CAVIJoiner (source)
        CDVQueue                (buffer)
        CAVIWriter or CDVOutput (sink)
        CMonitor                (preview)

CDVToolsDlg                       (CDialog)
  Main UI dialog.
  Owns: CDV m_video (the orchestrator / preview control)
```

`CFrameHandler` and `CFrameSource` are pure abstract interfaces defined in
`DShow.h`.
They decouple the pipeline source (CDVInput or CAVIJoiner) from the
orchestrator (CDV) and from each output sink (CAVIWriter, CDVOutput, CMonitor).

---

## 5. CDV state machine

`CDV` exposes a two-phase build/start model.
The `Build*` methods construct the pipeline and start the preview
(`CapturePaused` / `RecordPaused`) before the user initiates the action.
The `Start*` methods then begin writing or transmitting frames.

```text
Idle
 |
 +-- BuildCapturing(vsrc)    --> CapturePaused
 |     CDVInput + CMonitor + CDVQueue + CapturingThread created.
 |     Preview is live; no file is being written.
 |
 |     StartCapturing(...)   --> Capturing
 |     |   CAVIWriter created on first frame in CapturingThread.
 |     |   Automatic file splits happen inside CapturingThread.
 |     |
 |     |   StopCapturing()   --> CapturePaused
 |     |   (current AVI closed cleanly by CapturingThread)
 |     |
 |     +-- (captureTime reached)  --> Finished
 |
 +-- BuildRecording(files, vdst) --> RecordPaused
       CAVIJoiner + CMonitor + CDVOutput + CDVQueue + RecordingThread created.
       Frames are queuing; DV device is in record-pause if DVctrl is set.

       StartRecording()       --> Recording
       |   RecordingThread begins forwarding frames to CDVOutput.
       |
       |   StopRecording()    --> RecordPaused
       |
       +-- (source exhausted) --> Finished

Finished
  Destroy()  --> Idle
  (All pipeline objects torn down; counters reset.)
```

`Destroy()` is safe to call in any state.
Its shutdown sequence is described in [threading.md § Shutdown](threading.md).

---

## 6. How the pieces fit together

### Capture mode

```text
[FireWire device]
      | (DV frames, ~25-30 Hz)
      v
CDVInput::CInputPin::Receive()    <- DirectShow streaming thread
      | HandleFrame(duration, data, len)
      v
CDV::HandleFrame()
      | CDVQueue::Put()
      v
CDVQueue  (ring buffer, 100 slots)
      |
      +--CDVQueue::Get()  <- CapturingThread
      |     |
      |     +-- (queue < 50%) --> CMonitor::HandleFrame()  (preview)
      |     +-- (Capturing)   --> CAVIWriter::HandleFrame() (disk)
      |     +-- (WM_DV_TIMECHANGE posted to CDVToolsDlg)
      v
   [CAVIWriter -> AVI file(s)]
   [CMonitor   -> DV decoder -> Video Renderer -> IDC_VIDEO]
```

### Record mode

```text
[AVI files on disk]
      | (CAVIReader, one file at a time)
      v
CAVIJoiner::HandleFrame()         <- DirectShow streaming thread
      | CDVQueue::Put()
      v
CDVQueue  (ring buffer, 100 slots)
      |
      +--CDVQueue::Get()  <- RecordingThread
      |     |
      |     +-- (queue > 50% or EOS) --> CMonitor::HandleFrame()  (preview)
      |     +-- (Recording)          --> CDVOutput::HandleFrame() (FireWire)
      v
   [CDVOutput -> DV device over FireWire]
   [CMonitor  -> DV decoder -> Video Renderer -> IDC_VIDEO]
```

The preview throttling directions are **deliberately opposite** in the two
modes.
During capture, preview is sent when the queue is below half capacity —
meaning the disk is keeping up and there is spare CPU.
During recording, preview is sent when the queue is above half capacity —
meaning the file is being read ahead and frames are flowing freely.
This asymmetry ensures that preview never starves the primary data path.
See [threading.md § Preview throttling](threading.md) for details.
