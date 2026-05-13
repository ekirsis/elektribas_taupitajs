#include "../doctest.h"
#include "price_data_utils.h"

#include <cstring>

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

PriceDay make_day(const char* date, const std::vector<HourPrice>& prices)
{
    PriceDay d;
    std::strncpy(d.date_string, date, sizeof(d.date_string) - 1);
    d.date_string[sizeof(d.date_string) - 1] = '\0';
    d.prices = prices;
    return d;
}

PriceDay make_full_day(const char* date, float base_price)
{
    std::vector<HourPrice> prices;
    prices.reserve(24);
    for (uint8_t h = 0; h < 24; ++h)
    {
        prices.push_back({h, base_price + static_cast<float>(h)});
    }
    return make_day(date, prices);
}

} // namespace

// ── count_entries ────────────────────────────────────────────────────────

TEST_CASE("count_entries returns 0 for empty data")
{
    std::vector<PriceEntry> data;
    CHECK_EQ(count_entries(data, "2026-06-01"), 0);
}

TEST_CASE("count_entries returns 24 for a full day")
{
    std::vector<PriceEntry> data;
    for (uint8_t h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));

    CHECK_EQ(count_entries(data, "2026-06-01"), 24);
}

TEST_CASE("count_entries returns 0 for date not present")
{
    std::vector<PriceEntry> data;
    for (uint8_t h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));

    CHECK_EQ(count_entries(data, "2026-06-02"), 0);
}

TEST_CASE("count_entries is correct for partial day")
{
    std::vector<PriceEntry> data;
    for (uint8_t h = 0; h < 6; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));

    CHECK_EQ(count_entries(data, "2026-06-01"), 6);
}

TEST_CASE("count_entries distinguishes multiple dates")
{
    std::vector<PriceEntry> data;
    for (uint8_t h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));
    for (uint8_t h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-02", h, 20.0F));

    CHECK_EQ(count_entries(data, "2026-06-01"), 24);
    CHECK_EQ(count_entries(data, "2026-06-02"), 24);
    CHECK_EQ(count_entries(data, "2026-06-03"), 0);
}

// ── merge_day (basic) ────────────────────────────────────────────────────

TEST_CASE("merge_day adds 24 entries into empty data")
{
    std::vector<PriceEntry> data;
    PriceDay day = make_full_day("2026-06-01", 10.0F);

    merge_day(data, day);

    CHECK_EQ(count_entries(data, "2026-06-01"), 24);
    CHECK_EQ(data.size(), 24u);
}

TEST_CASE("merge_day replaces existing entries for same date")
{
    std::vector<PriceEntry> data;
    for (uint8_t h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));

    // Merge same date with different prices
    PriceDay day = make_full_day("2026-06-01", 99.0F);
    merge_day(data, day);

    CHECK_EQ(count_entries(data, "2026-06-01"), 24);
    // All entries should have the new price
    for (auto const& e : data)
    {
        if (strcmp(e.date_string, "2026-06-01") == 0)
        {
            CHECK(e.eur_mwh >= 99.0F);
        }
    }
}

TEST_CASE("merge_day replaces partial data with full day")
{
    std::vector<PriceEntry> data;
    // Only hours 0-5 present
    for (uint8_t h = 0; h < 6; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));

    PriceDay day = make_full_day("2026-06-01", 20.0F);
    merge_day(data, day);

    CHECK_EQ(count_entries(data, "2026-06-01"), 24);
    CHECK_EQ(data.size(), 24u);
}

TEST_CASE("merge_day does not affect other dates")
{
    std::vector<PriceEntry> data;
    for (uint8_t h = 0; h < 24; ++h)
    {
        data.push_back(make_entry("2026-06-01", h, 10.0F));
        data.push_back(make_entry("2026-06-02", h, 20.0F));
    }

    PriceDay day = make_full_day("2026-06-01", 99.0F);
    merge_day(data, day);

    CHECK_EQ(count_entries(data, "2026-06-01"), 24);
    CHECK_EQ(count_entries(data, "2026-06-02"), 24);
    CHECK_EQ(data.size(), 48u);
}

TEST_CASE("merge_day allows multiple dates to coexist")
{
    std::vector<PriceEntry> data;
    merge_day(data, make_full_day("2026-06-01", 10.0F));
    merge_day(data, make_full_day("2026-06-02", 20.0F));

    CHECK_EQ(count_entries(data, "2026-06-01"), 24);
    CHECK_EQ(count_entries(data, "2026-06-02"), 24);
    CHECK_EQ(data.size(), 48u);
}

TEST_CASE("merge_day with incomplete data is a no-op — preserves existing entries")
{
    std::vector<PriceEntry> data;
    for (uint8_t h = 0; h < 24; ++h)
        data.push_back(make_entry("2026-06-01", h, 10.0F));

    PriceDay partial_day = make_day("2026-06-01", {});
    merge_day(data, partial_day);

    CHECK_EQ(count_entries(data, "2026-06-01"), 24);
}

TEST_CASE("merge_day is idempotent: merging same day twice produces 24 entries not 48")
{
    std::vector<PriceEntry> data;
    PriceDay day = make_full_day("2026-06-01", 10.0F);

    merge_day(data, day);
    merge_day(data, day);

    CHECK_EQ(count_entries(data, "2026-06-01"), 24);
    CHECK_EQ(data.size(), 24u);
}

// ── Combined merge + count (integration edge cases) ──────────────────────

TEST_CASE("merge then count works end-to-end")
{
    std::vector<PriceEntry> data;
    merge_day(data, make_full_day("2026-06-01", 10.0F));
    merge_day(data, make_full_day("2026-06-02", 20.0F));

    CHECK_EQ(count_entries(data, "2026-06-01"), 24);
    CHECK_EQ(count_entries(data, "2026-06-02"), 24);
}

TEST_CASE("replace today, keep yesterday, count both")
{
    std::vector<PriceEntry> data;
    merge_day(data, make_full_day("2026-05-31", 5.0F));
    merge_day(data, make_full_day("2026-06-01", 10.0F));

    // Replace today with new prices
    merge_day(data, make_full_day("2026-06-01", 99.0F));

    CHECK_EQ(count_entries(data, "2026-05-31"), 24);
    CHECK_EQ(count_entries(data, "2026-06-01"), 24);
    CHECK_EQ(data.size(), 48u);

    // Today's prices should be the new ones
    bool found = false;
    float v = find_price(data, "2026-06-01", 0, found);
    CHECK(found);
    CHECK_EQ(v, doctest::Approx(9.9F)); // 99 * 0.1
}

TEST_CASE("sequential merges with partial data simulate real-world fetch pattern")
{
    std::vector<PriceEntry> data;

    // Yesterday already merged
    merge_day(data, make_full_day("2026-05-31", 5.0F));

    // Today fetch succeeds with full 24 hours
    merge_day(data, make_full_day("2026-06-01", 10.0F));
    CHECK_EQ(count_entries(data, "2026-06-01"), 24);

    // Tomorrow fetch with partial data (only hours 0-5 available from API)
    // Incomplete data is rejected — merge_day is a no-op
    {
        std::vector<HourPrice> partial;
        for (uint8_t h = 0; h < 6; ++h)
            partial.push_back({h, 30.0F + static_cast<float>(h)});
        merge_day(data, make_day("2026-06-02", partial));
    }
    CHECK_EQ(count_entries(data, "2026-06-02"), 0);

    // Tomorrow updated with full data
    merge_day(data, make_full_day("2026-06-02", 30.0F));
    CHECK_EQ(count_entries(data, "2026-06-02"), 24);

    // Yesterday still intact
    CHECK_EQ(count_entries(data, "2026-05-31"), 24);
}
