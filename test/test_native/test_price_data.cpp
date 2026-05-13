#include "../doctest.h"
#include "price_api.h"

#include <cstring>

TEST_CASE("find_price returns found=false for empty data")
{
    std::vector<PriceEntry> data;
    bool found = true;
    find_price(data, "2026-06-01", 0, found);
    CHECK_FALSE(found);
}

TEST_CASE("find_price finds entry by exact date and hour")
{
    std::vector<PriceEntry> data;
    PriceEntry e;
    std::strncpy(e.date_string, "2026-06-01", sizeof(e.date_string));
    e.hour = 12;
    e.eur_mwh = 50.0F;
    data.push_back(e);

    bool found = false;
    float result = find_price(data, "2026-06-01", 12, found);
    CHECK(found);
    CHECK_EQ(result, 5.0F); // 50 * 0.1
}

TEST_CASE("find_price returns found=false when date does not match")
{
    std::vector<PriceEntry> data;
    PriceEntry e;
    std::strncpy(e.date_string, "2026-06-01", sizeof(e.date_string));
    e.hour = 12;
    e.eur_mwh = 50.0F;
    data.push_back(e);

    bool found = true;
    find_price(data, "2026-06-02", 12, found);
    CHECK_FALSE(found);
}

TEST_CASE("find_price returns found=false when hour does not match")
{
    std::vector<PriceEntry> data;
    PriceEntry e;
    std::strncpy(e.date_string, "2026-06-01", sizeof(e.date_string));
    e.hour = 12;
    e.eur_mwh = 50.0F;
    data.push_back(e);

    bool found = true;
    find_price(data, "2026-06-01", 13, found);
    CHECK_FALSE(found);
}

TEST_CASE("find_price distinguishes same hour on different dates")
{
    std::vector<PriceEntry> data;

    PriceEntry e1;
    std::strncpy(e1.date_string, "2026-06-01", sizeof(e1.date_string));
    e1.hour = 0;
    e1.eur_mwh = 10.0F;
    data.push_back(e1);

    PriceEntry e2;
    std::strncpy(e2.date_string, "2026-06-02", sizeof(e2.date_string));
    e2.hour = 0;
    e2.eur_mwh = 20.0F;
    data.push_back(e2);

    bool found = false;
    CHECK_EQ(find_price(data, "2026-06-01", 0, found), 1.0F);
    CHECK(found);
    found = false;
    CHECK_EQ(find_price(data, "2026-06-02", 0, found), 2.0F);
    CHECK(found);
}

TEST_CASE("find_price returns first match in multi-entry vector")
{
    std::vector<PriceEntry> data;
    for (int h = 0; h < 24; ++h)
    {
        PriceEntry e;
        std::strncpy(e.date_string, "2026-06-01", sizeof(e.date_string));
        e.hour = static_cast<uint8_t>(h);
        e.eur_mwh = static_cast<float>(h * 10);
        data.push_back(e);
    }

    bool found = false;
    CHECK_EQ(find_price(data, "2026-06-01", 7, found), 7.0F);
    CHECK(found);
    found = false;
    CHECK_EQ(find_price(data, "2026-06-01", 23, found), 23.0F);
    CHECK(found);
    found = true;
    find_price(data, "2026-06-01", 24, found);
    CHECK_FALSE(found);
}

TEST_CASE("find_price conversion factor is EUR/MWh * 0.1")
{
    std::vector<PriceEntry> data;
    PriceEntry e;
    std::strncpy(e.date_string, "2026-06-15", sizeof(e.date_string));
    e.hour = 8;
    e.eur_mwh = 123.45F;
    data.push_back(e);

    bool found = false;
    float result = find_price(data, "2026-06-15", 8, found);
    CHECK(found);
    CHECK_EQ(result, doctest::Approx(12.345F));
}

TEST_CASE("find_price with found flag returns found=true for existing entry")
{
    std::vector<PriceEntry> data;
    PriceEntry e;
    std::strncpy(e.date_string, "2026-06-01", sizeof(e.date_string));
    e.hour = 8;
    e.eur_mwh = 100.0F;
    data.push_back(e);

    bool found = false;
    float v = find_price(data, "2026-06-01", 8, found);
    CHECK(found);
    CHECK_EQ(v, 10.0F);
}

TEST_CASE("find_price with found flag works for negative prices")
{
    std::vector<PriceEntry> data;
    PriceEntry e;
    std::strncpy(e.date_string, "2026-06-01", sizeof(e.date_string));
    e.hour = 12;
    e.eur_mwh = -0.04F;
    data.push_back(e);

    bool found = false;
    float v = find_price(data, "2026-06-01", 12, found);
    CHECK(found);
    CHECK_EQ(v, -0.004F);
}
