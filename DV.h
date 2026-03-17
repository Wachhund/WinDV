/*
 * DV.h — Public interface for DV frame timestamp extraction.
 *
 * Declares GetDVRecordingTime(), the sole function responsible for
 * parsing the SSYB (Sub-code Sync Block) area of a raw DV frame and
 * returning the camcorder's recorded date/time as a POSIX time_t value.
 *
 * Supports both NTSC (120 000 bytes/frame) and PAL (144 000 bytes/frame)
 * frame sizes.  Returns -1 when the frame size is unrecognised or the
 * required subcode packs are absent.
 */

/*
 * GetDVRecordingTime
 *
 * Extracts the recording timestamp embedded in the SSYB subcode area of
 * a single raw DV frame.
 *
 * Parameters:
 *   buf  - Pointer to the raw DV frame data.
 *   len  - Length of the frame in bytes.  Must be 120000 (NTSC) or
 *           144000 (PAL); any other value causes immediate return of -1.
 *
 * Returns:
 *   time_t  Camcorder recording time as seconds since the Unix epoch,
 *           suitable for use with localtime() / strftime().
 *           Returns -1 if the frame size is invalid or the required
 *           subcode packs (0x62 date, 0x63 time) cannot be found.
 */
int GetDVRecordingTime(BYTE *buf, int len);
