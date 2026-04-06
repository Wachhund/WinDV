# DV Error Detection

`DVError.cpp` / `DVError.h` implement per-frame DV error detection based on
the STA (error concealment status) fields defined in IEC 61834.
The module has no DirectShow or MFC dependency; it operates on raw DV frame
buffers.

This module is compatible with the error metrics used by
[DVRescue](https://mediaarea.net/DVRescue) and DVAnalyzer.

---

## Data Structures

### FrameErrorInfo -- Result for a Single Frame

```cpp
struct FrameErrorInfo {
    DWORD dwVideoErrorBlocks;   /* STA != 0 in video DIF blocks */
    DWORD dwAudioErrorBlocks;   /* parity errors in audio DIF blocks */
    DWORD dwVideoErrorsEven;    /* video errors in even DIF sequences (0,2,4,...) */
    DWORD dwVideoErrorsOdd;     /* video errors in odd DIF sequences (1,3,5,...) */
    DWORD dwVideoSTA_F;         /* count of STA=0xF (uncompensated, worst type) */
};
```

| Field | Description |
| --- | --- |
| `dwVideoErrorBlocks` | STA != 0 in any video DIF block |
| `dwAudioErrorBlocks` | AAUX pack ID == 0xFF in any audio DIF block |
| `dwVideoErrorsEven` | Video errors in even DIF sequences (0, 2, 4, ...) |
| `dwVideoErrorsOdd` | Video errors in odd DIF sequences (1, 3, 5, ...) |
| `dwVideoSTA_F` | STA == 0xF (uncompensated, worst-case error) count |

### ErrorStats -- Cumulative Statistics for a Capture Session

```cpp
struct ErrorStats {
    DWORD dwTotalFrames;
    DWORD dwFramesWithVideoErrors;
    DWORD dwFramesWithAudioErrors;
    DWORD dwTotalVideoErrorBlocks;
    DWORD dwTotalAudioErrorBlocks;
    DWORD dwWorstFrameNumber;
    DWORD dwWorstFrameErrors;
    DWORD dwTotalSTA_F;
    DWORD dwVideoErrorsEven;
    DWORD dwVideoErrorsOdd;
};
```

| Field | Description |
| --- | --- |
| `dwTotalFrames` | Total frames analyzed |
| `dwFramesWithVideoErrors` | Frames with at least one video STA error |
| `dwFramesWithAudioErrors` | Frames with at least one audio error |
| `dwTotalVideoErrorBlocks` | Sum of all video error blocks |
| `dwTotalAudioErrorBlocks` | Sum of all audio error blocks |
| `dwWorstFrameNumber` | 0-based index of the frame with most errors |
| `dwWorstFrameErrors` | Error count of that worst frame |
| `dwTotalSTA_F` | Total uncompensated errors across the session |
| `dwVideoErrorsEven` | Total even-sequence video errors |
| `dwVideoErrorsOdd` | Total odd-sequence video errors |

---

## Functions

```cpp
FrameErrorInfo AnalyzeDVFrame(const BYTE *data, int len);
void           ResetErrorStats(ErrorStats *stats);
void           AccumulateErrorStats(ErrorStats *stats,
                                   const FrameErrorInfo *frame,
                                   DWORD frameNumber);
```

`AnalyzeDVFrame()` accepts frames of exactly 120,000 bytes (NTSC) or
144,000 bytes (PAL) and returns all-zero `FrameErrorInfo` for any other
length.

---

## DIF Block Scanning

Within each DIF sequence (150 blocks of 80 bytes), blocks 6 through 149
contain interleaved audio and video data.
The SCT field (byte 0, bits 7-5) identifies each block type:

- **Video blocks (SCT=4):** the STA field is the upper nibble of byte 3.
  Any non-zero STA value is counted as an error.
  STA == 0xF (uncompensated) is additionally counted in `dwVideoSTA_F`.
- **Audio blocks (SCT=3):** byte 3 is the AAUX pack ID.
  A value of 0xFF indicates the audio data could not be read from tape
  (DVRescue/DVAnalyzer convention).

Audio blocks are **interleaved** -- they appear at fixed positions within
the block stream, not as a consecutive group:

```text
Index:  6   22  38  54  70  86  102  118  134   (9 audio blocks)
Stride: every 16th block starting at index 6
```

Between each pair of audio blocks are 15 video blocks, giving
135 video blocks per DIF sequence.

Maximum possible error counts per frame:

| Standard | Video blocks | Audio blocks |
| --- | --- | --- |
| NTSC (10 seqs) | 1350 | 90 |
| PAL (12 seqs) | 1620 | 108 |

---

## STA Field Values

Every video DIF block (SCT=4) carries a 4-bit STA (STAtus) field in the
upper nibble of byte 3:

```text
Byte 3 of a video DIF block:
  bits 7-4 : STA  (error concealment status)
  bits 3-0 : (other data)
```

STA values per IEC 61834:

| Value | Meaning |
| --- | --- |
| 0x0 | No error -- data decoded normally |
| 0x1 | Concealed by simple interpolation |
| 0x2 | Concealed by copying from the previous frame |
| 0x4 | Concealed by copying from the next frame |
| 0xA | Concealed -- unspecified method |
| 0xC | Concealed -- unspecified method |
| 0xF | **Uncompensated error** (worst case -- visible artifact) |

Any STA value other than 0x0 indicates that the corresponding DCT block
could not be decoded cleanly and had to be concealed.
`AnalyzeDVFrame()` counts all non-zero STA values as video errors, and
separately counts 0xF values as `dwVideoSTA_F` (uncompensated, critical).

---

## Audio Error Detection

Audio DIF blocks (SCT=3) use a different mechanism.
Byte 3 of an audio DIF block is the AAUX pack ID.
Per the DVRescue/DVAnalyzer convention, a pack ID of `0xFF` indicates
the audio data in that block could not be read from tape.

Maximum possible audio error counts per frame:

| Standard | Sequences | Audio blocks/seq | Max audio errors |
| --- | --- | --- | --- |
| NTSC | 10 | 9 | 90 |
| PAL | 12 | 9 | 108 |

---

## Even/Odd DIF Sequence Analysis

DV camcorders record with two rotating video heads.
Even-numbered DIF sequences (0, 2, 4, ...) are recorded by one head;
odd-numbered sequences (1, 3, 5, ...) by the other.

Comparing `dwVideoErrorsEven` and `dwVideoErrorsOdd` in `ErrorStats`
(or `FrameErrorInfo`) can reveal one-sided head wear or head-switching
problems:

- **Balanced errors** (even ~ odd) -- dropout is random, likely due to
  tape condition rather than a specific head.
- **Imbalanced errors** (one side dominates) -- one head may be dirty,
  worn, or misaligned.

This is the same head-wear diagnostic approach used by DVRescue and
DVAnalyzer.

---

## Integration in CapturingThread

`CDV::CapturingThread()` calls `AnalyzeDVFrame()` on every dequeued frame,
accumulates the result with `AccumulateErrorStats()` under `m_cs`, and
exposes the running totals via `CDV::GetErrorStats()` (which copies under
lock for safe access from the UI timer thread).

Error analysis happens on every frame regardless of whether a `CAVIWriter`
is open (i.e., even in `CapturePaused` state when no file is being written).

For the synchronization details see
[Threading Model](Threading_Model.md#synchronisation-primitives).

---

## Status Bar Display

During active capture, the UI timer formats the error statistics into
`m_status3`:

```text
Q:N E:N/X.X%
```

where `N` (first) is the queue slot count, `N` (second) is the number of
frames with at least one video error, and `X.X%` is the error rate.
The error portion is omitted when no errors have been detected yet.

After capture completes, the `WM_DV_CHECK_COMPLETE` handler builds an
extended status summary:

```text
AVI OK: N frames, PAL | DV: error-free
AVI OK: N frames, NTSC | DV errors: M frames (X.X%), worst: #F (N STA),
    audio: A frames [C CRITICAL]
AVI problem: <reason> | DV errors: ...
```
