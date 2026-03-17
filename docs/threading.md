# Threading Model

WinDV runs up to five concurrent threads.
This document describes each thread's role, the CDVQueue ring buffer that
connects them, the synchronisation primitives used, and the shutdown
sequence that must be respected to avoid deadlocks and data corruption.

Related reading:
[architecture.md § CDV state machine](architecture.md) for state transitions,
[directshow-pipeline.md](directshow-pipeline.md) for the pipeline each thread
operates on.

---

## Table of Contents

1. [Thread inventory](#1-thread-inventory)
2. [CDVQueue ring buffer](#2-cdvqueue-ring-buffer)
3. [Synchronisation primitives](#3-synchronisation-primitives)
4. [Thread lifecycle](#4-thread-lifecycle)
5. [Cross-thread UI updates](#5-cross-thread-ui-updates)
6. [Preview throttling strategy](#6-preview-throttling-strategy)
7. [Shutdown sequence](#7-shutdown-sequence)

---

## 1. Thread inventory

| Thread | Owner | Priority | Notes |
| --- | --- | --- | --- |
| Main (UI) | `CDVToolsDlg` | `HIGH_PRIORITY_CLASS` | Message pump, timer |
| DS streaming | DS runtime | Unmanaged | `Receive()` / `Deliver()` |
| Capturing | `CDV` | `NORMAL` | Drains queue, writes AVI |
| Recording | `CDV` | `NORMAL` | Drains queue, sends to out |
| Monitoring | `CMonitor` | `BELOW_NORMAL` | Rate-limited preview |
| Joiner | `CAVIJoiner` | `NORMAL` | Sequential file transitions |

Only one of CapturingThread or RecordingThread is active at a time
(they are mutually exclusive modes).
JoinerThread is only active during a record session.
MonitoringThread is active whenever `CMonitor` is alive (both modes).
The DirectShow streaming thread is managed entirely by the DirectShow
runtime and is not created or joined by WinDV directly.

**Why `HIGH_PRIORITY_CLASS`?**
DV runs at a fixed 25 fps (PAL) or 29.97 fps (NTSC).
A single missed frame cannot be recovered.
Raising the process priority class reduces the likelihood that the OS
scheduler preempts WinDV during the narrow window between frame arrival
and disk write.

---

## 2. CDVQueue ring buffer

`CDVQueue` is a fixed-capacity FIFO ring buffer for raw DV frames.
It bridges the DirectShow streaming thread (producer) with the
CapturingThread or RecordingThread (consumer).

### Memory layout

```text
m_buffers[]  (single contiguous allocation)
+----------+----------+----------+----------+
| Buffer 0 | Buffer 1 | Buffer 2 |   ...    |
+----------+----------+----------+----------+
  ^                                 ^
  m_queue[0]                        m_queue[N]

struct Buffer {
    REFERENCE_TIME duration;   // frame duration (100-ns units)
    int            len;        // valid bytes in data[]
    BYTE           data[1];    // variable-length; actual size = m_dataSize
};
```

The queue is constructed with `queueSize + 1` slots so that `m_load`
unambiguously represents the number of filled slots.
A classic two-pointer ring (head == tail = full OR empty?) requires an
extra sentinel; using an explicit `m_load` counter avoids that ambiguity.

Default parameters in `CDV::BuildCapturing()` and `CDV::BuildRecording()`:

```cpp
m_queue = new CDVQueue(100, type.GetSampleSize());
```

- 100 slots at PAL frame size (144,000 bytes) = ~13.8 MB total.
- At 25 fps this represents 4 seconds of buffering.

### Producer: `CDVQueue::Put(duration, data, len)`

Called on the DirectShow streaming thread via `CDV::HandleFrame()`.

```text
data != NULL:
  Acquire m_cs
    if m_load < m_queueSize - 1:
      copy data into m_queue[m_tail]->data
      advance m_tail, increment m_load
      signal m_evGet  (wake consumer if blocked)
    else:
      release m_cs, wait on m_evPut (ring full, backpressure)
      retry

data == NULL (end-of-stream):
  set m_end = true
  signal both m_evGet and m_evPut  (wake any blocked thread)
  return immediately
```

If the ring is full the producer blocks on `m_evPut`.
This propagates backpressure upstream: the FireWire driver's own internal
buffer absorbs the brief stall.
If that buffer also fills, frames are dropped at the driver level and
counted by `IAMDroppedFrames`.

### Consumer: `CDVQueue::Get(duration, data, len)`

Called on CapturingThread or RecordingThread.

```text
Acquire m_cs
  if m_load > 0:
    read m_queue[m_head]
    advance m_head, decrement m_load
    signal m_evPut  (wake producer if blocked)
    return true
  else:
    if m_end: return false  (EOS, ring empty)
    release m_cs, wait on m_evGet
    retry
```

The returned `*data` pointer points **directly into the ring slot**.
The caller must consume or copy the data before calling `Get()` again,
because the next `Put()` may overwrite the same slot.
In practice, `CapturingThread` passes the pointer directly to
`CAVIWriter::HandleFrame()` (which calls `CopyMemory` internally) before
calling `Get()` again.

---

## 3. Synchronisation primitives

| Primitive | Class | Protects |
| --- | --- | --- |
| `CCritSec m_cs` | `CDVQueue` | `m_head`, `m_tail`, `m_load` |
| `CEvent m_evGet` | `CDVQueue` | Signals consumer when a slot is filled |
| `CEvent m_evPut` | `CDVQueue` | Signals producer when a slot is freed |
| `CEvent m_ev` | `CAVIJoiner` | Signals JoinerThread on EOS |
| `CEvent m_ev` | `CMonitor` | Signals MonitoringThread on frame ready |
| `CCritSec m_cs` | `CDV` | Protects `m_captureFilename` |
| `CAutoLock` | Both | RAII guard for CCritSec |

`CEvent` in the DirectShow BaseClasses wraps a Win32 manual-reset event.
`CSingleLock::Lock()` on a `CEvent` calls `WaitForSingleObject(INFINITE)`.

`CAutoLock` is the RAII lock guard for `CCritSec`.
It is used as a local variable:

```cpp
{
    CAutoLock lock(&m_cs);
    // m_cs held here
}   // m_cs released at scope exit
```

---

## 4. Thread lifecycle

All application-managed threads follow the same pattern:

```cpp
m_thread = AfxBeginThread(
    ThreadProc,          // static UINT __cdecl trampoline
    this,                // parameter (cast to correct type in body)
    THREAD_PRIORITY_xxx,
    0,                   // stack size (0 = default)
    CREATE_SUSPENDED     // start suspended
);
m_thread->m_bAutoDelete = FALSE;   // prevent automatic deletion on exit
m_thread->ResumeThread();          // start running
```

**Why `CREATE_SUSPENDED` followed by `ResumeThread()`?**
`m_bAutoDelete = FALSE` must be set before the thread runs, otherwise the
`CWinThread` object could be deleted by the MFC framework before the owner
has a chance to call `WaitForSingleObject()` and `delete m_thread`.
The suspended start gives a window to set the flag atomically.

**Why `m_bAutoDelete = FALSE`?**
The default `TRUE` causes MFC to `delete this` when the thread proc
returns.
With `FALSE`, ownership stays with the creating object
(`CDV`, `CMonitor`, `CAVIJoiner`), which explicitly calls
`WaitForSingleObject(m_thread->m_hThread, INFINITE)` followed by
`delete m_thread`.
This guarantees the thread has fully exited before any shared state is
freed.

---

## 5. Cross-thread UI updates

All Win32 window operations must happen on the thread that created the
window — the main UI thread.

`CDV::CapturingThread()` and `CDV::RecordingThread()` post
`WM_DV_TIMECHANGE` to the parent window whenever the DV recording
timestamp changes:

```cpp
GetParent()->PostMessage(WM_DV_TIMECHANGE, 0, dvTime);
```

`PostMessage` is non-blocking: it places the message in the window's
message queue and returns immediately.
The main thread processes it in `CDVToolsDlg::OnDVTimeChange()`,
which reads `lParam` as a `time_t` and updates `m_status2`.

`WM_DV_TIMECHANGE` is defined as `WM_USER + 201` and routed via
`ON_MESSAGE(WM_DV_TIMECHANGE, OnDVTimeChange)` in the message map.

Status labels (`m_status`, `m_counter`, `m_status3`) are updated by the
200 ms UI timer (`OnTimer`), which calls `CDV::GetState()`,
`CDV::GetTime()`, `CDV::GetDropped()`, and `CDV::GetQueueLoad()`.
These accessors return `int` / `REFERENCE_TIME` values; their reads are
not individually lock-protected because they are updated by a single
worker thread and consumed by the UI thread at a much lower rate.
The only lock-protected cross-thread value is `m_captureFilename`
(guarded by `CDV::m_cs`).

---

## 6. Preview throttling strategy

Preview is deliberately deprioritised relative to the primary data path.
The throttling direction differs between the two modes.

### Capture mode (CapturingThread)

```cpp
if (m_queue->m_load < m_queue->m_queueSize / 2)
    m_monitor->HandleFrame(duration, buffer, len);
```

Preview is sent only when the queue is **below half full**.
A queue above half capacity means the disk is falling behind;
skipping preview prevents the DV decoder and video renderer from
competing with disk I/O for CPU time.

### Record mode (RecordingThread)

```cpp
if (m_recordPreview) {
    if (m_queue->m_end || m_queue->m_load > m_queue->m_queueSize / 2)
        m_monitor->HandleFrame(duration, buffer, len);
}
```

Preview is sent only when the queue is **above half full** (or EOS).
During recording, the queue is filled from AVI files on disk; a full
queue means the source is reading ahead of the DV device's consumption.
A nearly-empty queue means frames are being consumed as fast as they
arrive; skipping preview prevents MonitoringThread from stalling the
delivery to the DV device.

### MonitoringThread rate limiter

Even when the calling thread does offer a frame to `CMonitor`,
`MonitoringThread` imposes an additional rate limit of approximately
5 fps via a dynamic sleep:

```cpp
ticks = GetTickCount() - ticks;
Sleep(ticks < 200 ? ticks + 10 : 200);
```

If less than 200 ms has elapsed since the last delivery, it sleeps the
remaining time (plus 10 ms to avoid busy-looping).
If more than 200 ms has elapsed, it sleeps the full 200 ms maximum.

---

## 7. Shutdown sequence

`CDV::Destroy()` performs a carefully ordered teardown to avoid deadlocks
and ensure all data is flushed to disk:

```text
1.  m_state = Idle
    CapturingThread / RecordingThread check m_state at the top of their
    loops; setting Idle causes them to exit on the next iteration.

2.  m_queue->Put(-1, NULL, 0)
    Sends the EOS sentinel.  If the worker thread is blocked in
    CDVQueue::Get(), it will wake up and see m_end = true.

3.  m_aviJoiner->Stop()  or  m_dvInput->Stop()
    Stops the frame source so no new frames enter the queue.
    Stop() for CAVIJoiner sets m_stopping and signals m_ev to unblock
    JoinerThread; it then waits for JoinerThread to exit.

4.  WaitForSingleObject(m_thread->m_hThread, INFINITE)
    Blocks until the worker thread (CapturingThread or RecordingThread)
    has exited cleanly.
    delete m_thread.

5.  delete m_aviJoiner / delete m_dvInput
6.  delete m_aviWriter
    CAVIWriter destructor sends DeliverEndOfStream(), waits up to 5 s
    for EC_COMPLETE, then renames the temp file to its final name.
7.  delete m_dvOutput
    CDVOutput destructor waits up to 5 s for EC_COMPLETE.
8.  delete m_monitor
    CMonitor destructor releases IVideoWindow, signals m_ev to wake
    MonitoringThread, waits for MonitoringThread to exit, then deletes
    it.
9.  delete m_queue
10. Reset m_dropped, m_counter, m_time, m_captureTime.
```

The ordering in steps 5–8 is critical:

- `m_aviWriter` must be deleted before `m_monitor` because `CapturingThread`
  may still be writing to `m_aviWriter` while consuming from the queue.
  Step 4 guarantees the thread is done before either is deleted.
- `m_dvOutput` and `m_monitor` both wait internally for their own threads
  (`COutputQueue` delivery thread and `MonitoringThread`), so they are safe
  to delete after the worker thread has exited.
- `m_queue` is deleted last because `m_monitor->HandleFrame()` may be
  called by CapturingThread right up to the moment the thread exits.
