/* AVICheck.h — AVI/DV integrity checker.
 *
 * Validates RIFF-AVI structure, DV frame sizes, and index presence.
 * Pure Win32 file I/O, no DirectShow dependency.  XP SP3 compatible.
 */

#ifndef AVICHECK_H
#define AVICHECK_H

struct AVICheckResult {
	BOOL            bValid;          /* overall: file is a valid DV-AVI */
	BOOL            bHasIndex;       /* idx1 or indx chunk present */
	DWORD           dwFrameCount;    /* total video frames (from avih header) */
	DWORD           dwIndexEntries;  /* video entries in idx1 */
	DWORD           dwDefectFrames;  /* frames with wrong size (!= 120000/144000) */
	unsigned __int64 dwExpectedSize; /* computed expected file size */
	unsigned __int64 dwActualSize;   /* actual file size */
	BOOL            bIsPAL;          /* TRUE=PAL (144000), FALSE=NTSC (120000) */
	CString         sError;          /* error description (empty if OK) */

	AVICheckResult() {
		bValid = FALSE;
		bHasIndex = FALSE;
		dwFrameCount = 0;
		dwIndexEntries = 0;
		dwDefectFrames = 0;
		dwExpectedSize = 0;
		dwActualSize = 0;
		bIsPAL = FALSE;
	}
};

/* Check a single AVI file for structural integrity.
 * Returns immediately on unreadable/empty/non-AVI files.
 * Performance: uses chunk-header walking + idx1 scan, does NOT read frame data. */
AVICheckResult CheckAVIIntegrity(LPCSTR szPath);

/* Format a one-line summary for console output.
 * bVerbose adds frame count, index count, and file size. */
CString FormatCheckResult(const AVICheckResult& r, LPCSTR szFilename, BOOL bVerbose);

#endif /* AVICHECK_H */
