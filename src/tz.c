/* tz.c - Timezone database functions for SyncTime
 *
 * Provides timezone lookup, region/city enumeration, and DST calculation.
 * Works with the generated tz_table[] from tz_table.c.
 *
 * Amiga epoch is Jan 1, 1978 00:00:00 UTC.
 */

#include "synctime.h"

/* =========================================================================
 * Constants
 * ========================================================================= */

#define SECS_PER_MIN   60
#define SECS_PER_HOUR  3600
#define SECS_PER_DAY   86400

/* Maximum regions (12 should cover all IANA regions) */
#define MAX_REGIONS    17  /* 16 + 1 for NULL terminator */

/* Maximum cities per region */
#define MAX_CITIES     200

/* Amiga epoch year */
#define AMIGA_EPOCH_YEAR 1978

/* =========================================================================
 * Static module state
 * ========================================================================= */

/* Cached region list - built on first call to tz_get_regions */
static const char *region_list[MAX_REGIONS];
static ULONG region_count = 0;
static BOOL regions_initialized = FALSE;

/* Cached city list for current region query */
static const TZEntry *city_list[MAX_CITIES];
static ULONG city_count = 0;
static const char *cached_region = NULL;

/* =========================================================================
 * Days in each month (non-leap year)
 * ========================================================================= */

static const UBYTE days_in_month[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* =========================================================================
 * Helper: check if year is a leap year
 * ========================================================================= */

static BOOL is_leap_year(LONG year)
{
    if ((year % 4) != 0)
        return FALSE;
    if ((year % 100) != 0)
        return TRUE;
    if ((year % 400) != 0)
        return FALSE;
    return TRUE;
}

/* =========================================================================
 * Helper: get days in a month (accounting for leap years)
 * ========================================================================= */

static UBYTE get_days_in_month(LONG year, UBYTE month)
{
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && is_leap_year(year))
        return 29;
    return days_in_month[month - 1];
}

/* =========================================================================
 * Helper: calculate day of week using Zeller's congruence
 *
 * Returns 0=Sunday, 1=Monday, ... 6=Saturday
 * ========================================================================= */

static UBYTE day_of_week(LONG year, UBYTE month, UBYTE day)
{
    LONG y, m, q, k, j, h;

    /* Zeller's congruence treats Jan and Feb as months 13 and 14 of prev year */
    if (month < 3) {
        m = month + 12;
        y = year - 1;
    } else {
        m = month;
        y = year;
    }

    q = day;
    k = y % 100;
    j = y / 100;

    /* Zeller's formula for Gregorian calendar */
    h = (q + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;

    /* Convert from Zeller's 0=Sat to 0=Sun */
    /* Zeller: 0=Sat, 1=Sun, 2=Mon, ... 6=Fri */
    /* We want: 0=Sun, 1=Mon, ... 6=Sat */
    h = ((h + 6) % 7);

    return (UBYTE)h;
}

/* =========================================================================
 * Helper: find day of month for "Nth DOW of month"
 *
 * week: 1-4 = first through fourth occurrence, 5 = last occurrence
 * dow: 0=Sun, 1=Mon, ... 6=Sat
 * Returns day of month (1-31)
 * ========================================================================= */

static UBYTE nth_dow_of_month(LONG year, UBYTE month, UBYTE week, UBYTE dow)
{
    UBYTE first_dow;
    UBYTE first_occurrence;
    UBYTE days_this_month;
    UBYTE day;

    if (month < 1 || month > 12 || week < 1 || week > 5 || dow > 6)
        return 1;  /* Invalid input, return safe default */

    days_this_month = get_days_in_month(year, month);

    /* Find what day of week the 1st of the month is */
    first_dow = day_of_week(year, month, 1);

    /* Find the first occurrence of the target day of week */
    if (dow >= first_dow)
        first_occurrence = 1 + (dow - first_dow);
    else
        first_occurrence = 1 + (7 - first_dow + dow);

    if (week == 5) {
        /* "Last" occurrence - find the last one in the month */
        day = first_occurrence;
        while (day + 7 <= days_this_month)
            day += 7;
        return day;
    }

    /* Nth occurrence */
    day = first_occurrence + (week - 1) * 7;
    if (day > days_this_month)
        day = days_this_month;  /* Shouldn't happen with valid data */

    return day;
}

/* =========================================================================
 * Helper: convert Amiga seconds to year/month/day/hour components
 *
 * Amiga epoch: Jan 1, 1978 00:00:00
 * ========================================================================= */

static void amiga_secs_to_date(ULONG secs, LONG *year, UBYTE *month,
                               UBYTE *day, UBYTE *hour)
{
    ULONG days_remaining;
    ULONG days_in_year;
    LONG y;
    UBYTE m;
    UBYTE dim;

    /* Extract time of day */
    days_remaining = secs / SECS_PER_DAY;
    if (hour)
        *hour = (UBYTE)((secs % SECS_PER_DAY) / SECS_PER_HOUR);

    /* Count years from 1978 */
    y = AMIGA_EPOCH_YEAR;
    for (;;) {
        days_in_year = is_leap_year(y) ? 366 : 365;
        if (days_remaining < days_in_year)
            break;
        days_remaining -= days_in_year;
        y++;
    }

    if (year)
        *year = y;

    /* Count months */
    m = 1;
    for (;;) {
        dim = get_days_in_month(y, m);
        if (days_remaining < dim)
            break;
        days_remaining -= dim;
        m++;
        if (m > 12) {
            m = 12;  /* Safety check */
            break;
        }
    }

    if (month)
        *month = m;
    if (day)
        *day = (UBYTE)(days_remaining + 1);  /* Days are 1-based */
}

/* =========================================================================
 * Helper: convert year/month/day/hour to seconds since Amiga epoch
 *
 * Used to calculate DST transition times
 * ========================================================================= */

static ULONG date_to_amiga_secs(LONG year, UBYTE month, UBYTE day, UBYTE hour)
{
    ULONG secs = 0;
    LONG y;
    UBYTE m;

    /* Count days from years */
    for (y = AMIGA_EPOCH_YEAR; y < year; y++) {
        secs += (is_leap_year(y) ? 366 : 365) * SECS_PER_DAY;
    }

    /* Count days from months */
    for (m = 1; m < month; m++) {
        secs += get_days_in_month(year, m) * SECS_PER_DAY;
    }

    /* Add days (1-based, so subtract 1) */
    secs += (day - 1) * SECS_PER_DAY;

    /* Add hours */
    secs += hour * SECS_PER_HOUR;

    return secs;
}

/* =========================================================================
 * Helper: compare two strings for equality
 * ========================================================================= */

static BOOL str_equal(const char *a, const char *b)
{
    if (!a || !b)
        return FALSE;
    while (*a && *b) {
        if (*a != *b)
            return FALSE;
        a++;
        b++;
    }
    return (*a == *b);
}

/* =========================================================================
 * tz_find_by_name - Find timezone entry by full IANA name
 *
 * Linear search through tz_table[].
 * Returns pointer to entry or NULL if not found.
 * ========================================================================= */

const TZEntry *tz_find_by_name(const char *name)
{
    ULONG i;

    if (!name)
        return NULL;

    for (i = 0; i < tz_table_count; i++) {
        if (str_equal(tz_table[i].name, name))
            return &tz_table[i];
    }

    return NULL;
}

/* =========================================================================
 * tz_get_regions - Get list of unique region names
 *
 * Builds static array of unique regions on first call.
 * Returns array of region strings, sets count via output parameter.
 * ========================================================================= */

const char **tz_get_regions(ULONG *count)
{
    ULONG i, j;
    BOOL found;

    if (!regions_initialized) {
        region_count = 0;

        for (i = 0; i < tz_table_count && region_count < MAX_REGIONS; i++) {
            /* Check if this region is already in our list */
            found = FALSE;
            for (j = 0; j < region_count; j++) {
                if (str_equal(region_list[j], tz_table[i].region)) {
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                region_list[region_count++] = tz_table[i].region;
            }
        }

        /* NULL-terminate for GadTools GTCY_Labels */
        if (region_count < MAX_REGIONS)
            region_list[region_count] = NULL;

        regions_initialized = TRUE;
    }

    if (count)
        *count = region_count;

    return region_list;
}

/* =========================================================================
 * tz_get_cities_for_region - Get timezone entries for a region
 *
 * Builds static array of TZEntry pointers for given region.
 * Returns array, sets count via output parameter.
 * ========================================================================= */

const TZEntry **tz_get_cities_for_region(const char *region, ULONG *count)
{
    ULONG i;

    /* Check if we already have this region cached */
    if (cached_region && str_equal(cached_region, region)) {
        if (count)
            *count = city_count;
        return city_list;
    }

    /* Build new city list for this region */
    city_count = 0;
    cached_region = region;

    if (!region) {
        if (count)
            *count = 0;
        return city_list;
    }

    for (i = 0; i < tz_table_count && city_count < MAX_CITIES; i++) {
        if (str_equal(tz_table[i].region, region)) {
            city_list[city_count++] = &tz_table[i];
        }
    }

    if (count)
        *count = city_count;

    return city_list;
}

/* =========================================================================
 * tz_is_dst_active - Check if DST is active for given UTC time
 *
 * Handles both northern hemisphere (DST spring-fall) and southern
 * hemisphere (DST fall-spring wrapping year).
 *
 * utc_secs is Amiga epoch seconds (since Jan 1, 1978).
 * Returns TRUE if DST is currently active, FALSE otherwise.
 * ========================================================================= */

BOOL tz_is_dst_active(const TZEntry *tz, ULONG utc_secs)
{
    LONG year;
    UBYTE month, day, hour;
    ULONG local_secs;
    UBYTE dst_start_day, dst_end_day;
    ULONG dst_start_secs, dst_end_secs;

    if (!tz)
        return FALSE;

    /* No DST if dst_start_month is 0 or dst_offset is 0 */
    if (tz->dst_start_month == 0 || tz->dst_offset_mins == 0)
        return FALSE;

    /* Convert UTC to local standard time for comparison */
    /* We add the standard offset (which may be negative for western zones) */
    if (tz->std_offset_mins >= 0) {
        local_secs = utc_secs + (ULONG)(tz->std_offset_mins * SECS_PER_MIN);
    } else {
        ULONG offset_secs = (ULONG)((-tz->std_offset_mins) * SECS_PER_MIN);
        if (utc_secs < offset_secs)
            return FALSE;  /* Time too early to calculate DST */
        local_secs = utc_secs - offset_secs;
    }

    /* Get current date/time components in local standard time */
    amiga_secs_to_date(local_secs, &year, &month, &day, &hour);

    /* Calculate DST transition dates for this year */
    dst_start_day = nth_dow_of_month(year, tz->dst_start_month,
                                     tz->dst_start_week, tz->dst_start_dow);
    dst_end_day = nth_dow_of_month(year, tz->dst_end_month,
                                   tz->dst_end_week, tz->dst_end_dow);

    /* Calculate transition times in local standard seconds since epoch */
    dst_start_secs = date_to_amiga_secs(year, tz->dst_start_month,
                                        dst_start_day, tz->dst_start_hour);
    dst_end_secs = date_to_amiga_secs(year, tz->dst_end_month,
                                      dst_end_day, tz->dst_end_hour);

    /* Northern hemisphere: DST start month < DST end month
     * (e.g., March to November in USA)
     * DST is active when: start <= now < end
     */
    if (tz->dst_start_month < tz->dst_end_month) {
        return (local_secs >= dst_start_secs && local_secs < dst_end_secs);
    }

    /* Southern hemisphere: DST start month > DST end month
     * (e.g., October to April in Australia)
     * DST is active when: now >= start OR now < end
     * This handles the year wrap (DST spans Dec 31/Jan 1)
     */
    return (local_secs >= dst_start_secs || local_secs < dst_end_secs);
}

/* =========================================================================
 * tz_get_offset_mins - Get current offset from UTC in minutes
 *
 * Returns std_offset_mins + dst_offset_mins if DST active,
 * otherwise returns std_offset_mins.
 * ========================================================================= */

LONG tz_get_offset_mins(const TZEntry *tz, ULONG utc_secs)
{
    if (!tz)
        return 0;

    if (tz_is_dst_active(tz, utc_secs))
        return (LONG)tz->std_offset_mins + (LONG)tz->dst_offset_mins;

    return (LONG)tz->std_offset_mins;
}
