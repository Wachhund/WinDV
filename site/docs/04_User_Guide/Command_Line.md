# Command-Line Interface

WinDV parses the command line in `CDVToolsDlg::OnInitDialog()` via
`__argc` / `__argv` (except `--version`, which is handled earlier in
`CWinDVApp::InitInstance()`).

---

## Command Reference

```text
WinDV --version                         print version string and exit
WinDV capture [-exit] <secs> <file>     timed capture from FireWire
WinDV record  [-exit] <files...>        record AVI file(s) to DV tape
WinDV check   [-v] <file.avi> [...]     AVI integrity check
WinDV hash    [-q] <file> [...]         compute SHA-256 and write sidecar
WinDV verify  <file> [...]              verify file against .sha256 sidecar
```

---

## --version

Prints the version string (from `Version.h`) to stdout and exits
immediately. Handled in `CWinDVApp::InitInstance()` before the dialog
is created.

---

## capture

```text
WinDV capture [-exit] <secs> <file>
```

Starts a timed capture from the default FireWire DV device.

- `<secs>` -- capture duration in seconds.
- `<file>` -- base path for the output AVI file.
- `-exit` -- exit the application when capture completes (useful for
  scripted/batch capture).

The capture uses all current settings (AVI type, split thresholds, etc.)
from the registry or INI file.

---

## record

```text
WinDV record [-exit] <files...>
```

Records the specified AVI file(s) to the default FireWire DV device.
Multiple files are played in the order given.

- `-exit` -- exit the application when recording completes.

---

## check

```text
WinDV check [-v] <file.avi> [file2.avi ...]
```

Performs an AVI integrity check on one or more files. The checker validates
RIFF-AVI structure: hdrl, movi, idx1/indx, and DV frame sizes
(120,000 bytes for NTSC, 144,000 bytes for PAL).

- `-v` -- verbose output (print details for each check).

The checker reads only chunk headers and the index, not frame data,
making it fast even for large files.

Exit code: 0 if all files pass, non-zero if any problems are found.

---

## hash (v1.6.0)

```text
WinDV hash [-q] <file> [file2 ...]
```

For each file, computes the SHA-256 hash and prints the result to stdout
in `sha256sum` format:

```text
<64-hex-hash>  <filename>
```

Unless `-q` (quiet) is given, a `.sha256` sidecar file is written next
to each input file. The sidecar format is:

```text
<64-hex-hash> *<bare-filename>
```

Exit code: 0 if all files were hashed successfully, non-zero if any
file could not be read.

---

## verify (v1.6.0)

```text
WinDV verify <file> [file2 ...]
```

For each file, reads the `.sha256` sidecar, re-hashes the file, and
compares the results. Prints `OK` or `FAILED` per file.

Return codes per file (accumulated into the process exit code):

| Code | Meaning |
| --- | --- |
| 0 | Hash matches |
| 1 | Hash mismatch |
| 2 | Sidecar not found or unreadable |

---

## Console Output

WinDV is a GUI application (`/SUBSYSTEM:WINDOWS`). Console output for
all CLI commands uses `GetStdHandle(STD_OUTPUT_HANDLE)` first (works
when stdout is redirected to a pipe or file), then falls back to
`SafeAttachConsole()` for interactive use.

`AllocConsole()` is never used, as it would replace inherited standard
handles.
