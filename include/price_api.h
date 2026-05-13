#pragma once

#include <cstdio>
#include <ctime>
#include <stdint.h>
#include <string.h>
#include <vector>

struct HourPrice
{
    uint8_t hour;
    float eur_mwh;
};

struct PriceDay
{
    char date_string[11];
    std::vector<HourPrice> prices;
};

struct PriceEntry
{
    char date_string[11];
    uint8_t hour;
    float eur_mwh;
};

inline void offset_date_string(const char* base_date, int day_offset, char* out, size_t out_size)
{
    struct tm tm = {};
    sscanf(base_date, "%4d-%2d-%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_mday += day_offset;
    tm.tm_isdst = -1;
    mktime(&tm);
    strftime(out, out_size, "%Y-%m-%d", &tm);
}

inline bool resolve_view_hour(
    int view_hour, const char* wallclock_prev, const char* wallclock_curr, const char* wallclock_next,
    // NOLINTNEXTLINE(readability-named-parameter) — all params are named; false positive from continued-line def
    const char*& out_date, uint8_t& out_needle)
{
    if (view_hour >= 48)
    {
        return false;
    }
    if (view_hour < 0)
    { // NOLINT(bugprone-branch-clone) — false positive: each branch assigns different values
        out_date = wallclock_prev;
        out_needle = static_cast<uint8_t>(view_hour + 24);
    }
    else if (view_hour < 24)
    {
        out_date = wallclock_curr;
        out_needle = static_cast<uint8_t>(view_hour);
    }
    else
    {
        out_date = wallclock_next;
        out_needle = static_cast<uint8_t>(view_hour - 24);
    }
    return true;
}

// NOLINTNEXTLINE(misc-unused-parameters) — false positive: <template> confused with parameter 'std'
inline bool can_draw_prices_for_dates(const std::vector<PriceEntry>& data, int current_hour,
                                      const char* wallclock_today)
{
    char wallclock_prev[11];
    char wallclock_next[11];
    offset_date_string(wallclock_today, -1, wallclock_prev, sizeof(wallclock_prev));
    offset_date_string(wallclock_today, 1, wallclock_next, sizeof(wallclock_next));

    for (int view_hour = current_hour - 1; view_hour <= current_hour + 4; ++view_hour)
    {
        const char* date;
        uint8_t needle;
        if (!resolve_view_hour(view_hour, wallclock_prev, wallclock_today, wallclock_next, date, needle))
        {
            return false;
        }
        bool found = false;
        for (const auto& e : data)
        {
            if (strcmp(e.date_string, date) == 0 && e.hour == needle)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}

inline float find_price(const std::vector<PriceEntry>& data, const char* date, uint8_t hour, bool& found)
{
    for (const auto& e : data)
    {
        if (strcmp(e.date_string, date) == 0 && e.hour == hour)
        {
            found = true;
            return e.eur_mwh * 0.1f;
        }
    }
    found = false;
    return 0.0F;
}
