# USB DV Capture

WinDV was originally designed for FireWire (IEEE 1394) DV
devices. However, a small number of camcorders support
full-resolution DV streaming over USB. This document covers
USB DV compatibility and known working configurations.

---

## Background

Most MiniDV camcorders use USB only for:

- Transferring still photos from a memory card
- Low-resolution "webcam" mode

These modes do **not** deliver raw DV data and are **not**
compatible with WinDV.

A few camcorder models, primarily from Panasonic, support
streaming full-resolution DV data over USB 2.0. When
connected in this mode, the camera appears as a DirectShow
capture device exposing `MEDIATYPE_Interleaved` -- the same
media type used by FireWire DV devices. WinDV's device
filter recognizes these devices and lists them in the
device selection dialog.

---

## Enabling USB DV Streaming

On supported Panasonic camcorders, USB DV streaming must
be enabled in the camera menu:

1. Set the camera to **VCR/Playback** mode
2. Open the menu and navigate to **USB FUNCTION**
3. Set it to **Motion DV** (not "WEB CAMERA" or "PictBridge")
4. Connect the camera via USB to the PC
5. Start WinDV and select the device

The setting name "Motion DV" refers to Panasonic's
MotionDV Studio software, which was originally required
for USB capture. On modern Windows (7/10/11), the camera
is recognized without Panasonic's proprietary drivers.

---

## Known Compatible Models

### Panasonic (confirmed USB DV streaming)

These models have been reported to deliver full-resolution
DV over USB 2.0. The NV prefix is for PAL models, PV is
for the equivalent NTSC models.

| PAL Model | NTSC Model | USB DV | Notes |
| --- | --- | --- | --- |
| NV-GS400 | PV-GS400 | Yes | Tested on Win 7/10/11 |
| NV-GS500 | PV-GS500 | Yes | Tested on Win 7/10/11 |
| NV-GS300 | PV-GS300 | Likely | Higher-end model |
| NV-GS320 | PV-GS320 | Likely | Higher-end model |
| NV-GS330 | PV-GS330 | Likely | Higher-end model |

### Panasonic (USB for photos/webcam only)

These models have USB ports but are **not** known to
support full-resolution DV streaming over USB. Use
FireWire (DV port) for tape capture.

| PAL Model | NTSC Model | USB DV | Notes |
| --- | --- | --- | --- |
| NV-GS25 | -- | No | USB = photos/webcam only |
| NV-GS35 | PV-GS35 | No | USB = photos/webcam only |
| NV-GS75 | PV-GS75 | No | USB = photos/webcam only |
| NV-GS150 | PV-GS150 | No | USB = photos/webcam only |
| NV-GS180 | PV-GS180 | Unclear | Check menu for "Motion DV" |
| NV-GS230 | PV-GS230 | Unclear | Check menu for "Motion DV" |
| NV-GS250 | PV-GS250 | Unclear | Check menu for "Motion DV" |

### Canon (confirmed USB DV streaming)

A few Canon models also support DV over USB. The camera
menu setting is `VCR SETUP > AV-DV > OFF`.

- Canon Optura 60
- Canon Optura 500
- Canon Optura 600

### Sony (no USB DV streaming)

Most Sony camcorders do **not** support DV streaming over
USB. However, a few models are known exceptions.

| Model | USB DV | Notes |
| --- | --- | --- |
| DCR-PC1000 / DCR-PC1000E | Yes | Tested with WinDV 1.2.4 |

Most other Sony camcorders support USB streaming but
**not** raw DV quality -- they deliver compressed video
instead. Use FireWire (i.LINK) for DV tape capture on
those models.

If you have a Sony camcorder that works with WinDV over
USB, please report it so this list can be updated.

---

## How to Check if Your Camera Supports USB DV

1. Look in the camera menu for a **"DV Streaming"** or
   **"Motion DV"** USB function setting
2. If the menu only shows "WEB CAMERA" or "PictBridge",
   USB DV is not supported
3. When connected via USB in the correct mode, the device
   should appear in Windows Device Manager as
   **"Panasonic DVC DV Stream Device"** or similar

---

## Technical Details

When a camcorder streams DV over USB, it registers as a
DirectShow video capture device under
`CLSID_VideoInputDeviceCategory`. The device exposes output
pins with `MEDIATYPE_Interleaved` -- the same media type
used by FireWire DV devices.

WinDV's device enumeration filter (in `EnumVideoDevices()`,
DShow.cpp) checks for `MEDIATYPE_Interleaved` on output
pins. USB DV devices that expose this media type are
automatically listed in the device selection dialog
alongside FireWire devices. No code changes are needed.

If a USB DV device uses `MEDIATYPE_Video` with a DV
subtype (`MEDIASUBTYPE_dvsd`, `MEDIASUBTYPE_dvhd`, or
`MEDIASUBTYPE_dvsl`) instead of `MEDIATYPE_Interleaved`,
it will not appear in the device list. Please report
such devices so the filter can be extended.

---

## Troubleshooting

**Device not listed in WinDV:**

- Verify the camera is in VCR/Playback mode (not Record)
- Check that the USB function is set to "Motion DV"
- Try disconnecting and reconnecting the USB cable
- Some cameras must be powered on *before* connecting USB

**Panasonic GS400 detection issue:**

The GS400 may not be detected if it is connected via USB
and then powered on. Workaround: power on the camera
first, then connect the USB cable.

**Low resolution or no video:**

If you see low-resolution video, the camera is in webcam
mode, not DV streaming mode. Change the USB function
setting in the camera menu.

---

## References

- [dvmp.co.uk -- DV Capture over USB](https://dvmp.co.uk/dv-capture-over-usb.htm)
  (comprehensive testing of USB DV on Windows 7/10/11)
- Panasonic USB Driver Update:
  `https://av.jpn.support.panasonic.com/support/global/cs/e_cam/download/downdvc_usb.html`
