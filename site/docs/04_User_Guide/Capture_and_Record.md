# Capture and Record

WinDV's main interface is a single resizable dialog with a tab control
that switches between Capture and Record modes.

---

## Dialog Layout

`CDVToolsDlg` is the application's only top-level window, derived from
`CDialog`.
There is no document/view pattern and no frame window.
The dialog is resizable; the initial size sets the minimum size constraint.

The dialog contains one instance of `CDV` (the `m_video` member) mapped
to the `IDC_VIDEO` picture control.
`CDV` inherits `CStatic`, so it is a legitimate child window that can host
the preview renderer as a grandchild window.

### Key Controls

| Control ID | Type | Purpose |
| --- | --- | --- |
| `IDC_VIDEO` | `CDV` (CStatic) | Preview window host |
| `IDC_TOOL_TAB` | `CToolTab` | Owner-draw tab control |
| `IDC_VSRC` | Static text | Current capture source device name |
| `IDC_VSRC_SEL` | Button | Opens device selection dialog |
| `IDC_FDST` | `CDropFilesEdit` | Capture destination filename base |
| `IDC_FDST_SEL` | Button | Opens file browse dialog |
| `IDC_FSRC` | `CDropFilesEdit` | Record source file list |
| `IDC_FSRC_SEL` | Button | Opens multi-file browse dialog |
| `IDC_VDST` | Static text | Current record destination device name |
| `IDC_VDST_SEL` | Button | Opens device selection dialog |
| `IDC_CAPTURE` | Button | Toggle capture start/pause |
| `IDC_RECORD` | Button | Toggle record start/pause |
| `IDCANCEL` | Button | Cancel / reset to idle |
| `IDC_CONFIG` | Button | Opens configuration property sheet |
| `IDC_DVCTRL` | Checkbox | Enable/disable tape transport control |
| `IDC_STATUS` | Static text | Primary status message |
| `IDC_STATUS2` | Static text | DV recording timestamp display |
| `IDC_STATUS3` | Static text | Queue fill level display |
| `IDC_COUNTER` | Static text | Elapsed capture/record time |
| `IDC_PICTURE` | Button | Logo (clicking opens About dialog) |

`CDropFilesEdit` is a `CEdit` subclass that accepts file-drop events.
For `IDC_FSRC` (multi-file record source) a `" | "` separator is used
between filenames.
For `IDC_FDST` (single-file capture destination) a transform callback
`CaptureFilenameExtractBase()` strips the extension so the field always
shows only the base name.

---

## Tab Control and Mode Switching

`IDC_TOOL_TAB` is a `CToolTab` (owner-draw) with two items:

| Tab index | Label | Mode |
| --- | --- | --- |
| 0 | "Video Capture" | Capture (FireWire to AVI) |
| 1 | "Video Recording" | Record (AVI to FireWire) |

### Tab-change Mechanics

`TCN_SELCHANGE` on the tab control routes to `OnSelchangeToolTab()`, which:

1. Reads `m_toolTab.GetCurSel()`.
2. For each control in `ctrlProperties[]`, calls
   `ShowWindow(SW_SHOW or SW_HIDE)` based on whether
   `(1 << sel) & tabMask` is non-zero.
3. Calls `InitVideo()` to rebuild the pipeline for the newly active tab.

`InitVideo()` for tab 0 calls `CDV::BuildCapturing()` (live preview begins
immediately).
`InitVideo()` for tab 1 calls `CDV::Destroy()` and displays a prompt.

Pressing the Cancel button or Escape also calls `InitVideo()`, which resets
the pipeline to the idle/paused state for the active tab rather than
closing the dialog.
The default `CDialog::OnCancel()` behaviour (close dialog) is suppressed.

### Tab Item Sizing

`SetToolTabItemSize()` recalculates the width of each tab item so that
all items together fill the tab control width with a half-item gutter on
each side:

```text
itemWidth = tabControlWidth * 2 / (itemCount * 2 + 1)
```

This is called from both `OnInitDialog()` and `OnSize()` so the tabs
resize with the dialog.

---

## Proportional Resize System

All controls in the dialog resize proportionally as the user drags the
window borders.
The system uses a static array of `CtrlProperties` structures defined at
the top of `DVToolsDlg.cpp`:

```cpp
static struct CtrlProperties {
    int id;             // control resource ID
    int dx, dw;         // horizontal left-edge and right-edge percentage anchors
    int dy, dh;         // vertical top-edge and bottom-edge percentage anchors
    int tabMask;        // which tabs show this control
} ctrlProperties[] = { ... };
```

### How the Percentages Work

When `OnSize(cx, cy)` fires:

```text
deltaX = cx - m_originalRect.right    (change from initial width)
deltaY = cy - m_originalRect.bottom   (change from initial height)
```

For each control, the new position is computed as:

```text
new_left   = originalLeft   + (dx  * deltaX) / 100
new_top    = originalTop    + (dy  * deltaY) / 100
new_width  = originalWidth  + ((dw - dx) * deltaX) / 100
new_height = originalHeight + ((dh - dy) * deltaY) / 100
```

`dx` and `dy` anchor the control's **leading edges**;
`dw` and `dh` anchor the **trailing edges**.

- `dx = 0`: left edge is fixed (left-anchored).
- `dx = 100`: left edge tracks the right side (right-anchored, fixed width).
- `dw = 100, dx = 0`: control stretches full width.
- `dy = 100, dh = 100`: control is anchored to the bottom.

Two named percentage constants are defined to describe the layout zones:

```cpp
#define XL 25   // left zone boundary (25% from left)
#define XR 75   // right zone boundary (75% from left, or 25% from right)
```

### Layout Table (Selected Controls)

| Control | dx | dw | Behaviour |
| --- | --- | --- | --- |
| `IDC_VIDEO` | 0 | 100 | Fills entire client area width |
| `IDC_TOOL_TAB` | XL=25 | XR=75 | Centred band; narrows as dialog widens |
| Device/file labels | XL | XL | Fixed-width, pinned to 25% mark |
| Device/file edits | XL | XR | Stretch between 25% and 75% marks |
| Buttons (sel, cfg, action) | XR | XR | Pinned to 75% mark, fixed width |
| `IDC_STATUS` | XL | XR | Full-width status stretches with dialog |
| `IDC_COUNTER`, status2/3 | XR | XR | Pinned to right |

All controls use `dy = 100, dh = 100` so their vertical positions track
100% of the height change, keeping them pinned to the bottom of the dialog.

---

## Status Display and Timer Updates

A 200 ms `WM_TIMER` (timer ID 1) is started by `SetTimer(1, 200, NULL)`
when the pipeline becomes active and stopped by `KillTimer(1)` in
`InitVideo()`.

`OnTimer()` performs two jobs:

**Auto-exit check:**
If `m_exitOnFinish` is set (from the `-exit` command-line flag) and the
pipeline has reached `CDV::Finished`, `OnClose()` is called immediately.

**Status label updates** (only when the text changes to avoid repaints):

| Label | Content |
| --- | --- |
| `m_status` (IDC_STATUS) | Pipeline state; appends dropped-frame count |
| `m_status2` (IDC_STATUS2) | DV timestamp as `DD.MM.'YY HH:MM:SS` |
| `m_counter` (IDC_COUNTER) | Elapsed time as `H:MM:SS.t` |
| `m_status3` (IDC_STATUS3) | Queue fill level; during capture also shows DV error count and percentage |

During active capture (`CDV::Capturing`), `m_status3` shows the queue
load together with the current DV error statistics:

```text
Q:N E:N/X.X%
```

where `N` (first) is the queue slot count, `N` (second) is the number of
frames with at least one video error, and `X.X%` is the error rate.
The error portion is omitted when no errors have been detected yet.

The elapsed time conversion:

```cpp
REFERENCE_TIME t = m_video.GetTime();  // 100-ns units
t /= 1000000;       // -> tenths of a second
int ss = (int)(t % 10); t /= 10;
int s  = (int)(t % 60); t /= 60;
int m  = (int)(t % 60); t /= 60;
txt2.Format("%d:%02d:%02d.%01d", (int)t, m, s, ss);
```

`SetThreadExecutionState(ES_DISPLAY_REQUIRED)` is called in `OnTimer()` to
prevent the monitor from blanking during active capture or recording
sessions.

### Custom Window Messages

WinDV defines several `WM_USER`-based messages for cross-thread
notification.
All are declared in `DShow.h` and routed via `ON_MESSAGE` entries in
`CDVToolsDlg`'s message map.

| Message | Value | Sender | Handler |
| --- | --- | --- | --- |
| `WM_DV_TIMECHANGE` | `+201` | Capturing | `OnDVTimeChange` |
| `WM_DV_LOWDISKSPACE` | `+202` | Capturing | `OnDVLowDiskSpace` |
| `WM_DV_SIGNALLOST` | `+203` | Capturing | `OnDVSignalLost` |
| `WM_DV_CHECK_COMPLETE` | `+204` | Capturing (post-capture) | `OnDVCheckComplete` |

#### OnDVTimeChange

Reads `lParam` as a `time_t` DV recording timestamp and updates
the `m_status2` label.

#### OnDVLowDiskSpace (v1.2.6)

Posted by `CapturingThread` when `GetDiskFreeSpaceEx()` reports fewer
than 500 MB free on the capture destination drive.
`lParam` carries the remaining free space in megabytes.

The handler:

1. Formats the string
   `"WARNING: Low disk space! X MB remaining"` into `m_status`.
2. Calls `MessageBeep(MB_ICONEXCLAMATION)` to produce an audible
   alert.

Capture continues; the handler does not stop or pause the pipeline.
The warning is informational, allowing the user to decide whether to
stop early.

#### OnDVSignalLost (v1.2.7)

Posted by `CapturingThread` when `CDVQueue::GetWithTimeout()` times
out without receiving a frame or an EOS marker, indicating the
FireWire signal was lost.

The handler:

1. Sets `m_status` to `"Signal lost - capture stopped."`.
2. Calls `MessageBeep(MB_ICONEXCLAMATION)`.

The thread has already set the pipeline state to `Finished` before
posting the message, so no additional stop call is required from the
dialog.

#### OnDVCheckComplete (v1.6.0)

Posted by `CapturingThread` after the post-capture phase (AVI integrity
check + SHA-256 + CSV log) completes.
`wParam` is non-zero if the AVI check passed, zero if problems were found.

The handler reads `CDV::GetLastCheckResult()` and `CDV::GetErrorStats()`
to build an extended status bar message:

```text
AVI OK: N frames, PAL | DV: error-free
AVI OK: N frames, NTSC | DV errors: M frames (X.X%), worst: #F (N STA),
    audio: A frames [C CRITICAL]
AVI problem: <reason> | DV errors: ...
```

If the AVI check failed, `MessageBeep(MB_ICONWARNING)` is called.
The full check result is available via `CDV::GetLastCheckResult()` for
programmatic access (e.g., from the CLI `check` command).

---

## Configuration Dialogs

The configuration button (`IDC_CONFIG`) opens a `CPropertySheet` with
two pages:

### CCaptureCfg

Exposes:

- AVI type radio button (Type 1 / Type 2)
- Discontinuity threshold (seconds)
- Maximum frames per file
- Frame decimation factor (`EveryNth`)
- Date/time format string (with history combo)
- Sequence digit count
- Auto-stop on signal loss (`IDC_CHK_AUTOSTOP`, timeout in seconds)
- **SHA-256 checksum** (`IDC_CHK_SHA256`) -- when checked, a
  `sha256sum`-compatible sidecar file (`.sha256`) is written next to each
  captured AVI file after the post-capture phase completes.
  Controls `CDV::m_enableSHA256`.

### CRecordCfg

Exposes:

- AVI prefix list (pipe-delimited paths played before main file list)
- AVI suffix list (pipe-delimited paths played after main file list)
- Preview checkbox (enable/disable preview during recording)

The property sheet is created with `PSH_NOAPPLYNOW` because settings
take effect only when a new pipeline is built.
Live application would require a graph rebuild, which is not implemented.
Changes made in the dialog are copied back to `CDV` and the `CDVToolsDlg`
member variables only when the user confirms with OK.

---

## Supporting Controls

### CToolTab

`CToolTab` is a `CTabCtrl` subclass that overrides `DrawItem()` to draw
tab items using the owner-draw style.
It provides consistent styling across Windows versions without requiring
visual styles.

### CDropFilesEdit

`CDropFilesEdit` is a `CEdit` subclass that registers as a drop target
and handles `WM_DROPFILES`.
It accepts a separator string (for multi-file mode) and an optional
transform callback.

For `IDC_FSRC` (Record source):

- Separator: `" | "`
- Transform: none
- Multiple files dropped are joined with the separator.

For `IDC_FDST` (Capture destination):

- Separator: `""` (single file)
- Transform: `CaptureFilenameExtractBase` strips the extension so only the
  base path is stored.

### CVideoDeviceSel

A simple `CDialog` subclass that shows a list box populated with device
friendly names returned by `GetVideoSrcList()` or `GetVideoDstList()`.
Returns the selected index via `GetSelection()`.
