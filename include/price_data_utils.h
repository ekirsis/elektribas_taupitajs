#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "price_api.h"
#include "time_utils.h"

// A local civil date has 23, 24 or 25 hourly slots depending on DST.
// Returns 0 for an invalid date.
inline int expected_hours_for_date(const char* date_string)
{
    return time_utils::local_day_hours(date_string);
}

inline void merge_day(std::vector<PriceEntry>& data, PriceDay const& day)
{
    if (day.date_string[0] == '\0')
    {
        return;
    }

    int const expected_hours = expected_hours_for_date(day.date_string);
    if (expected_hours <= 0 || static_cast<int>(day.prices.size()) != expected_hours)
    {
        return;
    }

    data.erase(std::remove_if(data.begin(), data.end(),
                              [&](PriceEntry const& e) { return strcmp(e.date_string, day.date_string) == 0; }),
               data.end());
    for (auto const& h : day.prices)
    {
        PriceEntry pe{};
        snprintf(pe.date_string, sizeof(pe.date_string), "%s", day.date_string);
        pe.start_utc = h.start_utc;
        pe.eur_mwh = h.eur_mwh;
        data.push_back(pe);
    }
}

inline int count_entries(std::vector<PriceEntry> const& data, char const* date_str)
{
    int n = 0;
    for (auto const& e : data)
    {
        if (strcmp(e.date_string, date_str) == 0)
        {
            ++n;
        }
    }
    return n;
}
