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
| [architecture.md](architecture.md) | High-level component map, startup sequence, and CDV state machine |
| [directshow-pipeline.md](directshow-pipeline.md) | Filter graph design, custom filters, AVI type 1/2, graph topologies |
| [threading.md](threading.md) | All five threads, CDVQueue ring buffer, synchronization primitives, shutdown order |
| [dv-format.md](dv-format.md) | DIF frame structure, SSYB subcode, pack 0x62/0x63, BCD decoding, Y2K pivot |
| [ui-layout.md](ui-layout.md) | Dialog architecture, tab control, proportional resize system, status display |
| [building.md](building.md) | Prerequisites, IDE and NMAKE build steps, SDK paths, linked libraries |

---

## Quick Orientation

WinDV is a single-dialog MFC application layered as follows:

```text
CDVToolsDlg (UI)
  └── CDV (orchestrator, owns pipeline)
        ├── CDVInput / CAVIJoiner   (frame source)
        ├── CDVQueue                (ring buffer)
        ├── CAVIWriter / CDVOutput  (frame sink)
        └── CMonitor                (preview)
```

For the full class hierarchy see
[architecture.md](architecture.md).
For the DirectShow graph topologies see
[directshow-pipeline.md](directshow-pipeline.md).
For thread roles and synchronisation see
[threading.md](threading.md).
