#include "../doctest.h"
#include "price_data_utils.h"
#include "time_utils.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int64_t utc_for(const char* date, int hour)
{
    int year = 0;
    int month = 0;
    int day = 0;
    std::sscanf(date, "%4d-%2d-%2d", &year, &month, &day);
    return time_utils::utc_from_civil(year, month, day, hour);
}

class ScopedLatvianTz
{
public:
    ScopedLatvianTz()
        : had_tz_(std::getenv("TZ") != nullptr),
          old_tz_(had_tz_ ? std::getenv("TZ") : "")
    {
        setenv("TZ", "EET-2EEST-3,M3.5.0/3:00:00,M10.5.0/4:00:00", 1);
        tzset();
    }

    ~ScopedLatvianTz()
    {
        if (had_tz_)
        {
            setenv("TZ", old_tz_.c_str(), 1);
        }
        else
        {
            unsetenv("TZ");
        }
        tzset();
    }

private:
    bool had_tz_;
    std::string old_tz_;
};

PriceEntry make_entry(const char* date, uint8_t hour, float eur_mwh)
{
    PriceEntry e{};
    std::strncpy(e.date_string, date, sizeof(e.date_string) - 1);
    e.date_string[sizeof(e.date_string) - 1] = '\0';
    e.start_utc = utc_for(date, hour);
    e.eur_mwh = eur_mwh;
    return e;
}

PriceDay make_day(const char* date, const std::vector<HourPrice>& prices)
{
    PriceDay d{};
    std::strncpy(d.date_string, date, sizeof(d.date_string) - 1);
    d.date_string[sizeof(d.date_string) - 1] = '\0';
    d.prices = prices;
    return d;
}

PriceDay make_full_day(const char* date, float base_price)
{
    std::vector<HourPrice> prices;
    prices.reserve(24);
    for (int h = 0; h < 24; ++h)
    {
        prices.push_back({utc_for(date, h), base_price + static_cast<float>(h)});
    }
    return make_day(date, prices);
}

PriceDay make_fall_back_day()
{
    PriceDay d{};
    std::strncpy(d.date_string, "2026-10-25", sizeof(d.date_string) - 1);
    int64_t const start = time_utils::utc_from_civil(2026, 10, 24, 21);
    for (int i = 0; i < 25; ++i)
    {
        d.prices.push_back({start + static_cast<int64_t>(i) * 3600, 10.0F + static_cast<float>(i)});
    }
    return d;
}

PriceDay make_spring_forward_day()
{
    PriceDay d{};
    std::strncpy(d.date_string, "2026-03-29", sizeof(d.date_string) - 1);
    int64_t const start = time_utils::utc_from_civil(2026, 3, 28, 22);
    for (int i = 0; i < 23; ++i)
    {
        d.prices.push_back({start + static_cast<int64_t>(i) * 3600, 10.0F + static_cast<float>(i)});
    }
    return d;
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
    float v = find_price_at(data, utc_for("2026-06-01", 0), found);
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
            partial.push_back({utc_for("2026-06-02", h), 30.0F + static_cast<float>(h)});
        merge_day(data, make_day("2026-06-02", partial));
    }
    CHECK_EQ(count_entries(data, "2026-06-02"), 0);

    // Tomorrow updated with full data
    merge_day(data, make_full_day("2026-06-02", 30.0F));
    CHECK_EQ(count_entries(data, "2026-06-02"), 24);

    // Yesterday still intact
    CHECK_EQ(count_entries(data, "2026-05-31"), 24);
}

// ── DST days ─────────────────────────────────────────────────────────────

TEST_CASE("expected_hours_for_date reports 23/24/25 hour local days")
{
    ScopedLatvianTz const tz;
    CHECK_EQ(expected_hours_for_date("2026-06-01"), 24);
    CHECK_EQ(expected_hours_for_date("2026-10-25"), 25);
    CHECK_EQ(expected_hours_for_date("2026-03-29"), 23);
    CHECK_EQ(expected_hours_for_date("not-a-date"), 0);
}

TEST_CASE("merge_day accepts a 25-hour fall-back day and keeps both 03:00 slots")
{
    ScopedLatvianTz const tz;
    std::vector<PriceEntry> data;
    merge_day(data, make_fall_back_day());

    CHECK_EQ(count_entries(data, "2026-10-25"), 25);
    CHECK_EQ(data.size(), 25u);

    bool found = false;
    float const first_03 = find_price_at(data, time_utils::utc_from_civil(2026, 10, 25, 0, 30), found);
    CHECK(found);
    CHECK_EQ(first_03, doctest::Approx(1.3F)); // 13 EUR/MWh, first 03:00 (EEST)

    float const second_03 = find_price_at(data, time_utils::utc_from_civil(2026, 10, 25, 1, 30), found);
    CHECK(found);
    CHECK_EQ(second_03, doctest::Approx(1.4F)); // 14 EUR/MWh, second 03:00 (EET)
}

TEST_CASE("merge_day accepts a 23-hour spring-forward day")
{
    ScopedLatvianTz const tz;
    std::vector<PriceEntry> data;
    merge_day(data, make_spring_forward_day());

    CHECK_EQ(count_entries(data, "2026-03-29"), 23);
    CHECK_EQ(data.size(), 23u);

    bool found = false;
    CHECK_EQ(find_price_at(data, time_utils::utc_from_civil(2026, 3, 28, 23, 30), found),
             doctest::Approx(1.1F)); // 01:30 EET
    CHECK(found);

    CHECK_EQ(find_price_at(data, time_utils::utc_from_civil(2026, 3, 29, 0, 30), found),
             doctest::Approx(1.2F)); // 02:30 EET; local 03:00 does not exist
    CHECK(found);

    CHECK_EQ(find_price_at(data, time_utils::utc_from_civil(2026, 3, 29, 1, 30), found),
             doctest::Approx(1.3F)); // 04:30 EEST
    CHECK(found);
}

TEST_CASE("merge_day rejects a 24-entry response on a 25-hour day")
{
    ScopedLatvianTz const tz;
    std::vector<PriceEntry> data;
    merge_day(data, make_full_day("2026-10-25", 10.0F));
    CHECK_EQ(count_entries(data, "2026-10-25"), 0);
}

TEST_CASE("merge_day rejects a 24-entry response on a 23-hour day")
{
    ScopedLatvianTz const tz;
    std::vector<PriceEntry> data;
    merge_day(data, make_full_day("2026-03-29", 10.0F));
    CHECK_EQ(count_entries(data, "2026-03-29"), 0);
}
