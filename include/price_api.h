#pragma once

#include <stdint.h>
#include <vector>

// Prices are stored with an absolute UTC timestamp.  A local date is only
// used for grouping/pruning.  On a DST fall-back day two entries share the
// local wall-clock hour 03:00, so a (date, hour) key is not sufficient.
struct HourPrice
{
    int64_t start_utc; // seconds since Unix epoch
    float eur_mwh;
};

struct PriceDay
{
    char date_string[11]; // requested local civil date
    std::vector<HourPrice> prices;
};

struct PriceEntry
{
    char date_string[11]; // local civil date this entry belongs to
    int64_t start_utc; // seconds since Unix epoch
    float eur_mwh;
};

// Find the hourly entry that covers `at_utc`.  Intervals are half-open:
// [start, start + 1 hour).  This naturally handles both the duplicated
// 03:00 hour in autumn and the missing 03:00 hour in spring.
// NOLINTNEXTLINE(misc-unused-parameters) — false positive: <template> confused with parameter 'std'
inline float find_price_at(const std::vector<PriceEntry>& data, int64_t at_utc, bool& found)
{
    PriceEntry const* best = nullptr;
    for (const auto& e : data)
    {
        if (e.start_utc <= at_utc && at_utc < e.start_utc + 3600)
        {
            if (best == nullptr || e.start_utc > best->start_utc)
            {
                best = &e;
            }
        }
    }

    if (best == nullptr)
    {
        found = false;
        return 0.0F;
    }

    found = true;
    return best->eur_mwh * 0.1f;
}

// The display shows the current hour plus the previous and next two hours.
// Using absolute time rather than local-hour arithmetic makes this correct
// across both DST transitions.
// NOLINTNEXTLINE(misc-unused-parameters) — false positive: <template> confused with parameter 'std'
inline bool can_draw_prices_at(const std::vector<PriceEntry>& data, int64_t now_utc)
{
    for (int offset = -1; offset <= 4; ++offset)
    {
        bool found = false;
        find_price_at(data, now_utc + static_cast<int64_t>(offset) * 3600, found);
        if (!found)
        {
            return false;
        }
    }
    return true;
}
