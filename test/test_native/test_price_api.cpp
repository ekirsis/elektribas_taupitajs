#include "../doctest.h"
#include "price_api_response_parser.h"
#include "time_utils.h"
#include "../../src/price_api_response_parser.cpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include "secrets.h"

namespace {

bool live_api_configured()
{
    return strcmp(PRICE_API_KEY, "YOUR_API_KEY_HERE") != 0;
}

std::string fetch_url(const char* url)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "curl -s --max-time 10 -H 'X-API-Key: %s' '%s' 2>/dev/null", PRICE_API_KEY, url);
    FILE* fp = popen(cmd, "r");
    if (!fp)
    {
        return {};
    }
    std::string result;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        result.append(buf, n);
    pclose(fp);
    return result;
}

bool is_valid_json(const std::string& body)
{
    JsonDocument doc;
    return deserializeJson(doc, body) == DeserializationError::Ok;
}

struct FetchResult
{
    std::string body;
    char today[11];
};

FetchResult fetch_today_prices()
{
    FetchResult r;
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    snprintf(r.today, sizeof(r.today), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    char url[256];
    snprintf(url, sizeof(url), "https://teapots.cloud/elektriba/api/prices/%s?vat=true&resolution=60", r.today);
    r.body = fetch_url(url);
    return r;
}

} // namespace

TEST_CASE("price API returns valid JSON for today")
{
    if (!live_api_configured())
    {
        MESSAGE("Skipping live API test: PRICE_API_KEY is not configured");
        return;
    }
    FetchResult fetch = fetch_today_prices();
    if (!is_valid_json(fetch.body))
    {
        FAIL_CHECK("API appears to be down (response was not JSON)");
        return;
    }

    JsonDocument doc;
    REQUIRE_EQ(deserializeJson(doc, fetch.body), DeserializationError::Ok);
    REQUIRE(doc.is<JsonObject>());

    REQUIRE(doc["start"].is<const char*>());
    const char* start = doc["start"].as<const char*>();
    CHECK_EQ(strlen(start), 10);
    CHECK_EQ(start[4], '-');
    CHECK_EQ(start[7], '-');

    REQUIRE(doc["prices"].is<JsonArray>());
    JsonArray prices = doc["prices"].as<JsonArray>();
    REQUIRE_GT(prices.size(), 0);

    JsonObject first = prices[0];
    REQUIRE(first["start"].is<const char*>());
    REQUIRE(first["value"].is<float>());

    const char* ts = first["start"].as<const char*>();
    CHECK_GT(strlen(ts), 10u);
    CHECK_EQ(ts[10], 'T');

    float val = first["value"].as<float>();
    CHECK_GE(val, -100.0f);
    CHECK_LE(val, 500.0f);

    // resolution=60 means one entry per hour of the local civil day, and a
    // DST change day is 23 or 25 hours long, never "about 24".
    unsigned const expected_hours = static_cast<unsigned>(time_utils::local_day_hours(fetch.today));
    REQUIRE_GT(expected_hours, 0u);
    CHECK_EQ(expected_hours, prices.size());

    // Timestamps carry explicit offsets, so stepping over the whole day means
    // exact one-hour gaps in real time, across the duplicated 03:00 too.
    int64_t previous_utc = 0;
    for (unsigned i = 0; i < prices.size(); ++i)
    {
        JsonObject entry = prices[i];
        REQUIRE(entry["start"].is<const char*>());
        int64_t utc = 0;
        REQUIRE(time_utils::parse_iso8601_utc(entry["start"].as<const char*>(), utc));
        if (i == 0)
        {
            previous_utc = utc;
            continue;
        }
        CHECK_EQ(static_cast<int64_t>(3600), utc - previous_utc);
        previous_utc = utc;
    }
}

TEST_CASE("price_response_parse produces valid PriceDay")
{
    if (!live_api_configured())
    {
        MESSAGE("Skipping live API test: PRICE_API_KEY is not configured");
        return;
    }
    FetchResult fetch = fetch_today_prices();
    if (!is_valid_json(fetch.body))
    {
        FAIL_CHECK("API appears to be down (response was not JSON)");
        return;
    }

    PriceDay result;
    bool ok = price_response_parse(fetch.body.c_str(), fetch.today, result);
    REQUIRE(ok);

    REQUIRE_FALSE(result.prices.empty());
    CHECK_EQ(strncmp(result.date_string, fetch.today, 10), 0);

    // One hourly slot per hour of the local civil day: 23, 24 or 25 entries,
    // decided by the date rather than a guess.
    size_t const expected_hours = static_cast<size_t>(time_utils::local_day_hours(fetch.today));
    REQUIRE_GT(expected_hours, 0u);
    CHECK_EQ(expected_hours, result.prices.size());

    for (const auto& p : result.prices)
    {
        CHECK_GT(p.start_utc, 0);
        CHECK_GE(p.eur_mwh, -100.0f);
        CHECK_LE(p.eur_mwh, 500.0f);
    }

    // Sorted by absolute UTC, and because entries keep their explicit offsets,
    // consecutive slots are exactly one real hour apart even on the fall-back
    // day; a strictly-increasing check would also accept a shifted day.
    for (size_t i = 1; i < result.prices.size(); ++i)
        CHECK_EQ(static_cast<int64_t>(3600), result.prices[i].start_utc - result.prices[i - 1].start_utc);
}

TEST_CASE("price_response_parse rejects empty input")
{
    PriceDay result;
    CHECK_FALSE(price_response_parse("", "2024-01-01", result));
    CHECK_FALSE(price_response_parse(nullptr, "2024-01-01", result));
    CHECK_FALSE(price_response_parse("{bad json}", "2024-01-01", result));
}

TEST_CASE("price_response_parse rejects response without prices array")
{
    PriceDay result;
    CHECK_FALSE(price_response_parse("{\"start\":\"2024-01-01T00:00:00Z\"}", "2024-01-01", result));
}

TEST_CASE("price_response_parse handles empty prices array")
{
    PriceDay result;
    CHECK_FALSE(
        price_response_parse("{\"start\":\"2024-01-01T00:00:00Z\",\"prices\":[]}", "2024-01-01", result));
}

TEST_CASE("price_response_parse sorts by UTC and keeps both 03:00 fall-back entries")
{
    const char* json = R"({
        "start": "2026-10-25T00:00:00Z",
        "prices": [
            {"start": "2026-10-25T03:00:00+02:00", "value": 20.0},
            {"start": "2026-10-25T03:00:00+03:00", "value": 10.0},
            {"start": "2026-10-25T04:00:00+02:00", "value": 30.0}
        ]
    })";

    PriceDay result;
    REQUIRE(price_response_parse(json, "2026-10-25", result));
    REQUIRE_EQ(result.prices.size(), 3u);

    CHECK_EQ(result.prices[0].start_utc, time_utils::utc_from_civil(2026, 10, 25, 0));
    CHECK_EQ(result.prices[0].eur_mwh, 10.0F);
    CHECK_EQ(result.prices[1].start_utc, time_utils::utc_from_civil(2026, 10, 25, 1));
    CHECK_EQ(result.prices[1].eur_mwh, 20.0F);
    CHECK_EQ(result.prices[2].start_utc, time_utils::utc_from_civil(2026, 10, 25, 2));
    CHECK_EQ(result.prices[2].eur_mwh, 30.0F);
}

TEST_CASE("price_response_parse rejects timestamps without an explicit offset")
{
    const char* json = R"({
        "start": "2026-10-25T00:00:00Z",
        "prices": [{"start": "2026-10-25T03:00:00", "value": 10.0}]
    })";

    PriceDay result;
    CHECK_FALSE(price_response_parse(json, "2026-10-25", result));
}

TEST_CASE("price_response_parse ignores entries for a different local date")
{
    const char* json = R"({
        "start": "2026-10-25T00:00:00Z",
        "prices": [{"start": "2026-10-26T00:00:00+03:00", "value": 10.0}]
    })";

    PriceDay result;
    CHECK_FALSE(price_response_parse(json, "2026-10-25", result));
}
