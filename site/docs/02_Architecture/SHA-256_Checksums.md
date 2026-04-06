# SHA-256 Checksums

`sha256.c` / `sha256.h` provide a pure C, streaming SHA-256 implementation
conforming to FIPS 180-4. The module is used for post-capture integrity
verification and for the `hash` / `verify` CLI commands.

---

## Design Constraints

- Pure C89, no C++ and no MFC dependency -- the file must be compiled
  without the precompiled header (`StdAfx.h`).
- No heap allocation: the `SHA256_CTX` context is caller-allocated.
- Compatible with both MSVC 6.0 and MSVC 2017+.
- All symbols are wrapped in `extern "C"` to allow calling from C++ files.

---

## SHA256_CTX Structure

```c
typedef struct {
    unsigned char data[64];      /* current 64-byte input block */
    unsigned int  datalen;       /* number of bytes in current block */
    unsigned int  bitlen[2];     /* total message length in bits (64-bit as two 32-bit) */
    unsigned int  state[8];      /* running hash state (H0..H7) */
} SHA256_CTX;
```

The context is designed to be stack-allocated or embedded as a member
variable. No dynamic memory is used.

---

## API

```c
void sha256_init  (SHA256_CTX *ctx);
void sha256_update(SHA256_CTX *ctx, const unsigned char *data,
                   unsigned int len);
void sha256_final (SHA256_CTX *ctx, unsigned char hash[32]);
void sha256_hex   (const unsigned char hash[32], char out[65]);
```

| Function | Description |
| --- | --- |
| `sha256_init` | Initialize context. Must be called before first update. |
| `sha256_update` | Feed data into the hash. Can be called multiple times with arbitrary-sized chunks. |
| `sha256_final` | Finalize and write the 32-byte (256-bit) hash. |
| `sha256_hex` | Format a 32-byte hash as 64 lowercase hex characters + NUL terminator. |

The typical usage pattern is to stream file data in 64 KB blocks via
repeated `sha256_update()` calls.

---

## Post-capture Integration

After every successful capture file is finalized,
`CDV::CapturingThread()` calls `ComputeFileSHA256()` (in `DShow.cpp`)
to hash the completed AVI file.
`ComputeFileSHA256()` reads the file in 64 KB blocks and feeds them
through the streaming SHA-256 API.
On success it calls `WriteSHA256Sidecar()` to write a
`sha256sum`-compatible sidecar file (`.sha256`) next to the AVI:

```text
<64-hex-hash> *<bare-filename>
```

The computed hash is also stored in `CaptureStats::szSHA256` and written
to the CSV capture log.

SHA-256 computation is controlled by `CDV::m_enableSHA256` (default:
`true`), toggled via the `IDC_CHK_SHA256` checkbox in the Capture Config
dialog (`IDD_CAPTURE_CONFIG`).

---

## Build System Integration

### CMake

The CMake project file declares `LANGUAGES C CXX` to support `sha256.c`:

```cmake
project(WinDV VERSION 1.6.0 LANGUAGES C CXX)
```

`sha256.c` is explicitly excluded from the precompiled header:

```cmake
set_source_files_properties(sha256.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
```

### VC6

In the VC6 `.dsp` file, `sha256.c` has the `/Y-` flag
(`# SUBTRACT CPP /YX /Yc /Yu`) to disable precompiled header processing.

---

## CLI Commands

The `hash` and `verify` CLI commands expose the same functionality for
off-line use:

### hash

```text
WinDV hash [-q] <file> [file2 ...]
```

For each file, `ComputeFileSHA256()` hashes the file and prints
`<hash>  <filename>` to stdout in `sha256sum` format.
Unless `-q` (quiet) is given, a `.sha256` sidecar file is written next
to each input file.

Exit code: 0 if all files were hashed successfully, non-zero if any
file could not be read.

### verify

```text
WinDV verify <file> [file2 ...]
```

For each file, `VerifyFileSHA256()` reads the `.sha256` sidecar, hashes
the file, and compares the results.
Prints `OK` or `FAILED` per file.

Return codes per file (accumulated into the process exit code):

| Code | Meaning |
| --- | --- |
| 0 | Hash matches |
| 1 | Hash mismatch |
| 2 | Sidecar not found or unreadable |

---

## Sidecar File Format

The `.sha256` sidecar file uses the standard `sha256sum` format:

```text
a1b2c3d4...  *filename.avi
```

- 64 lowercase hexadecimal characters (the SHA-256 hash)
- Two spaces
- `*` prefix (binary mode indicator, per `sha256sum` convention)
- Bare filename (no path components)

This format is compatible with the `sha256sum -c` verification command
available on Linux/macOS and via Git for Windows.
