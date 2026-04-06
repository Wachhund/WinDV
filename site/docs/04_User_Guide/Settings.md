# Settings

All settings are saved in `OnClose()` and restored in `OnInitDialog()`.
The registry root is `HKCU\Software\Petr Mourek\WinDV`, set by
`SetRegistryKey("Petr Mourek")` in `CWinDVApp::InitInstance()`.

---

## Portable Mode (INI File)

When `CWinApp::m_pszProfileName` points to a `.ini` file instead of the
registry, MFC transparently redirects all `GetProfileInt` /
`WriteProfileInt` calls to the INI file. This enables portable mode
where settings travel with the executable.

---

## Registry Keys

### Main Window (section `"MainWindow"`)

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

### Capture (section `"Capture"`)

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

### Record (section `"Record"`)

| Value | Type | Default | Description |
| --- | --- | --- | --- |
| `DVDevice` | `SZ` | `Microsoft DV Camera and VCR` | Record dest. device |
| `File` | `SZ` | | Last record source file list |
| `AVIPrefix` | `SZ` | | Pipe-delimited leader AVI files |
| `AVISuffix` | `SZ` | | Pipe-delimited trailer AVI files |
| `Preview` | `DWORD` | 1 | Show preview during recording |
