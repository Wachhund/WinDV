# WinDV Documentation

This directory contains in-depth technical documentation for the WinDV
codebase.
For a project overview, installation steps, command-line reference, and
configuration registry keys, see
[`project-overview.md`](../project-overview.md) one level up.

---

## Contents

| Document | Description |
| --- | --- |
| [architecture.md](architecture.md) | High-level component map, startup sequence, CDV state machine, DVError and SHA-256 modules |
| [directshow-pipeline.md](directshow-pipeline.md) | Filter graph design, custom filters, AVI type 1/2, graph topologies, CapturingThread extended pipeline |
| [threading.md](threading.md) | All five threads, CDVQueue ring buffer, synchronization primitives, shutdown order, post-capture phase |
| [dv-format.md](dv-format.md) | DIF frame structure, SSYB subcode, pack 0x62/0x63, BCD decoding, Y2K pivot, STA error fields, audio error detection |
| [ui-layout.md](ui-layout.md) | Dialog architecture, tab control, proportional resize system, status display, CLI commands, error reporting |
| [building.md](building.md) | Prerequisites, CMake and VC6 build steps, SDK paths, linked libraries (v1.6.0) |
| [usb-dv-capture.md](usb-dv-capture.md) | USB DV streaming compatibility, known working cameras, troubleshooting |

---

## Quick Orientation

WinDV is a single-dialog MFC application layered as follows:

```text
CDVToolsDlg (UI)
  └── CDV (orchestrator, owns pipeline)
        ├── CDVInput / CAVIJoiner   (frame source)
        ├── CDVQueue                (ring buffer)
        ├── CAVIWriter / CDVOutput  (frame sink)
        ├── CMonitor                (preview)
        ├── DVError.cpp/h           (per-frame STA error analysis)
        └── sha256.c/h              (post-capture SHA-256 hash + sidecar)
```

For the full class hierarchy see
[architecture.md](architecture.md).
For the DirectShow graph topologies see
[directshow-pipeline.md](directshow-pipeline.md).
For thread roles and synchronisation see
[threading.md](threading.md).
