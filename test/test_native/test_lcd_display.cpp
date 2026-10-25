#include "../doctest.h"
#include "price_api.h"
#include "time_utils.h"

#include <vector>

namespace {

PriceEntry make_entry(const char* date, int hour, float eur_mwh)
{
    int year = 0;
    int month = 0;
    int day = 0;
    std::sscanf(date, "%4d-%2d-%2d", &year, &month, &day);

    PriceEntry e{};
    std::strncpy(e.date_string, date, sizeof(e.date_string) - 1);
    e.date_string[sizeof(e.date_string) - 1] = '\0';
    e.start_utc = time_utils::utc_from_civil(year, month, day, hour);
    e.eur_mwh = eur_mwh;
    return e;
}

std::vector<PriceEntry> make_hourly_entries(int64_t start_utc, int count, float eur_mwh)
{
    std::vector<PriceEntry> data;
    for (int i = 0; i < count; ++i)
    {
        PriceEntry e{};
        e.start_utc = start_utc + static_cast<int64_t>(i) * 3600;
        e.eur_mwh = eur_mwh;
        data.push_back(e);
    }
    return data;
}

} // namespace

// --- empty data ---

TEST_CASE("can_draw_prices_at returns false for empty data")
{
    std::vector<PriceEntry> data;
    CHECK_FALSE(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 6, 1, 12, 30)));
}

// --- normal day ---

TEST_CASE("can_draw_prices_at true when today covers all 6 hours")
{
    // now=10:30 → need 09:00..14:00
    std::vector<PriceEntry> data;
    for (int h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));

    CHECK(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 6, 1, 10, 30)));
}

TEST_CASE("can_draw_prices_at false when a required hour is missing")
{
    // now=10:30 → need 09:00..14:00. Remove hour 12.
    std::vector<PriceEntry> data;
    for (int h = 0; h < 24; ++h)
    {
        if (h == 12)
        {
            continue;
        }
        data.push_back(make_entry("2026-06-01", h, 10.0F));
    }

    CHECK_FALSE(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 6, 1, 10, 30)));
}

// --- cross-midnight ---

TEST_CASE("can_draw_prices_at true when today+tomorrow cover the window")
{
    // now=22:30 → need 21:00..02:00 next day
    std::vector<PriceEntry> data;
    for (int h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));
    for (int h = 0; h <= 6; ++h)
        data.push_back(make_entry("2026-06-02", h, 20.0F));

    CHECK(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 6, 1, 22, 30)));
}

TEST_CASE("can_draw_prices_at false when tomorrow data is missing")
{
    // now=22:30 → need tomorrow 00:00..02:00. Only 00:00 and 01:00 exist.
    std::vector<PriceEntry> data;
    for (int h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));
    for (int h = 0; h <= 1; ++h)
        data.push_back(make_entry("2026-06-02", h, 20.0F));

    CHECK_FALSE(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 6, 1, 22, 30)));
}

TEST_CASE("can_draw_prices_at does NOT match today's hour for tomorrow's slot")
{
    // now=22:30 → needs tomorrow 00:00..02:00. Data has today's 00:00,
    // which the old (date, hour) lookup could accidentally match.
    std::vector<PriceEntry> data;
    data.push_back(make_entry("2026-06-01", 0, 10.0F));
    data.push_back(make_entry("2026-06-01", 21, 10.0F));
    data.push_back(make_entry("2026-06-01", 22, 10.0F));
    data.push_back(make_entry("2026-06-01", 23, 10.0F));

    CHECK_FALSE(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 6, 1, 22, 30)));
}

// --- midnight crossover ---

TEST_CASE("can_draw_prices_at at 00:30 fails without yesterday's last hour")
{
    std::vector<PriceEntry> data;
    for (int h = 0; h <= 8; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));

    CHECK_FALSE(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 6, 1, 0, 30)));
}

TEST_CASE("can_draw_prices_at at 00:30 passes with yesterday+today data")
{
    std::vector<PriceEntry> data;
    data.push_back(make_entry("2026-05-31", 23, 5.0F));
    for (int h = 0; h <= 8; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));

    CHECK(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 6, 1, 0, 30)));
}

TEST_CASE("can_draw_prices_at at 23:30 covers the six-hour window")
{
    std::vector<PriceEntry> data;
    for (int h = 22; h <= 23; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));
    for (int h = 0; h <= 3; ++h)
        data.push_back(make_entry("2026-06-02", h, 20.0F));

    CHECK(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 6, 1, 23, 30)));
}

TEST_CASE("can_draw_prices_at at 23:30 fails when tomorrow is incomplete")
{
    std::vector<PriceEntry> data;
    for (int h = 22; h <= 23; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));
    for (int h = 0; h <= 2; ++h)
        data.push_back(make_entry("2026-06-02", h, 20.0F));

    CHECK_FALSE(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 6, 1, 23, 30)));
}

// --- DST transitions ---

TEST_CASE("can_draw_prices_at works during the duplicated 03:00 hour in autumn")
{
    // 2026-10-25 is a 25-hour day in Riga.  The 25 hourly slots start at
    // 2026-10-24 21:00 UTC (00:00 EEST) and end at 2026-10-25 21:00 UTC.
    auto const data = make_hourly_entries(time_utils::utc_from_civil(2026, 10, 24, 21), 25, 10.0F);

    // 03:30 EEST (first 03:00) and 03:30 EET (second 03:00).
    CHECK(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 10, 25, 0, 30)));
    CHECK(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 10, 25, 1, 30)));
}

TEST_CASE("can_draw_prices_at works during the missing 03:00 hour in spring")
{
    // 2026-03-29 is a 23-hour day in Riga.  The 23 hourly slots start at
    // 2026-03-28 22:00 UTC (00:00 EET).
    auto const data = make_hourly_entries(time_utils::utc_from_civil(2026, 3, 28, 22), 23, 10.0F);

    // 02:30 EET: the previous slot is 01:30 EET; the next slot is
    // 04:30 EEST.  There is no 03:00 hour.
    CHECK(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 3, 29, 0, 30)));

    // 04:30 EEST: previous is 02:30 EET, current 04:30 EEST.
    CHECK(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 3, 29, 1, 30)));
}

TEST_CASE("can_draw_prices_at fails if a DST-day slot is genuinely missing")
{
    auto data = make_hourly_entries(time_utils::utc_from_civil(2026, 10, 24, 21), 25, 10.0F);
    data.erase(data.begin() + 4); // remove the second 03:00 slot

    CHECK_FALSE(can_draw_prices_at(data, time_utils::utc_from_civil(2026, 10, 25, 1, 30)));
}
