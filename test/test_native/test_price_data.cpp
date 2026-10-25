#include "../doctest.h"
#include "price_api.h"
#include "time_utils.h"

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

} // namespace

TEST_CASE("find_price_at returns found=false for empty data")
{
    std::vector<PriceEntry> data;
    bool found = true;
    find_price_at(data, time_utils::utc_from_civil(2026, 6, 1, 0, 30), found);
    CHECK_FALSE(found);
}

TEST_CASE("find_price_at finds entry covering the timestamp")
{
    std::vector<PriceEntry> data;
    data.push_back(make_entry("2026-06-01", 12, 50.0F));

    bool found = false;
    float result = find_price_at(data, time_utils::utc_from_civil(2026, 6, 1, 12, 30), found);
    CHECK(found);
    CHECK_EQ(result, 5.0F); // 50 * 0.1
}

TEST_CASE("find_price_at returns found=false before the entry starts")
{
    std::vector<PriceEntry> data;
    data.push_back(make_entry("2026-06-01", 12, 50.0F));

    bool found = true;
    find_price_at(data, time_utils::utc_from_civil(2026, 6, 1, 11, 59), found);
    CHECK_FALSE(found);
}

TEST_CASE("find_price_at returns found=false after the entry ends")
{
    std::vector<PriceEntry> data;
    data.push_back(make_entry("2026-06-01", 12, 50.0F));

    bool found = true;
    find_price_at(data, time_utils::utc_from_civil(2026, 6, 1, 13, 0), found);
    CHECK_FALSE(found);
}

TEST_CASE("find_price_at distinguishes same hour on different dates")
{
    std::vector<PriceEntry> data;
    data.push_back(make_entry("2026-06-01", 0, 10.0F));
    data.push_back(make_entry("2026-06-02", 0, 20.0F));

    bool found = false;
    CHECK_EQ(find_price_at(data, time_utils::utc_from_civil(2026, 6, 1, 0, 30), found), 1.0F);
    CHECK(found);
    CHECK_EQ(find_price_at(data, time_utils::utc_from_civil(2026, 6, 2, 0, 30), found), 2.0F);
    CHECK(found);
}

TEST_CASE("find_price_at returns the entry covering a multi-entry vector")
{
    std::vector<PriceEntry> data;
    for (int h = 0; h < 24; ++h)
    {
        data.push_back(make_entry("2026-06-01", h, static_cast<float>(h * 10)));
    }

    bool found = false;
    CHECK_EQ(find_price_at(data, time_utils::utc_from_civil(2026, 6, 1, 7, 30), found), 7.0F);
    CHECK(found);
    CHECK_EQ(find_price_at(data, time_utils::utc_from_civil(2026, 6, 1, 23, 59), found), 23.0F);
    CHECK(found);
    found = true;
    find_price_at(data, time_utils::utc_from_civil(2026, 6, 2, 0, 0), found);
    CHECK_FALSE(found);
}

TEST_CASE("find_price_at conversion factor is EUR/MWh * 0.1")
{
    std::vector<PriceEntry> data;
    data.push_back(make_entry("2026-06-15", 8, 123.45F));

    bool found = false;
    float result = find_price_at(data, time_utils::utc_from_civil(2026, 6, 15, 8, 1), found);
    CHECK(found);
    CHECK_EQ(result, doctest::Approx(12.345F));
}

TEST_CASE("find_price_at works for negative prices")
{
    std::vector<PriceEntry> data;
    data.push_back(make_entry("2026-06-01", 12, -0.04F));

    bool found = false;
    float v = find_price_at(data, time_utils::utc_from_civil(2026, 6, 1, 12, 30), found);
    CHECK(found);
    CHECK_EQ(v, -0.004F);
}

TEST_CASE("find_price_at distinguishes the duplicated 03:00 hour on fall-back")
{
    std::vector<PriceEntry> data;
    PriceEntry first{};
    std::strncpy(first.date_string, "2026-10-25", sizeof(first.date_string) - 1);
    first.start_utc = time_utils::utc_from_civil(2026, 10, 25, 0); // 03:00 EEST
    first.eur_mwh = 10.0F;
    data.push_back(first);

    PriceEntry second{};
    std::strncpy(second.date_string, "2026-10-25", sizeof(second.date_string) - 1);
    second.start_utc = time_utils::utc_from_civil(2026, 10, 25, 1); // 03:00 EET
    second.eur_mwh = 20.0F;
    data.push_back(second);

    bool found = false;
    CHECK_EQ(find_price_at(data, time_utils::utc_from_civil(2026, 10, 25, 0, 30), found), 1.0F);
    CHECK(found);
    CHECK_EQ(find_price_at(data, time_utils::utc_from_civil(2026, 10, 25, 1, 30), found), 2.0F);
    CHECK(found);
}
