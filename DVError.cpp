#include "stdafx.h"

#include "DVError.h"

/*
 * DVError.cpp — DV frame error detection via DIF block STA analysis.
 *
 * Scans raw DV frames for error concealment flags recorded by the DV
 * codec.  The DV specification (IEC 61834) defines a 4-bit STA field
 * in each video DIF block that indicates whether the block's data was
 * successfully decoded or had to be concealed (interpolated from
 * neighbours, repeated from the previous frame, etc.).
 *
 * STA values (upper nibble of byte 3 in each video DIF block):
 *   0x0  — No error, data decoded normally
 *   0x1  — Concealed by simple interpolation
 *   0x2  — Concealed by previous-frame repetition
 *   ...
 *   0xF  — Uncompensated error (worst case, visible artifact)
 *
 * Audio DIF blocks use a different mechanism.  Per DVRescue methodology,
 * an audio block is considered erroneous when the AAUX pack at offset 3
 * has pack ID 0xFF, indicating the audio data could not be read from tape.
 *
 * DIF block layout per sequence (150 blocks, 80 bytes each):
 *   Block  0      : Header   (SCT=000)
 *   Blocks 1-2    : Subcode  (SCT=001)
 *   Blocks 3-5    : VAUX     (SCT=010)
 *   Blocks 6-149  : Interleaved Audio (SCT=011) + Video (SCT=100)
 *     Audio at:  6, 22, 38, 54, 70, 86, 102, 118, 134  (9 blocks)
 *     Video at:  7-21, 23-37, ..., 135-149              (135 blocks)
 *
 * Frame sizes:
 *   NTSC: 10 DIF sequences x 150 blocks x 80 bytes = 120000 bytes
 *   PAL : 12 DIF sequences x 150 blocks x 80 bytes = 144000 bytes
 */


/* SCT field values (byte 0, bits 7-5) per IEC 61834 */
#define SCT_HEADER   0  /* 000 */
#define SCT_SUBCODE  1  /* 001 */
#define SCT_VAUX     2  /* 010 */
#define SCT_AUDIO    3  /* 011 */
#define SCT_VIDEO    4  /* 100 */

/* DIF block geometry */
#define BLOCKS_PER_SEQ    150
#define BYTES_PER_BLOCK    80
#define SEQ_SIZE          (BLOCKS_PER_SEQ * BYTES_PER_BLOCK)  /* 12000 */

/* Block index ranges within each DIF sequence.
 *
 * IEC 61834 interleaved layout: audio and video blocks alternate
 * in groups.  9 audio blocks sit at every 16th block starting at 6:
 *   6, 22, 38, 54, 70, 86, 102, 118, 134
 * Between each pair of audio blocks are 15 video blocks.
 * Total: 9 audio + 135 video + 1 header + 2 subcode + 3 VAUX = 150 */
#define AUDIO_BLOCKS_PER_SEQ  9
#define AUDIO_FIRST_BLOCK     6
#define AUDIO_BLOCK_STRIDE   16   /* audio block every 16th block */
#define VIDEO_BLOCKS_PER_SEQ 135

/* Audio error: AAUX pack ID indicating unreadable data */
#define AAUX_INVALID_PACK_ID  0xFF


/*
 * AnalyzeDVFrame
 *
 * Analyzes a single raw DV frame for error concealment flags.
 *
 * Iterates over all DIF sequences in the frame and inspects the STA
 * field in each video DIF block and the AAUX pack ID in each audio
 * DIF block.  Before checking, validates that the SCT field in byte 0
 * matches the expected section type; blocks with unexpected SCT values
 * are silently skipped.
 *
 * Parameters:
 *   data - Pointer to the raw DV frame data.
 *   len  - Frame length in bytes (120000 or 144000).
 *
 * Returns:
 *   FrameErrorInfo with per-frame error counts.
 *   All fields are zero if len is not a valid frame size.
 */
FrameErrorInfo AnalyzeDVFrame(const BYTE *data, int len)
{
    FrameErrorInfo info;
    memset(&info, 0, sizeof(info));

    /* reject frames that are neither NTSC nor PAL */
    if (len != 120000 && len != 144000)
        return info;

    /* PAL has 12 DIF sequences per frame; NTSC has 10 */
    int seqCount = (len == 144000) ? 12 : 10;

    for (int i = 0; i < seqCount; ++i) {

        /* base offset of this DIF sequence */
        int seqBase = i * SEQ_SIZE;

        /* --- Scan all interleaved blocks (6-149) for video and audio ---
         *
         * IEC 61834 interleaves 9 audio blocks among 135 video blocks:
         *   Audio at indices 6, 22, 38, 54, 70, 86, 102, 118, 134
         *   Video fills the remaining 135 slots
         * We iterate all 144 blocks and use the SCT field to dispatch. */

        for (int b = AUDIO_FIRST_BLOCK; b < BLOCKS_PER_SEQ; ++b) {

            const BYTE *block = data + seqBase + b * BYTES_PER_BLOCK;
            int sct = (block[0] >> 5) & 0x7;

            if (sct == SCT_VIDEO) {
                /* STA is the upper nibble of byte 3 */
                int sta = (block[3] >> 4) & 0xF;

                if (sta != 0) {
                    info.dwVideoErrorBlocks++;

                    /* track even/odd DIF sequence distribution */
                    if (i % 2 == 0)
                        info.dwVideoErrorsEven++;
                    else
                        info.dwVideoErrorsOdd++;

                    /* count worst-case uncompensated errors */
                    if (sta == 0xF)
                        info.dwVideoSTA_F++;
                }
            }
            else if (sct == SCT_AUDIO) {
                /* Check the AAUX pack at offset 3 (first byte after the
                 * 3-byte DIF block ID).  A pack ID of 0xFF means the audio
                 * data in this block could not be read from tape. */
                if (block[3] == AAUX_INVALID_PACK_ID)
                    info.dwAudioErrorBlocks++;
            }
            /* Skip header, subcode, VAUX blocks (should not appear here
             * but guard against malformed frames). */
        }
    }

    return info;
}


/*
 * ResetErrorStats
 *
 * Clears all fields of an ErrorStats struct to zero.
 * Call this before starting a new capture session.
 */
void ResetErrorStats(ErrorStats *stats)
{
    memset(stats, 0, sizeof(ErrorStats));
}


/*
 * AccumulateErrorStats
 *
 * Merges a single frame's error analysis into the cumulative session
 * statistics.  Tracks the worst frame (highest video error count) and
 * maintains running totals for all error categories.
 *
 * Parameters:
 *   stats       - Cumulative statistics to update.
 *   frame       - Error analysis for the current frame.
 *   frameNumber - 0-based index of the frame in the capture session.
 */
void AccumulateErrorStats(ErrorStats *stats, const FrameErrorInfo *frame, DWORD frameNumber)
{
    stats->dwTotalFrames++;

    /* count frames that had any errors at all */
    if (frame->dwVideoErrorBlocks > 0)
        stats->dwFramesWithVideoErrors++;
    if (frame->dwAudioErrorBlocks > 0)
        stats->dwFramesWithAudioErrors++;

    /* accumulate block-level totals */
    stats->dwTotalVideoErrorBlocks += frame->dwVideoErrorBlocks;
    stats->dwTotalAudioErrorBlocks += frame->dwAudioErrorBlocks;
    stats->dwTotalSTA_F            += frame->dwVideoSTA_F;
    stats->dwVideoErrorsEven       += frame->dwVideoErrorsEven;
    stats->dwVideoErrorsOdd        += frame->dwVideoErrorsOdd;

    /* track the single worst frame */
    if (frame->dwVideoErrorBlocks > stats->dwWorstFrameErrors) {
        stats->dwWorstFrameErrors = frame->dwVideoErrorBlocks;
        stats->dwWorstFrameNumber = frameNumber;
    }
}
