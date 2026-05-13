#include "../doctest.h"
#include "price_api_response_parser.h"
#include "../../src/price_api_response_parser.cpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include "secrets.h"

namespace {

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
    CHECK_EQ(ts[10], 'T');

    float val = first["value"].as<float>();
    CHECK_GE(val, -100.0f);
    CHECK_LE(val, 500.0f);

    CHECK_GE(prices.size(), 23);
}

TEST_CASE("price_response_parse produces valid PriceDay")
{
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

    CHECK_GE(result.prices.size(), 23);

    for (const auto& p : result.prices)
    {
        CHECK_LT(p.hour, 24);
        CHECK_GE(p.eur_mwh, -100.0f);
        CHECK_LE(p.eur_mwh, 500.0f);
    }

    for (size_t i = 1; i < result.prices.size(); ++i)
        CHECK_GT(result.prices[i].hour, result.prices[i - 1].hour);
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
