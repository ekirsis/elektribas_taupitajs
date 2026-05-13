#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "price_api.h"

inline void merge_day(std::vector<PriceEntry>& data, PriceDay const& day)
{
    if (day.prices.size() != 24)
    {
        return;
    }
    data.erase(std::remove_if(data.begin(), data.end(),
                              [&](PriceEntry const& e) { return strcmp(e.date_string, day.date_string) == 0; }),
               data.end());
    for (auto const& h : day.prices)
    {
        PriceEntry pe;
        snprintf(pe.date_string, sizeof(pe.date_string), "%s", day.date_string);
        pe.hour = h.hour;
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
