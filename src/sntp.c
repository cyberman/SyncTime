/* sntp.c - SNTP protocol for SyncTime
 *
 * Pure data transformation module: builds NTP request packets,
 * parses NTP response packets, and converts between NTP epoch
 * and AmigaOS epoch timestamps. No I/O, no library calls beyond
 * memset/memcpy.
 */

#include "synctime.h"

/* NTP mode values */
#define NTP_MODE_SERVER    4
#define NTP_MODE_BROADCAST 5

/*
 * sntp_build_request - Build an SNTP client request packet
 *
 * Zeroes all 48 bytes and sets the LI/Version/Mode byte to
 * indicate NTPv3 client mode (0x1B).
 */
void sntp_build_request(UBYTE *packet)
{
    memset(packet, 0, NTP_PACKET_SIZE);
    packet[0] = (NTP_VERSION << 3) | NTP_MODE_CLIENT;  /* 0x1B */
}

/*
 * sntp_parse_response - Parse an SNTP server response packet
 *
 * Validates the response mode and stratum, then extracts the
 * transmit timestamp (bytes 40-47) as big-endian 32-bit values.
 *
 * Returns TRUE on success, FALSE if the packet is invalid.
 */
BOOL sntp_parse_response(const UBYTE *packet, ULONG *ntp_secs, ULONG *ntp_frac)
{
    UBYTE mode;
    UBYTE stratum;
    ULONG secs;
    ULONG frac;

    /* Extract mode from byte 0, bits 0-2 */
    mode = packet[0] & 0x07;

    /* Server response mode should be 4 (server) or 5 (broadcast) */
    if (mode != NTP_MODE_SERVER && mode != NTP_MODE_BROADCAST)
        return FALSE;

    /* Stratum 0 is kiss-of-death */
    stratum = packet[1];
    if (stratum == 0)
        return FALSE;

    /* Extract transmit timestamp seconds (bytes 40-43, big-endian) */
    secs = ((ULONG)packet[40] << 24) |
           ((ULONG)packet[41] << 16) |
           ((ULONG)packet[42] << 8)  |
           ((ULONG)packet[43]);

    /* Extract transmit timestamp fraction (bytes 44-47, big-endian) */
    frac = ((ULONG)packet[44] << 24) |
           ((ULONG)packet[45] << 16) |
           ((ULONG)packet[46] << 8)  |
           ((ULONG)packet[47]);

    /* Server didn't set a transmit timestamp */
    if (secs == 0)
        return FALSE;

    *ntp_secs = secs;
    *ntp_frac = frac;
    return TRUE;
}

/*
 * sntp_ntp_to_amiga - Convert NTP timestamp to Amiga local time
 *
 * Subtracts the NTP-to-Amiga epoch offset (2,461,449,600 seconds
 * from Jan 1 1900 to Jan 1 1978), then applies timezone offset
 * including DST if currently active.
 */
ULONG sntp_ntp_to_amiga(ULONG ntp_secs, const TZEntry *tz)
{
    ULONG utc_secs;
    LONG offset_mins;

    /* Convert from NTP epoch (1900) to Amiga epoch (1978) */
    utc_secs = ntp_secs - NTP_TO_AMIGA_EPOCH;

    /* Get timezone offset including DST if active */
    offset_mins = tz_get_offset_mins(tz, utc_secs);

    /* Apply offset (can be negative for western timezones) */
    return (ULONG)((LONG)utc_secs + (offset_mins * 60));
}
