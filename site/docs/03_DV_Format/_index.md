# DV Format and Frame Parsing

This document describes the DV frame structure as it pertains to WinDV,
specifically the SSYB subcode area that carries recording timestamps, and
how `GetDVRecordingTime()` in `DV.cpp` extracts those timestamps for use
in filename generation and automatic file splitting.

---

## DV Frame Structure

A raw DV frame is a sequence of **DIF (Digital Interface Format) blocks**,
each 80 bytes.
The total frame size depends on the TV standard:

| Standard | DIF sequences | Blocks per sequence | Block size | Total bytes |
| --- | --- | --- | --- | --- |
| NTSC | 10 | 150 | 80 | 120,000 |
| PAL | 12 | 150 | 80 | 144,000 |

WinDV determines the standard by testing `len >= 144000`:

```cpp
int seqCount = len >= 144000 ? 12 : 10;
```

Frames of any other size are rejected by `GetDVRecordingTime()`:

```cpp
if (len != 144000 && len != 120000) return -1;
```

The two sizes come from the DV standard (IEC 61834):

- NTSC: 29.97 fps, 10 sequences x 150 blocks x 80 bytes = 120,000 bytes.
- PAL: 25 fps, 12 sequences x 150 blocks x 80 bytes = 144,000 bytes.

---

## DIF Sequence and Block Layout

Each DIF sequence of 150 blocks is structured as follows:

| Block index | Type | Count |
| --- | --- | --- |
| 0 | Header | 1 |
| 1-2 | Subcode (SSYB) | 2 |
| 3-5 | VAUX (video auxiliary) | 3 |
| 6-149 | Interleaved audio + video | 144 |

Blocks 6 through 149 contain **interleaved** audio and video data.
Audio blocks are not consecutive; they appear at every 16th position
starting at index 6:

```text
Audio block indices: 6, 22, 38, 54, 70, 86, 102, 118, 134  (9 blocks)
Video blocks fill the remaining 135 positions between them.
```

Each audio block is identified by its SCT field (byte 0, bits 7-5) equal
to 3 (SCT_AUDIO = 011 binary).
Each video block has SCT = 4 (SCT_VIDEO = 100 binary).

Per-sequence totals:

| Block type | Count per sequence |
| --- | --- |
| Audio | 9 |
| Video | 135 |
| Header + Subcode + VAUX | 6 |
| **Total** | **150** |

The subcode blocks at indices 1 and 2 each hold 6 sync packets, each
8 bytes long (including a 3-byte sync-packet header), yielding 5 bytes
of pack payload per packet.

---

## SSYB Subcode Area

`GetSSYBPack()` in `DV.cpp` scans all subcode packets in all DIF
sequences to find the first packet whose ID byte matches a given pack
number.

The address of sync packet `k` in subcode block `j` of sequence `i` is:

```text
offset = i * 150 * 80     <- start of DIF sequence i
       + 1 * 80            <- skip the sequence header block (block 0)
       + j * 80            <- select subcode block j (j = 0 or 1)
       + 3                 <- skip the 3-byte DIF block header
       + k * 8             <- select sync packet k (0 through 5)
       + 3                 <- skip the 3-byte sync-packet header
```

In code:

```cpp
const unsigned char *s =
    &data[i * 150 * 80 + 1 * 80 + j * 80 + 3 + k * 8 + 3];
```

`s[0]` is the pack ID byte.
`s[1]` through `s[4]` are the four data bytes of the pack payload.

The scan iterates over:

- `i` from 0 to `seqCount - 1` (10 or 12 sequences)
- `j` from 0 to 1 (two subcode blocks per sequence)
- `k` from 0 to 5 (six packets per subcode block)

It returns the first matching pack and stops.
The recording date and time are typically written in sequence 0, subcode
block 0, packets 2 and 3 (or nearby), but the spec allows them anywhere
in the SSYB area.
Scanning all packets ensures any valid recording produces a result.

---

## Pack 0x62 (Date) and 0x63 (Time)

### Pack 0x62 -- Recording Date

```text
Byte 0: pack ID = 0x62
Byte 1: flag byte (unused by WinDV)
Byte 2: day   -- BCD, bits 5-4 = tens digit, bits 3-0 = units digit
Byte 3: month -- BCD, bit  4   = tens digit, bits 3-0 = units digit
Byte 4: year  -- BCD, bits 7-4 = tens digit, bits 3-0 = units digit (2-digit)
```

### Pack 0x63 -- Recording Time

```text
Byte 0: pack ID = 0x63
Byte 1: flag byte (unused by WinDV)
Byte 2: seconds -- BCD, bits 6-4 = tens digit, bits 3-0 = units digit
Byte 3: minutes -- BCD, bits 6-4 = tens digit, bits 3-0 = units digit
Byte 4: hours   -- BCD, bits 5-4 = tens digit, bits 3-0 = units digit
```

The upper bits of the tens nibble carry flags or parity in the DV standard;
the masks below strip those flag bits to isolate the numeric value.

---

## BCD Decoding

BCD (Binary-Coded Decimal) packs each decimal digit into a 4-bit nibble.
The pattern for each field is:

```text
value = (raw & 0xF) + 10 * ((raw >> 4) & mask)
```

where `mask` limits the tens nibble to its valid range:

| Field | Valid range | Tens mask | Reason |
| --- | --- | --- | --- |
| Seconds | 0-59 | 0x7 | Max tens = 5, fits in 3 bits |
| Minutes | 0-59 | 0x7 | Same as seconds |
| Hours | 0-23 | 0x3 | Max tens = 2, fits in 2 bits |
| Year | 0-99 | 0xF | Full nibble used |
| Month | 1-12 | 0x1 | Max tens = 1, fits in 1 bit |
| Day | 1-31 | 0x3 | Max tens = 3, fits in 2 bits |

The source code (DV.cpp):

```cpp
sec   = (sec  & 0xf) + 10 * ((sec  >> 4) & 0x7);
min   = (min  & 0xf) + 10 * ((min  >> 4) & 0x7);
hour  = (hour & 0xf) + 10 * ((hour >> 4) & 0x3);
year  = (year & 0xf) + 10 * ((year >> 4) & 0xf);
month = (month & 0xf) + 10 * ((month >> 4) & 0x1);
day   = (day  & 0xf) + 10 * ((day  >> 4) & 0x3);
```

---

## Y2K Pivot Logic

The DV standard stores only a 2-digit year (00-99).
WinDV applies a fixed pivot at 50 to convert to a 4-digit year:

```cpp
if (year < 50)
    year += 2000;  // 00-49 -> 2000-2049
else
    year += 1900;  // 50-99 -> 1950-1999
```

This is appropriate for MiniDV tapes recorded between 1995 and 2025, which
covers the entire practical lifespan of the DV format.
Tapes from 1950-1994 or after 2049 would require a different pivot, but
such tapes do not exist in the DV format.

After decoding, the values are assembled into a `struct tm` and passed to
`mktime()` to produce a `time_t` (seconds since the Unix epoch):

```cpp
recDate.tm_sec  = sec;
recDate.tm_min  = min;
recDate.tm_hour = hour;
recDate.tm_mday = day;
recDate.tm_mon  = month - 1;   // tm_mon is 0-based; DV month is 1-based
recDate.tm_year = year - 1900; // tm_year is years since 1900
recDate.tm_isdst = -1;         // let mktime() determine DST
return mktime(&recDate);
```

`mktime()` uses the local timezone.
Tapes recorded in a different timezone will have timestamps in local time
relative to the recording location.

---

## How GetDVRecordingTime() Is Used

### Filename Generation

`CAVIWriter` stores the first valid `time_t` from a capture session in
`m_dvtime`.
When the writer is closed (in the destructor), `GetCaptureFilename()` uses
`m_dvtime` as the seed for the date/time suffix:

```text
<base>.<strftime(m_dtformat, m_dvtime)>.<NNN>.avi
```

If the first few frames have no valid SSYB timestamp (e.g., the tape head
has just engaged), `m_dvtime` starts at 0 (no timestamp).
`CapturingThread` backfills it on the first frame that does carry a valid
time:

```cpp
if (dvTime && !m_aviWriter->m_dvtime) {
    m_aviWriter->m_dvtime = dvTime;
}
```

This means the filename reflects the recording time at which the actual
footage starts, not the wall-clock time at which capture began.
If no valid DV timestamp is ever received, `GetCaptureFilename()` falls
back to `time(NULL)` (wall-clock time).

### Automatic File Splitting on Timestamp Discontinuity

`CapturingThread` computes the absolute delta between consecutive DV
timestamps:

```cpp
deltaDVTime = newDVTime - oldDVTime;
if (deltaDVTime < 0) deltaDVTime = -deltaDVTime;
```

If `deltaDVTime > m_discontinuityTreshold` (default: 1 second), a tape
cut or a recording gap has been detected, and the current `CAVIWriter` is
closed and a new one opened.
This splits the output into separate files at natural tape boundaries,
with each file named with the recording timestamp of its first frame.

Setting `m_discontinuityTreshold` to 0 disables discontinuity-based
splitting entirely; only the frame count limit (`m_maxAVIFrames`) will
trigger splits.

### UI Display

`CapturingThread` and `RecordingThread` both post `WM_DV_TIMECHANGE` to
`CDVToolsDlg` whenever the decoded `dvTime` value changes.
`CDVToolsDlg::OnDVTimeChange()` formats the `time_t` as
`"DD.MM.'YY HH:MM:SS"` using `strftime` and displays it in `m_status2`.
This gives the user real-time feedback on the tape's original recording
date and time as the tape plays.

---

## STA Error Fields and Error Detection

`DVError.cpp` / `DVError.h` extend WinDV's understanding of DV frames
beyond timestamps.
For full documentation of the error detection module, see
[DV Error Detection](../02_Architecture/DV_Error_Detection.md).
