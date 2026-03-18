# Changelog

All notable changes to WinDV are documented in this file.

## 1.2.7 - 2026-03-18

End-of-signal auto-detection for unattended capture.

### Added

- Automatically stop capture when no DV frames arrive
  within 5 seconds (end of tape or device disconnect).
- CDVQueue::GetWithTimeout() for timeout-based frame
  retrieval.
- CDV::m_autoStopTimeout (default 5000 ms, 0 to disable).
- WM_DV_SIGNALLOST message with status bar notification
  and alert sound.

## 1.2.6 - 2026-03-18

Low disk space warning during capture.

### Added

- Monitor free disk space approximately once per minute
  during active capture.
- When free space drops below 500 MB, display warning in
  status bar and play alert sound.
- WM_DV_LOWDISKSPACE message for UI notification.

## 1.2.5 - 2026-03-18

Complete error checking across all DirectShow pipeline code.

### Fixed

- CInputGraph::Run() — check m_MC->Run() return value.
- CInputGraph::GetMediaType() — check ConnectionMediaType().
- CAVIReader — check AddSourceFilter() and CoCreateInstance()
  for both AVI Splitter and DV Mux.
- CMonitor constructor — check RenderStream(), put_Owner(),
  put_WindowStyle(), and m_MC->Run().
- CMonitor::HandleFrame() — GetPointer() null check.
- COutputGraph::HandleFrame() — TRACE on Deliver() failure.

## 1.2.4 - 2026-03-17

Bugfixes, documentation, and code quality improvements.
Adopted and completed by new maintainer.

### Fixed

- Directories containing dots are now handled properly in
  capture filename generation. Previously, a path like
  `C:\My.Docs\video.avi` was truncated at the first dot.
- Only DV video devices are now listed in device selection.
  Webcams and USB capture cards are filtered out by checking
  for MEDIATYPE_Interleaved support.
- AVI max frames validation upper bound raised from 1,000,000
  to UINT_MAX, fixing unintended video segmentation on long
  captures (GitHub issue #2).
- Fixed int/UINT type mismatch for m_discontinuityTreshold,
  m_maxAVIFrames, and m_everyNth to match DDV_MinMaxUInt
  validation.
- Fixed potential crash in CAVIWriter when IFileSinkFilter2
  QueryInterface fails (null-pointer dereference).
- MoveFile in CAVIWriter destructor now checks for errors
  instead of silently failing.
- Buffer overflow protection added to CDVQueue::Put() by
  clamping frame length to allocated buffer size.
- GetPointer() return value is now checked in
  COutputGraph::HandleFrame() to prevent null-pointer writes.

### Added

- Comprehensive inline code comments across all source files.
- Developer documentation in docs/ directory:
  architecture, DirectShow pipeline, threading model,
  DV format, UI layout, and build instructions.
- Project overview document (project-overview.md).
- Comments added to WinDV.rc resource script and
  WinDV.exe.manifest.
- Windows platform badge in README.
- Documentation section with links added to README.

## 1.2.3 - 2003-05-29

Minor bugfixes and changes.

- Windows XP look support (on XP systems only).
- Bugfix in AVI type-1/2 autodetection.
- Fixed crash when moving the window between monitors on
  dual-head desktops.

## 1.2.2 - 2003-02-14

Windows XP supported.

- Fixed error when recording video back to tape under
  Windows XP. Both capturing and recording back should work
  properly now.

## 1.2.1 - 2003-02-12

New features, minor bugfix.

- Added option for DV device control — useful for output to
  camcorder without "record" button.
- Fixed small bug in "Open File" dialog.

## 1.2.0 - 2002-12-13

New features.

- Capturing file-naming scheme extended; filename can contain
  configurable date-time stamp and/or indexing number.
- Added command-line handling; capturing/recording can be
  started from the command-line.
- Counter format changed to H:MI:SS.s (tenth of second
  instead of frames).
- Capturing discontinuity detection can be disabled by
  setting the threshold to zero.
- Compiled using new version of Microsoft Platform SDK.

## 1.1.3 - 2002-09-22

More code cleanup, new features.

- File selection dialog in record mode now supports multiple
  selection.
- Changed file separator from ";" to "|" in the recording
  "Source file" field.
- Video-preview code rewritten — should skip more frames on
  slower machines.
- AVI-joining code cleanup.
- Record configuration fields renamed to be more
  comprehensible. File selection dialogs added.
- Added H:MI:SS:FF counter.
- If ".avi" extension not specified, automatically adding it
  to the capturing filename.
- Removed forgotten debug information from the executable.
- Interactive screenshot with detailed description created.

## 1.1.2 - 2002-09-20

Code cleanup, bugfixes, new option.

- Video-preview code simplified.
- Option for enabling/disabling preview during recording to
  DV device added — improves reliability on slower systems.
- Possible deadlock during cancelling of recording and some
  other race-conditions fixed.
- Reprogrammed multithreading code from polling to use of
  synchronization objects.
- Added code preventing screensaver during video
  transmitting.

## 1.1.1 - 2002-09-17

Minor bugfixes.

- Fixed small bug in capture-pausing and autosplitting logic.
- Distinction between PAL and NTSC video — NTSC could be
  functional now, but still untested.

## 1.1.0 - 2002-09-15

First published version.

- Capturing from DV device works very reliably.
- Recording to DV device acceptable.
- NTSC not tested and probably non-functional.
- Win98SE tested OK for capturing; in some cases recording
  back works for type-1 only.
