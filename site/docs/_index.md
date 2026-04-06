---
auto_toc: false
---

<p class="HeroText">
    <strong>WinDV</strong> is a lightweight Windows utility for capturing and recording DV video over FireWire (IEEE 1394). Originally written in 2002 by Petr Mourek, actively maintained at <a href="https://github.com/Wachhund/WinDV">Wachhund/WinDV</a>.
</p>

---

### Key Features

---

<div class="Row">
<div class="Row__third">

#### Capture & Record

- DV capture to AVI files (Type-1 and Type-2)
- DV recording from AVI files to tape
- Scene-split on timestamp discontinuities
- Configurable auto-stop on signal loss

</div>
<div class="Row__third">

#### Archive Quality

- [DV error detection](02_Architecture/DV_Error_Detection.md) via STA analysis (IEC 61834)
- [SHA-256 checksums](02_Architecture/SHA-256_Checksums.md) with sha256sum-compatible sidecar files
- [AVI integrity check](04_User_Guide/Command_Line.md) for post-capture validation
- CSV capture logging with error statistics

</div>
<div class="Row__third">

#### Developer Friendly

- Dual build system: [CMake](01_Getting_Started/Building.md) + VC6
- CI via GitHub Actions
- Portable mode (INI file)
- Windows XP SP3 to Windows 11

</div>
</div>

---

### Quick Start

Download the [latest release](https://github.com/Wachhund/WinDV/releases/latest) and run the EXE. No installation required.

| Download | Platform |
|----------|----------|
| `WinDV_x.x.x_x86.exe` | Windows 7+ (32-bit) |
| `WinDV_x.x.x_x86_xp.exe` | Windows XP SP3+ (32-bit) |

### Requirements

- FireWire (IEEE 1394) controller (OHCI)
- DV camcorder or DV deck
- DirectX 8.1+
