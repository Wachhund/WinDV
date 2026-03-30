/* DVError.h — DV frame error detection via DIF block STA analysis.
 *
 * Analyzes error concealment flags in DV frames per IEC 61834.
 * Compatible with DVRescue/DVAnalyzer STA metrics.
 */

#ifndef DVERROR_H
#define DVERROR_H

/* Per-frame error analysis result. */
struct FrameErrorInfo {
    DWORD dwVideoErrorBlocks;   /* STA != 0 in video DIF blocks */
    DWORD dwAudioErrorBlocks;   /* parity errors in audio DIF blocks */
    DWORD dwVideoErrorsEven;    /* video errors in even DIF sequences (0,2,4,...) */
    DWORD dwVideoErrorsOdd;     /* video errors in odd DIF sequences (1,3,5,...) */
    DWORD dwVideoSTA_F;         /* count of STA=0xF (uncompensated, worst type) */
};

/* Cumulative error statistics for a capture session. */
struct ErrorStats {
    DWORD dwTotalFrames;           /* total frames analyzed */
    DWORD dwFramesWithVideoErrors; /* frames with at least one video STA != 0 */
    DWORD dwFramesWithAudioErrors; /* frames with at least one audio error */
    DWORD dwTotalVideoErrorBlocks; /* sum of all video STA errors */
    DWORD dwTotalAudioErrorBlocks; /* sum of all audio errors */
    DWORD dwWorstFrameNumber;      /* frame number with most errors */
    DWORD dwWorstFrameErrors;      /* error count of worst frame */
    DWORD dwTotalSTA_F;            /* sum of all uncompensated errors (0xF) */
    DWORD dwVideoErrorsEven;       /* total even-sequence video errors */
    DWORD dwVideoErrorsOdd;        /* total odd-sequence video errors */
};

/* Analyze a single raw DV frame for error concealment flags.
 *
 * Scans the STA byte (byte 3) of all video and audio DIF blocks.
 * Video blocks: 135 per DIF sequence (interleaved among audio blocks).
 * Audio blocks: 9 per DIF sequence (at indices 6, 22, 38, ..., 134).
 *
 * Parameters:
 *   data - Pointer to the raw DV frame data.
 *   len  - Frame length (120000 NTSC or 144000 PAL; other values return zeros).
 *
 * Returns:
 *   FrameErrorInfo with error counts. All zeros if len is invalid.
 *
 * Max possible values per frame:
 *   NTSC: 1350 video blocks (135*10), 90 audio blocks (9*10)
 *   PAL:  1620 video blocks (135*12), 108 audio blocks (9*12)
 */
FrameErrorInfo AnalyzeDVFrame(const BYTE *data, int len);

/* Reset an ErrorStats struct to all zeros. */
void ResetErrorStats(ErrorStats *stats);

/* Accumulate a single frame's errors into cumulative stats.
 * frameNumber is the 0-based frame index within the capture session. */
void AccumulateErrorStats(ErrorStats *stats, const FrameErrorInfo *frame, DWORD frameNumber);

#endif /* DVERROR_H */
