#include "price_api_response_parser.h"
#include "time_utils.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <cmath>
#include <cstring>

bool price_response_parse(const char* json_body, const char* expected_date, PriceDay& out)
{
    out.prices.clear();
    out.date_string[0] = '\0';

    if (json_body == nullptr || json_body[0] == '\0')
    {
        return false;
    }

    // NOLINTNEXTLINE(misc-const-correctness) — deserializeJson needs non-const ref
    JsonDocument document;
    DeserializationError parse_error = deserializeJson(document, json_body);
    if (parse_error)
    {
        return false;
    }

    if (!document["prices"].is<JsonArray>())
    {
        return false;
    }

    if (expected_date != nullptr && expected_date[0] != '\0')
    {
        snprintf(out.date_string, sizeof(out.date_string), "%s", expected_date);
    }
    else if (document["start"].is<const char*>())
    {
        char const* start = document["start"].as<const char*>();
        snprintf(out.date_string, sizeof(out.date_string), "%.10s", start);
    }

    if (out.date_string[0] == '\0')
    {
        return false;
    }

    for (JsonObject item : document["prices"].as<JsonArray>())
    {
        char const* start_timestamp = item["start"].as<const char*>();
        float const price_value = item["value"].as<float>();
        if (start_timestamp == nullptr || std::isnan(price_value))
        {
            continue;
        }

        // Each item is a local timestamp with an explicit offset.  The
        // first ten characters must match the requested civil date; the
        // absolute value distinguishes the two 03:00 entries in autumn.
        if (std::strncmp(start_timestamp, out.date_string, 10) != 0)
        {
            continue;
        }

        int64_t start_utc = 0;
        if (!time_utils::parse_iso8601_utc(start_timestamp, start_utc))
        {
            continue;
        }

        out.prices.push_back({start_utc, price_value});
    }

    std::sort(out.prices.begin(), out.prices.end(),
              [](HourPrice const& a, HourPrice const& b) { return a.start_utc < b.start_utc; });
    out.prices.erase(
        std::unique(out.prices.begin(), out.prices.end(),
                    [](HourPrice const& a, HourPrice const& b) { return a.start_utc == b.start_utc; }),
        out.prices.end());

    return !out.prices.empty();
}
