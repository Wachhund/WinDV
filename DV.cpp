#include "stdafx.h"

#include "DV.h"

/*
 * DV.cpp — DV frame SSYB subcode parser.
 *
 * A raw DV frame is structured as a sequence of DIF (Digital Interface
 * Format) blocks.  Each DIF sequence (one field's worth of data) starts
 * with a header block, followed by subcode blocks, then video and audio
 * data blocks.
 *
 * The subcode (SSYB) area carries auxiliary metadata recorded by the
 * camcorder alongside the video, including the date (pack ID 0x62) and
 * time (pack ID 0x63) at which the footage was originally recorded.
 * Both values are BCD-encoded (Binary-Coded Decimal).
 *
 * Frame layout constants:
 *   NTSC: 10 DIF sequences × 150 DIF blocks × 80 bytes = 120 000 bytes
 *   PAL : 12 DIF sequences × 150 DIF blocks × 80 bytes = 144 000 bytes
 *
 * Within each DIF sequence:
 *   Block 0        : header (1 block)
 *   Blocks 1–2     : subcode (2 blocks, 6 sync packets each)
 *   Blocks 3–5     : VAUX (video auxiliary)
 *   Blocks 6–8     : audio
 *   Blocks 9–134   : video data
 */


/*
 * GetSSYBPack
 *
 * Scans the SSYB subcode area of a raw DV frame and returns the first
 * 5-byte subcode pack whose ID byte matches packNum.
 *
 * The SSYB area lives in the first two DIF blocks of each DIF sequence
 * (block index 1 and 2, zero-based within the sequence).  Each of those
 * blocks contains 6 sync packets; each packet is 8 bytes long and is
 * preceded by a 3-byte block header and a 3-byte sync-packet header,
 * leaving 5 bytes of actual pack payload (ID + 4 data bytes).
 *
 * Address formula for packet k in subcode block j of sequence i:
 *
 *   offset = i * 150 * 80          <- start of DIF sequence i
 *          + 1 * 80                <- skip the sequence header block
 *          + j * 80                <- select subcode block j (0 or 1)
 *          + 3                     <- skip the 3-byte DIF block header
 *          + k * 8                 <- select sync packet k (0..5)
 *          + 3                     <- skip the 3-byte sync-packet header
 *
 * Parameters:
 *   data    - Pointer to the start of the raw DV frame.
 *   len     - Frame length in bytes (used to determine NTSC vs. PAL).
 *   packNum - Subcode pack ID to search for (e.g. 0x62 = date, 0x63 = time).
 *   pack    - Output buffer of at least 5 bytes; receives the found pack.
 *
 * Returns:
 *   TRUE  if a matching pack was found (pack[] has been filled).
 *   FALSE if no matching pack exists in any subcode block.
 */
static int GetSSYBPack(BYTE *data, int len, int packNum, BYTE *pack)
{
    /* PAL has 12 DIF sequences per frame; NTSC has 10 */
    int seqCount = len >= 144000 ? 12 : 10;

    /* process all DIF sequences */

    for (int i = 0; i < seqCount; ++i) {

        /* there are two DIF blocks in the subcode section */

        for (int j = 0; j < 2; ++j) {

            /* each block has 6 packets */

            for (int k = 0; k < 6; ++k) {

                /* 150 DIF blocks per sequence, 80 bytes per DIF block,
				   subcode blocks start at block 1,
				   block and packet have 3 bytes header,
				   packet is 8 bytes long (including header) */

                const unsigned char *s = &data[i * 150 * 80 + 1 * 80 + j * 80 + 3 + k * 8 + 3];

                /* first byte of pack payload is the pack ID */
                if (s[0] == packNum) {
                    pack[0] = s[0];
                    pack[1] = s[1];
                    pack[2] = s[2];
                    pack[3] = s[3];
                    pack[4] = s[4];
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

/*
 * GetDVRecordingTime
 *
 * Extracts the camcorder recording timestamp from a raw DV frame by
 * reading the SSYB date pack (0x62) and time pack (0x63).
 *
 * Pack 0x62 layout (bytes 1..4 after the ID):
 *   byte 1: unused flags
 *   byte 2: day   (BCD, bits 5-4 = tens, bits 3-0 = units)
 *   byte 3: month (BCD, bit  4   = tens, bits 3-0 = units)
 *   byte 4: year  (BCD, bits 7-4 = tens, bits 3-0 = units)
 *
 * Pack 0x63 layout (bytes 1..4 after the ID):
 *   byte 1: unused flags
 *   byte 2: seconds (BCD, bits 6-4 = tens, bits 3-0 = units)
 *   byte 3: minutes (BCD, bits 6-4 = tens, bits 3-0 = units)
 *   byte 4: hours   (BCD, bits 5-4 = tens, bits 3-0 = units)
 *
 * BCD decoding pattern for each field:
 *   value = (raw & 0xF) + 10 * ((raw >> 4) & mask)
 * where mask limits the tens nibble to its valid range.
 *
 * Parameters:
 *   buf - Pointer to the raw DV frame data.
 *   len - Frame length in bytes (must be 120000 NTSC or 144000 PAL).
 *
 * Returns:
 *   time_t  Recording timestamp as seconds since the Unix epoch, or
 *           -1 on invalid frame size or missing subcode packs.
 */
int GetDVRecordingTime(BYTE *buf, int len)
{
    BYTE pack62[5];  /* SSYB date pack (ID 0x62) */
    BYTE pack63[5];  /* SSYB time pack (ID 0x63) */

	/* reject frames that are neither NTSC nor PAL */
	if (len != 144000 && len != 120000) return -1;

    /* locate the date pack; bail out if not present */
    if (!GetSSYBPack(buf, len, 0x62, pack62))
        return -1;

    /* locate the time pack; bail out if not present */
    if (!GetSSYBPack(buf, len, 0x63, pack63))
        return -1;

    /* raw BCD bytes from the date pack */
    int day   = pack62[2];
    int month = pack62[3];
    int year  = pack62[4];

    /* raw BCD bytes from the time pack */
    int sec  = pack63[2];
    int min  = pack63[3];
    int hour = pack63[4];

    /* --- BCD decode each field ---
     * Lower nibble = units digit.
     * Upper nibble (masked to valid range) = tens digit.
     * The masks strip flag/parity bits that share the upper nibble. */
    sec   = (sec  & 0xf) + 10 * ((sec  >> 4) & 0x7); /* seconds: 0-59, tens fits in 3 bits */
    min   = (min  & 0xf) + 10 * ((min  >> 4) & 0x7); /* minutes: 0-59 */
    hour  = (hour & 0xf) + 10 * ((hour >> 4) & 0x3); /* hours:   0-23, tens fits in 2 bits */
    year  = (year & 0xf) + 10 * ((year >> 4) & 0xf); /* year:    2-digit (e.g. 03 = 2003) */
    month = (month & 0xf) + 10 * ((month >> 4) & 0x1); /* month: 1-12, tens is always 0 or 1 */
    day   = (day  & 0xf) + 10 * ((day  >> 4) & 0x3); /* day:    1-31 */

    /* DV stores only a 2-digit year; use Y2K pivot: <50 -> 2000s, >=50 -> 1900s */
    if (year < 50)
        year += 2000;
    else
        year += 1900;

	struct tm recDate;

    recDate.tm_sec  = sec;
    recDate.tm_min  = min;
    recDate.tm_hour = hour;
    recDate.tm_mday = day;
    recDate.tm_mon  = month - 1;  /* tm_mon is 0-based; DV month is 1-based */
    recDate.tm_year = year - 1900; /* tm_year is years since 1900 */
    recDate.tm_wday  = -1;         /* let mktime() compute day-of-week */
    recDate.tm_yday  = -1;         /* let mktime() compute day-of-year */
    recDate.tm_isdst = -1;         /* let mktime() determine DST */

    return mktime(&recDate);
}
