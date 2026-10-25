#pragma once

// Time helpers shared by the firmware and native tests.
//
// Price timestamps are kept as absolute Unix seconds.  Local-time
// conversion is only done when formatting something for a human (the
// display and the API request date).  That is the only way to represent a
// DST day correctly: a local day can have 23, 24, or 25 hourly slots, and
// the two 03:00 slots on the autumn change have different absolute times.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace time_utils {

// Days since 1970-01-01 for a civil date.  Howard Hinnant's algorithm;
// avoids relying on timegm(), which is not available on every target.
inline int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    int64_t const era = (year >= 0 ? year : year - 399) / 400;
    unsigned const year_of_era = static_cast<unsigned>(year - era * 400);
    unsigned const day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned const day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + static_cast<int64_t>(day_of_era) - 719468;
}

inline int64_t utc_from_civil(int year, int month, int day, int hour = 0, int minute = 0, int second = 0)
{
    return days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 +
           static_cast<int64_t>(hour) * 3600 + static_cast<int64_t>(minute) * 60 + second;
}

inline bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

inline int days_in_month(int year, int month)
{
    static constexpr int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
    {
        return 0;
    }
    return DAYS[month - 1] + (month == 2 && is_leap_year(year) ? 1 : 0);
}

// Parse an ISO-8601 timestamp with an explicit offset, e.g.
//   2026-10-25T03:00:00+03:00
//   2026-10-25T03:00:00+02:00
//   2026-10-25T00:00:00Z
// Fractional seconds are accepted and truncated.  A timestamp without a
// timezone is rejected: treating it as local time would make the result
// depend on the device's current TZ setting.
inline bool parse_iso8601_utc(const char* text, int64_t& out_utc)
{
    if (text == nullptr)
    {
        return false;
    }

    // "YYYY-MM-DDTHH:MM:SS" is 19 characters.  sscanf() reports success for a
    // truncated timestamp ("...T03:00:0" still fills six fields), so the
    // length has to be checked before indexing below.
    if (std::strlen(text) < 19)
    {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second) != 6)
    {
        return false;
    }
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > days_in_month(year, month) || hour < 0 ||
        hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60)
    {
        return false;
    }

    char const* cursor = text + 19; // YYYY-MM-DDTHH:MM:SS
    if (*cursor == '.')
    {
        ++cursor;
        while (*cursor >= '0' && *cursor <= '9')
        {
            ++cursor;
        }
    }

    int offset_seconds = 0;
    if (*cursor == 'Z' && cursor[1] == '\0')
    {
        offset_seconds = 0;
    }
    else if ((*cursor == '+' || *cursor == '-') && cursor[1] != '\0')
    {
        int offset_hour = 0;
        int offset_minute = 0;
        char const sign = *cursor;
        if (std::strlen(cursor) != 6 || std::sscanf(cursor + 1, "%2d:%2d", &offset_hour, &offset_minute) != 2 ||
            offset_hour > 14 || offset_minute > 59)
        {
            return false;
        }
        offset_seconds = (offset_hour * 3600 + offset_minute * 60) * (sign == '-' ? -1 : 1);
    }
    else
    {
        return false;
    }

    out_utc = utc_from_civil(year, month, day, hour, minute, second) - offset_seconds;
    return true;
}

// Number of hourly slots in a local civil date (23, 24 or 25).  mktime()
// with tm_isdst = -1 asks the C library to resolve the offset for each
// boundary, which is exactly what is needed on DST change days.  Impossible
// dates are rejected before mktime(), which would otherwise normalise them
// (2026-02-30 silently becomes March 2nd).  Depends on the configured TZ.
inline int local_day_hours(const char* date_string)
{
    if (date_string == nullptr)
    {
        return 0;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (std::sscanf(date_string, "%4d-%2d-%2d", &year, &month, &day) != 3)
    {
        return 0;
    }
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > days_in_month(year, month))
    {
        return 0;
    }

    struct tm start_tm = {};
    start_tm.tm_year = year - 1900;
    start_tm.tm_mon = month - 1;
    start_tm.tm_mday = day;
    start_tm.tm_isdst = -1;
    time_t const start = mktime(&start_tm);
    if (start == static_cast<time_t>(-1))
    {
        return 0;
    }

    struct tm end_tm = start_tm;
    end_tm.tm_mday += 1;
    end_tm.tm_isdst = -1;
    time_t const end = mktime(&end_tm);
    if (end == static_cast<time_t>(-1) || end <= start)
    {
        return 0;
    }

    return static_cast<int>((end - start) / 3600);
}

// Format the local civil date at `now + day_offset`.  tm_isdst must be
// reset to -1 before mktime(); otherwise mktime() reuses the DST flag from
// `now`, which produces the wrong date on the two change days.
inline void format_local_date_at(time_t now, int day_offset, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0)
    {
        return;
    }

    struct tm local_tm = {};
    localtime_r(&now, &local_tm);
    local_tm.tm_mday += day_offset;
    local_tm.tm_isdst = -1;
    mktime(&local_tm);

    std::snprintf(out, out_size, "%04d-%02d-%02d", local_tm.tm_year + 1900, local_tm.tm_mon + 1,
                  local_tm.tm_mday);
}

// Format the wall-clock range of the hourly slot [start_utc, end_utc) for the
// LCD: "HH:00-HH:00", or just "HH:00" when the slot starts and ends on the
// same wall-clock hour.  That collapse happens on the autumn fall-back day,
// where the first 03:00 slot ends at 03:00 again because the clock jumped
// backwards.  Minutes are always :00 because slots are aligned to whole hours.
inline void format_hour_range(int64_t start_utc, int64_t end_utc, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0)
    {
        return;
    }

    time_t const start = static_cast<time_t>(start_utc);
    time_t const end = static_cast<time_t>(end_utc);

    struct tm start_tm = {};
    localtime_r(&start, &start_tm);
    struct tm end_tm = {};
    localtime_r(&end, &end_tm);

    if (start_tm.tm_hour == end_tm.tm_hour)
    {
        std::snprintf(out, out_size, "%02d:00", start_tm.tm_hour);
    }
    else
    {
        std::snprintf(out, out_size, "%02d:00-%02d:00", start_tm.tm_hour, end_tm.tm_hour);
    }
}

} // namespace time_utils
