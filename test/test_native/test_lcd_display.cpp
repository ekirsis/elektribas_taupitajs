#include "../doctest.h"
#include "price_api.h"

#include <cstring>
#include <vector>

namespace {

PriceEntry make_entry(const char* date, uint8_t hour, float eur_mwh)
{
    PriceEntry e;
    std::strncpy(e.date_string, date, sizeof(e.date_string) - 1);
    e.date_string[sizeof(e.date_string) - 1] = '\0';
    e.hour = hour;
    e.eur_mwh = eur_mwh;
    return e;
}

} // namespace

// --- empty data ---

TEST_CASE("can_draw_prices_for_dates returns false for empty data")
{
    std::vector<PriceEntry> data;
    CHECK_FALSE(can_draw_prices_for_dates(data, 12, "2026-06-01"));
}

// --- today-only data (within bounds) ---

TEST_CASE("can_draw_prices_for_dates true when today covers all 6 hours")
{
    // current_hour=10 → need hours 9..14 (all within today 0-23)
    std::vector<PriceEntry> data;
    for (int h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", static_cast<uint8_t>(h), 10.0F));

    CHECK(can_draw_prices_for_dates(data, 10, "2026-06-01"));
}

TEST_CASE("can_draw_prices_for_dates false when a required today hour is missing")
{
    // current_hour=10 → need hours 9..14. Remove hour 12.
    std::vector<PriceEntry> data;
    for (int h = 0; h < 24; ++h)
    {
        if (h == 12)
        {
            continue;
        }
        data.push_back(make_entry("2026-06-01", static_cast<uint8_t>(h), 10.0F));
    }

    CHECK_FALSE(can_draw_prices_for_dates(data, 10, "2026-06-01"));
}

// --- cross-midnight: window extends into tomorrow ---

TEST_CASE("can_draw_prices_for_dates true when today+tomorrow cover the window")
{
    // current_hour=22 → need hours 21..26:
    //   today:  21, 22, 23
    //   tomorrow: 24→0, 25→1, 26→2
    std::vector<PriceEntry> data;
    for (int h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", static_cast<uint8_t>(h), 10.0F));
    // tomorrow hour 0 = needle 0, hour 6 = needle 6
    for (int h = 0; h <= 6; ++h)
        data.push_back(make_entry("2026-06-02", static_cast<uint8_t>(h), 20.0F));

    CHECK(can_draw_prices_for_dates(data, 22, "2026-06-01"));
}

TEST_CASE("can_draw_prices_for_dates false when tomorrow data is missing (cross-midnight)")
{
    // current_hour=22 → need hours 21..26.
    // Today has 21,22,23. Tomorrow has only hours 0..1 (missing hour 2 for h=26).
    std::vector<PriceEntry> data;
    for (int h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", static_cast<uint8_t>(h), 10.0F));
    for (int h = 0; h <= 1; ++h)
        data.push_back(make_entry("2026-06-02", static_cast<uint8_t>(h), 20.0F));

    CHECK_FALSE(can_draw_prices_for_dates(data, 22, "2026-06-01"));
}

// --- date disambiguation ---

TEST_CASE("can_draw_prices_for_dates does NOT match today's hour for tomorrow's slot")
{
    // current_hour=22 → need hour 24 (tomorrow hour 0).
    // Data has today hour 0, but NOT tomorrow hour 0.
    // The old buggy code would match today's hour 0 → return true.
    // The fix requires matching by both date AND hour.
    std::vector<PriceEntry> data;
    data.push_back(make_entry("2026-06-01", 0, 10.0F)); // today hour 0
    data.push_back(make_entry("2026-06-01", 21, 10.0F)); // today hour 21
    data.push_back(make_entry("2026-06-01", 22, 10.0F)); // today hour 22
    data.push_back(make_entry("2026-06-01", 23, 10.0F)); // today hour 23
    // NO tomorrow data

    CHECK_FALSE(can_draw_prices_for_dates(data, 22, "2026-06-01"));
}

// --- boundary: current_hour = 0 (window starts before 0, needs yesterday's hour 23) ---

TEST_CASE("can_draw_prices_for_dates at hour 0 fails without yesterday data")
{
    // current_hour=0 → need hours -1..4. h=-1 requires yesterday's hour 23.
    // With only today data, hour -1 is not found.
    std::vector<PriceEntry> data;
    for (int h = 0; h <= 8; ++h)
        data.push_back(make_entry("2026-06-01", static_cast<uint8_t>(h), 10.0F));

    CHECK_FALSE(can_draw_prices_for_dates(data, 0, "2026-06-01"));
}

TEST_CASE("can_draw_prices_for_dates at hour 0 passes with yesterday+today data")
{
    // current_hour=0 → need hours -1..4. Yesterday hour 23 + today hours 0..4.
    // wallclock_today="2026-06-01" → offset -1 = "2026-05-31"
    std::vector<PriceEntry> data;
    data.push_back(make_entry("2026-05-31", 23, 5.0F)); // yesterday hour 23
    for (int h = 0; h <= 8; ++h)
        data.push_back(make_entry("2026-06-01", static_cast<uint8_t>(h), 10.0F));

    CHECK(can_draw_prices_for_dates(data, 0, "2026-06-01"));
}

// --- boundary: current_hour close to 48 limit ---

TEST_CASE("can_draw_prices_for_dates at hour 23 covers 6-hour window")
{
    // current_hour=23 → need hours 22..27. h=24..27 → tomorrow 0..3.
    std::vector<PriceEntry> data;
    for (int h = 22; h <= 23; ++h)
        data.push_back(make_entry("2026-06-01", static_cast<uint8_t>(h), 10.0F));
    for (int h = 0; h <= 3; ++h)
        data.push_back(make_entry("2026-06-02", static_cast<uint8_t>(h), 20.0F));

    CHECK(can_draw_prices_for_dates(data, 23, "2026-06-01"));
}

TEST_CASE("can_draw_prices_for_dates at hour 23 fails when tomorrow missing hours for 6-hour window")
{
    // Need hours 22..27 → tomorrow 0..3. Only have tomorrow 0..2 → h=27→needle 3 missing.
    std::vector<PriceEntry> data;
    for (int h = 22; h <= 23; ++h)
        data.push_back(make_entry("2026-06-01", static_cast<uint8_t>(h), 10.0F));
    for (int h = 0; h <= 2; ++h)
        data.push_back(make_entry("2026-06-02", static_cast<uint8_t>(h), 20.0F));

    CHECK_FALSE(can_draw_prices_for_dates(data, 23, "2026-06-01"));
}

// --- strict validation: no nullptr relaxation ---

TEST_CASE("strict validation at hour 22 fails without tomorrow data")
{
    // Display needs tomorrow hours 0,1,2 (view hours 24,25,26).
    // Today-only data should fail the strict check.
    std::vector<PriceEntry> data;
    for (int h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", static_cast<uint8_t>(h), 10.0F));
    CHECK_FALSE(can_draw_prices_for_dates(data, 22, "2026-06-01"));
}

TEST_CASE("strict validation at hour 22 fails with incomplete tomorrow data")
{
    // Tomorrow has only hour 0, but view needs hours 0,1,2 (h=24,25,26).
    std::vector<PriceEntry> data;
    for (int h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", static_cast<uint8_t>(h), 10.0F));
    data.push_back(make_entry("2026-06-02", 0, 20.0F)); // only hour 0
    CHECK_FALSE(can_draw_prices_for_dates(data, 22, "2026-06-01"));
}

TEST_CASE("strict validation at hour 22 passes with sufficient tomorrow data")
{
    // Tomorrow has hours 0,1,2 — covers all 6 view hours.
    std::vector<PriceEntry> data;
    for (int h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", static_cast<uint8_t>(h), 10.0F));
    for (int h = 0; h <= 2; ++h)
        data.push_back(make_entry("2026-06-02", static_cast<uint8_t>(h), 20.0F));
    CHECK(can_draw_prices_for_dates(data, 22, "2026-06-01"));
}
