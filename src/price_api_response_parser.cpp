#include "price_api_response_parser.h"
#include <ArduinoJson.h>
#include <cmath>

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
        const char* start = document["start"].as<const char*>();
        snprintf(out.date_string, sizeof(out.date_string), "%s", start);
    }

    for (JsonObject item : document["prices"].as<JsonArray>())
    {
        const char* start_timestamp = item["start"].as<const char*>();
        float price_value = item["value"].as<float>();
        if (!start_timestamp || std::isnan(price_value))
        {
            continue;
        }

        uint8_t hour = static_cast<uint8_t>(atoi(start_timestamp + 11));
        out.prices.push_back({hour, price_value});
    }

    return !out.prices.empty();
}
