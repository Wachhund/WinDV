/* AVICheck.cpp — AVI/DV integrity checker implementation.
 *
 * Walks RIFF chunk structure to validate:
 *   1. RIFF AVI signature
 *   2. hdrl chunk with avih (main AVI header)
 *   3. movi chunk presence
 *   4. idx1 index (or indx for OpenDML)
 *   5. DV frame sizes (120000 NTSC / 144000 PAL)
 *   6. Frame-count vs. index-entry consistency
 *   7. File-size plausibility
 *
 * Strategy for performance (AC-9: 13 GB < 5 s):
 *   - Read avih for declared frame count (56 bytes)
 *   - Scan idx1 for per-frame size validation (~1.4 MB for 60 min)
 *   - Top-level chunk walk with seeks (no frame data read)
 *   - OpenDML: verify indx presence, skip full parse
 */

#include "StdAfx.h"
#include "AVICheck.h"

/* ---- Local RIFF/AVI structures (avoid SDK header conflicts) ---- */

#pragma pack(push, 1)

struct RIFFChunk {
	char  fourcc[4];
	DWORD dwSize;
};

struct AVIMainHeader {
	DWORD dwMicroSecPerFrame;
	DWORD dwMaxBytesPerSec;
	DWORD dwPaddingGranularity;
	DWORD dwFlags;
	DWORD dwTotalFrames;
	DWORD dwInitialFrames;
	DWORD dwStreams;
	DWORD dwSuggestedBufferSize;
	DWORD dwWidth;
	DWORD dwHeight;
	DWORD dwReserved[4];
};

struct AVIIndexEntry {
	char  ckid[4];
	DWORD dwFlags;
	DWORD dwOffset;
	DWORD dwSize;
};

#pragma pack(pop)

/* ---- Helpers ---- */

static inline BOOL FCC(const char *a, const char *b)
{
	return memcmp(a, b, 4) == 0;
}

/* Pad chunk size to even boundary (RIFF spec). */
#define PADEVEN(x) (((x) + 1) & ~1UL)

static BOOL SeekAbs(HANDLE hFile, __int64 pos)
{
	LONG hi = (LONG)(pos >> 32);
	DWORD lo = SetFilePointer(hFile, (LONG)(pos & 0xFFFFFFFF), &hi, FILE_BEGIN);
	return !(lo == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR);
}

static BOOL ReadExact(HANDLE hFile, void *buf, DWORD n)
{
	DWORD dwRead;
	return ReadFile(hFile, buf, n, &dwRead, NULL) && dwRead == n;
}

/* ---- Main implementation ---- */

AVICheckResult CheckAVIIntegrity(LPCSTR szPath)
{
	AVICheckResult r;

	HANDLE hFile = CreateFile(szPath, GENERIC_READ, FILE_SHARE_READ,
	                          NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		r.sError = "Zugriff verweigert";
		return r;
	}

	/* File size (64-bit safe). */
	DWORD dwHigh = 0;
	DWORD dwLow = GetFileSize(hFile, &dwHigh);
	if (dwLow == INVALID_FILE_SIZE && GetLastError() != NO_ERROR) {
		r.sError = "Dateigröße nicht lesbar";
		CloseHandle(hFile);
		return r;
	}
	r.dwActualSize = ((unsigned __int64)dwHigh << 32) | dwLow;

	if (r.dwActualSize == 0) {
		r.sError = "Datei ist leer";
		CloseHandle(hFile);
		return r;
	}

	if (r.dwActualSize < 12) {
		r.sError = "Datei zu klein für RIFF-Header";
		CloseHandle(hFile);
		return r;
	}

	/* Read RIFF header: "RIFF" + size + "AVI ". */
	char riffHdr[12];
	if (!ReadExact(hFile, riffHdr, 12)) {
		r.sError = "Kann RIFF-Header nicht lesen";
		CloseHandle(hFile);
		return r;
	}
	if (!FCC(riffHdr, "RIFF") || !FCC(riffHdr + 8, "AVI ")) {
		r.sError = "Keine AVI-Datei (RIFF/AVI-Signatur fehlt)";
		CloseHandle(hFile);
		return r;
	}

	DWORD riffSize = *(const DWORD *)(riffHdr + 4);
	__int64 riffEnd = 8 + (__int64)riffSize;
	if (riffEnd > (__int64)r.dwActualSize)
		riffEnd = (__int64)r.dwActualSize;

	BOOL bHasHdrl = FALSE;
	BOOL bHasMovi = FALSE;
	AVIMainHeader avih;
	BOOL bHasAvih = FALSE;
	memset(&avih, 0, sizeof(avih));

	/* ---- Walk top-level chunks inside RIFF AVI ---- */

	__int64 pos = 12;
	while (pos + 8 <= riffEnd) {
		if (!SeekAbs(hFile, pos)) break;

		RIFFChunk ck;
		if (!ReadExact(hFile, &ck, 8)) break;

		__int64 nextPos = pos + 8 + (__int64)PADEVEN(ck.dwSize);
		/* Guard against corrupt chunks: no forward progress → bail out. */
		if (nextPos <= pos) break;

		if (FCC(ck.fourcc, "LIST")) {
			char listType[4];
			if (!ReadExact(hFile, listType, 4)) break;

			if (FCC(listType, "hdrl")) {
				bHasHdrl = TRUE;
				/* First sub-chunk of hdrl should be avih. */
				RIFFChunk avihCk;
				if (ReadExact(hFile, &avihCk, 8) && FCC(avihCk.fourcc, "avih")) {
					DWORD readSz = avihCk.dwSize;
					if (readSz > sizeof(AVIMainHeader))
						readSz = sizeof(AVIMainHeader);
					if (ReadExact(hFile, &avih, readSz))
						bHasAvih = TRUE;
				}
			}
			else if (FCC(listType, "movi")) {
				bHasMovi = TRUE;
			}
		}
		else if (FCC(ck.fourcc, "idx1")) {
			r.bHasIndex = TRUE;

			/* Read idx1 entries in blocks to count video frames
			 * and validate DV frame sizes. */
			DWORD totalEntries = ck.dwSize / sizeof(AVIIndexEntry);
			DWORD videoFrames = 0;
			DWORD defect = 0;
			BOOL  formatKnown = FALSE;

			AVIIndexEntry buf[4096];
			DWORD remaining = totalEntries;
			while (remaining > 0) {
				DWORD batch = remaining > 4096 ? 4096 : remaining;
				if (!ReadExact(hFile, buf, batch * sizeof(AVIIndexEntry)))
					break;
				for (DWORD i = 0; i < batch; i++) {
					/* Video chunks: "00dc" (compressed) or "00db" (DIB). */
					if (buf[i].ckid[0] == '0' && buf[i].ckid[1] == '0' &&
					    buf[i].ckid[2] == 'd' &&
					    (buf[i].ckid[3] == 'c' || buf[i].ckid[3] == 'b')) {
						videoFrames++;
						if (!formatKnown &&
						    (buf[i].dwSize == 120000 || buf[i].dwSize == 144000)) {
							r.bIsPAL = (buf[i].dwSize == 144000);
							formatKnown = TRUE;
						}
						if (buf[i].dwSize != 120000 && buf[i].dwSize != 144000)
							defect++;
					}
				}
				remaining -= batch;
			}
			r.dwIndexEntries = videoFrames;
			r.dwDefectFrames = defect;
		}
		else if (FCC(ck.fourcc, "indx")) {
			/* OpenDML super-index — note presence, skip full parse. */
			r.bHasIndex = TRUE;
		}

		pos = nextPos;
	}

	/* ---- OpenDML: scan for RIFF AVIX continuation chunks ---- */

	while (pos + 12 <= (__int64)r.dwActualSize) {
		if (!SeekAbs(hFile, pos)) break;
		char hdr[12];
		if (!ReadExact(hFile, hdr, 12)) break;
		if (!FCC(hdr, "RIFF") || !FCC(hdr + 8, "AVIX"))
			break;
		/* Valid continuation chunk — the file has multi-RIFF structure.
		 * movi data is in here; we already got frame counts from idx1/avih. */
		DWORD avixSize = *(const DWORD *)(hdr + 4);
		pos += 8 + (__int64)PADEVEN(avixSize);
	}

	CloseHandle(hFile);

	/* ---- Determine frame count ---- */

	if (bHasAvih) {
		r.dwFrameCount = avih.dwTotalFrames;
		if (!r.bIsPAL && avih.dwHeight > 0)
			r.bIsPAL = (avih.dwHeight >= 576);
	}
	if (r.dwFrameCount == 0 && r.dwIndexEntries > 0)
		r.dwFrameCount = r.dwIndexEntries;

	/* ---- Compute expected file size ---- */

	if (r.dwFrameCount > 0) {
		DWORD frameSize = r.bIsPAL ? 144000 : 120000;
		/* Each frame in movi: 8 bytes chunk header + frame data.
		 * Plus ~4 KB overhead for RIFF headers, hdrl, idx1 header. */
		r.dwExpectedSize = (unsigned __int64)r.dwFrameCount * (frameSize + 8) + 4096;
	}

	/* ---- Build result ---- */

	r.bValid = bHasHdrl && bHasMovi && (r.dwFrameCount > 0);

	if (!bHasHdrl)     r.sError += "hdrl-Chunk fehlt. ";
	if (!bHasMovi)     r.sError += "movi-Chunk fehlt. ";
	if (!r.bHasIndex)  r.sError += "Kein Index (idx1/indx). ";
	if (r.dwFrameCount == 0)
		r.sError += "Keine Frames gefunden. ";
	if (r.dwDefectFrames > 0) {
		CString tmp;
		tmp.Format("%lu Frames mit falscher Größe. ", r.dwDefectFrames);
		r.sError += tmp;
	}
	if (r.dwIndexEntries > 0 && r.dwFrameCount > 0 &&
	    r.dwIndexEntries != r.dwFrameCount) {
		CString tmp;
		tmp.Format("Frame-Count (%lu) != Index-Einträge (%lu). ",
		           r.dwFrameCount, r.dwIndexEntries);
		r.sError += tmp;
	}
	r.sError.TrimRight();

	return r;
}

/* ---- Formatting ---- */

CString FormatCheckResult(const AVICheckResult& r, LPCSTR szFilename, BOOL bVerbose)
{
	/* Extract bare filename from path. */
	CString name = szFilename;
	int sep = name.ReverseFind('\\');
	if (sep >= 0) name = name.Mid(sep + 1);

	CString out;
	if (r.bValid && r.dwDefectFrames == 0 && r.bHasIndex) {
		out.Format("OK   %s (%lu frames, %s)",
		           (LPCSTR)name, r.dwFrameCount,
		           r.bIsPAL ? "PAL" : "NTSC");
	} else {
		out.Format("FAIL %s: %s", (LPCSTR)name, (LPCSTR)r.sError);
	}

	if (bVerbose) {
		CString detail;
		detail.Format("\r\n     Frames: %lu  Index: %lu  Defekt: %lu  "
		              "Größe: %I64u Bytes",
		              r.dwFrameCount, r.dwIndexEntries, r.dwDefectFrames,
		              r.dwActualSize);
		out += detail;
	}
	return out;
}
