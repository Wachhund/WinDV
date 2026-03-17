# UI Layout

This document describes the structure of the main application dialog
(`CDVToolsDlg`), the tab control that switches between Capture and Record
modes, the proportional resize system, registry persistence, status
display, and the configuration dialogs.

Related reading:
[architecture.md](architecture.md) for how `CDVToolsDlg` interacts with
the `CDV` orchestrator.

---

## Table of Contents

1. [Dialog-based architecture](#1-dialog-based-architecture)
2. [Tab control and mode switching](#2-tab-control-and-mode-switching)
3. [Proportional resize system](#3-proportional-resize-system)
4. [Registry persistence](#4-registry-persistence)
5. [Status display and timer updates](#5-status-display-and-timer-updates)
6. [Configuration dialogs](#6-configuration-dialogs)
7. [Supporting controls](#7-supporting-controls)

---

## 1. Dialog-based architecture

`CDVToolsDlg` is the application's only top-level window, derived from
`CDialog`.
There is no document/view pattern and no frame window.
The dialog is resizable; the initial size sets the minimum size constraint
via `OnGetMinMaxInfo()`.

The dialog contains one instance of `CDV` (the `m_video` member) mapped
to the `IDC_VIDEO` picture control.
`CDV` inherits `CStatic`, so it is a legitimate child window that can host
the preview renderer as a grandchild window.

Key controls:

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

## 2. Tab control and mode switching

`IDC_TOOL_TAB` is a `CToolTab` (owner-draw) with two items:

| Tab index | Label | Mode |
| --- | --- | --- |
| 0 | "Video Capture" | Capture (FireWire to AVI) |
| 1 | "Video Recording" | Record (AVI to FireWire) |

### Tab-change mechanics

For each tab item, `OnInitDialog()` creates an invisible `CButton` at
`IDC_TAB_CHANGE + tabIndex` with zero size.
These buttons exist solely as MFC routing targets for accelerator keys;
they are never displayed.

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

### Tab item sizing

`SetToolTabItemSize()` recalculates the width of each tab item so that
all items together fill the tab control width with a half-item gutter on
each side:

```text
itemWidth = tabControlWidth * 2 / (itemCount * 2 + 1)
```

This is called from both `OnInitDialog()` and `OnSize()` so the tabs
resize with the dialog.

---

## 3. Proportional resize system

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

### How the percentages work

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

### Layout table (selected controls)

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

### Initial calibration

`m_originalRect` (a `RECT`) captures the client area dimensions at dialog
creation.
`m_originalRects[]` captures each control's initial client-relative
bounding box.
Both are populated in `OnInitDialog()` before any resize occurs.

The window's initial dimensions from `GetWindowRect()` set
`m_minWidth` / `m_minHeight`, enforced in `OnGetMinMaxInfo()`.

---

## 4. Registry persistence

All settings are saved in `OnClose()` and restored in `OnInitDialog()`.
The registry root is `HKCU\Software\Petr Mourek\WinDV`, set by
`SetRegistryKey("Petr Mourek")` in `CWinDVApp::InitInstance()`.

**Main window (section `"MainWindow"`):**

| Value | Type | Description |
| --- | --- | --- |
| `X`, `Y`, `W`, `H` | `DWORD` | Window position and size from `m_lastRect` |
| `SelectedTool` | `DWORD` | Last active tab index (0 or 1) |
| `DVControlEnabled` | `DWORD` | DV transport control checkbox state |
| `WorkingDirectory` | `SZ` | Current working directory for relative paths |

`m_lastRect` is updated in `OnMove()` and `OnSize()` whenever the window
is in the normal (non-iconic, non-maximised) state.
On first launch (`W == 0 && H == 0`) the About dialog is shown
automatically via `PostMessage(WM_SYSCOMMAND, IDM_ABOUTBOX)`.

**Capture (section `"Capture"`):**

| Value | Type | Default | Description |
| --- | --- | --- | --- |
| `DVDevice` | `SZ` | `Microsoft DV Camera and VCR` | Capture source device |
| `File` | `SZ` | | Capture destination base path |
| `Type2AVI` | `DWORD` | 1 | AVI format (0=Type 1, 1=Type 2) |
| `DiscontinuityTreshold` | `DWORD` | 1 | Seconds before DV timestamp split |
| `MaxAVIFrames` | `DWORD` | 22500 | Frames per file (25 fps x 15 min) |
| `EveryNth` | `DWORD` | 1 | Frame sub-sampling (1 = every frame) |
| `DateTimeFormat` | `SZ` | `%y-%m-%d_%H-%M` | strftime format for suffix |
| `DateTimeFormatHistory` | `SZ` | (presets) | Newline-delimited history |
| `SuffixDigits` | `DWORD` | 2 | Zero-padded digit width for seq no. |

**Record (section `"Record"`):**

| Value | Type | Default | Description |
| --- | --- | --- | --- |
| `DVDevice` | `SZ` | `Microsoft DV Camera and VCR` | Record dest. device |
| `File` | `SZ` | | Last record source file list |
| `AVIPrefix` | `SZ` | | Pipe-delimited leader AVI files |
| `AVISuffix` | `SZ` | | Pipe-delimited trailer AVI files |
| `Preview` | `DWORD` | 1 | Show preview during recording |

---

## 5. Status display and timer updates

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
| `m_status3` (IDC_STATUS3) | Queue fill level as `Q:N` |

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

---

## 6. Configuration dialogs

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

## 7. Supporting controls

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
