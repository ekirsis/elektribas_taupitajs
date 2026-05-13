#include "../doctest.h"
#include "secrets.h"

#include <cstdio>
#include <cstring>
#include <string>

std::string g_mock_prefs_api_url;
std::string g_mock_prefs_api_key;
bool g_mock_prefs_api_key_enabled = true;
int g_mock_http_status = 200;
std::string g_mock_response_body;
std::string g_captured_url;
bool g_key_header_was_set = false;

static std::string g_mock_today = "2026-07-02";
void get_current_date(char* buffer, int buffer_length)
{
    std::snprintf(buffer, buffer_length, "%s", g_mock_today.c_str());
}

#include "../../src/price_fetch.cpp"

namespace {

const char* SAMPLE_JSON = R"({
    "start": "2026-07-02T00:00:00Z",
    "prices": [
        {"start": "2026-07-02T00:00:00+03:00", "value": 42.12},
        {"start": "2026-07-02T01:00:00+03:00", "value": 38.50}
    ]
})";

void setup_mock(const char* prefs_url, int http_status, const char* response_body)
{
    g_mock_prefs_api_url = prefs_url ? prefs_url : "";
    g_mock_prefs_api_key = "";
    g_mock_prefs_api_key_enabled = true;
    g_mock_http_status = http_status;
    g_mock_response_body = response_body ? response_body : "";
    g_captured_url.clear();
    g_key_header_was_set = false;
    price_fetch_invalidate_url_cache();
}

} // namespace

TEST_CASE("price_fetch_by_date uses default URL when no custom URL saved")
{
    setup_mock(nullptr, 200, SAMPLE_JSON);

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");

    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://teapots.cloud/elektriba/api/prices/2026-07-02?vat=true&resolution=60");
}

TEST_CASE("price_fetch_by_date uses custom URL when saved in Preferences")
{
    setup_mock("https://custom.api/elektriba", 200, SAMPLE_JSON);

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");

    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://custom.api/elektriba/2026-07-02?vat=true&resolution=60");
}

TEST_CASE("price_fetch_by_date falls back to default when custom URL is empty string")
{
    setup_mock("", 200, SAMPLE_JSON);

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");

    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://teapots.cloud/elektriba/api/prices/2026-07-02?vat=true&resolution=60");
}

TEST_CASE("price_fetch_by_date returns false on HTTP error")
{
    setup_mock("https://broken.api/prices", 404, "Not found");

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");

    CHECK_FALSE(ok);
}

TEST_CASE("price_fetch_by_date uses today's date when date_to_request is null")
{
    setup_mock(nullptr, 200, SAMPLE_JSON);
    g_mock_today = "2026-12-25";

    PriceDay result;
    bool ok = price_fetch_by_date(result, nullptr);

    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://teapots.cloud/elektriba/api/prices/2026-12-25?vat=true&resolution=60");
}

TEST_CASE("price_fetch_by_date strips trailing slash from saved URL")
{
    setup_mock("https://custom.api/prices/", 200, SAMPLE_JSON);

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");

    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://custom.api/prices/2026-07-02?vat=true&resolution=60");
}

TEST_CASE("price_fetch_by_date trims whitespace from saved URL")
{
    setup_mock("  https://custom.api/prices  ", 200, SAMPLE_JSON);

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");

    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://custom.api/prices/2026-07-02?vat=true&resolution=60");
}

TEST_CASE("price_fetch_by_date cache invalidation picks up new URL")
{
    setup_mock("https://first.api/prices", 200, SAMPLE_JSON);

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");
    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://first.api/prices/2026-07-02?vat=true&resolution=60");

    setup_mock("https://second.api/prices", 200, SAMPLE_JSON);

    ok = price_fetch_by_date(result, "2026-07-02");
    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://second.api/prices/2026-07-02?vat=true&resolution=60");
}

TEST_CASE("price_fetch_by_date sends default PRICE_API_KEY when no saved key")
{
    setup_mock(nullptr, 200, SAMPLE_JSON);

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");

    CHECK(ok);
    CHECK(g_key_header_was_set);
}

TEST_CASE("price_fetch_override_settings overrides both URL and key")
{
    setup_mock("https://default.api/prices", 200, SAMPLE_JSON);

    price_fetch_override_settings("https://override.api/prices", "override-key");

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");

    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://override.api/prices/2026-07-02?vat=true&resolution=60");
}

TEST_CASE("price_fetch_override_settings with nullptr falls back to default URL")
{
    setup_mock(nullptr, 200, SAMPLE_JSON);

    price_fetch_override_settings(nullptr, nullptr);

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");

    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://teapots.cloud/elektriba/api/prices/2026-07-02?vat=true&resolution=60");
}

TEST_CASE("price_fetch_override_settings prepends https:// when no protocol given")
{
    setup_mock(nullptr, 200, SAMPLE_JSON);

    price_fetch_override_settings("custom.api/prices", nullptr);

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");

    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://custom.api/prices/2026-07-02?vat=true&resolution=60");
}

TEST_CASE("price_fetch_override_settings keeps http:// protocol as-is")
{
    setup_mock(nullptr, 200, SAMPLE_JSON);

    price_fetch_override_settings("http://custom.api/prices", nullptr);

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");

    CHECK(ok);
    CHECK_EQ(g_captured_url, "http://custom.api/prices/2026-07-02?vat=true&resolution=60");
}

TEST_CASE("price_fetch_override_settings strips trailing slash")
{
    setup_mock(nullptr, 200, SAMPLE_JSON);

    price_fetch_override_settings("https://custom.api/prices/", nullptr);

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");

    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://custom.api/prices/2026-07-02?vat=true&resolution=60");
}

TEST_CASE("cache invalidation restores Preferences values after override")
{
    setup_mock("https://stored.api/prices", 200, SAMPLE_JSON);

    // Override with different value
    price_fetch_override_settings("https://override.api/prices", nullptr);

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");
    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://override.api/prices/2026-07-02?vat=true&resolution=60");

    // Invalidate — next read re-reads from mock Preferences
    price_fetch_invalidate_url_cache();

    ok = price_fetch_by_date(result, "2026-07-02");
    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://stored.api/prices/2026-07-02?vat=true&resolution=60");
}

TEST_CASE("price_fetch_by_date sends saved key with default URL")
{
    setup_mock("https://custom.api/prices", 200, SAMPLE_JSON);

    price_fetch_override_settings(nullptr, "saved-key");

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");
    CHECK(ok);
    CHECK(g_key_header_was_set);
}

TEST_CASE("price_fetch_by_date sends default PRICE_API_KEY with default URL and empty override key")
{
    setup_mock(nullptr, 200, SAMPLE_JSON);

    price_fetch_override_settings(nullptr, nullptr);

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");
    CHECK(ok);
    CHECK(g_key_header_was_set);
}

TEST_CASE("price_fetch_by_date sends saved key with custom URL")
{
    setup_mock(nullptr, 200, SAMPLE_JSON);

    price_fetch_override_settings("https://custom.api/prices", "custom-key");

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");
    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://custom.api/prices/2026-07-02?vat=true&resolution=60");
    CHECK(g_key_header_was_set);
}

TEST_CASE("price_fetch_by_date sends no header with custom URL and empty key")
{
    setup_mock(nullptr, 200, SAMPLE_JSON);

    price_fetch_override_settings("https://no-key.api/prices", "");

    PriceDay result;
    bool ok = price_fetch_by_date(result, "2026-07-02");
    CHECK(ok);
    CHECK_EQ(g_captured_url, "https://no-key.api/prices/2026-07-02?vat=true&resolution=60");
    CHECK_FALSE(g_key_header_was_set);
}
