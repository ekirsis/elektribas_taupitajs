// price_fetch.cpp — Fetch hourly electricity prices
//                  from the teapots.cloud price API
//
// The API returns a JSON response like:
//   {"start":"2024-01-01T00:00:00Z","prices":[
//     {"start":"2024-01-01T00:00:00Z","value":42.12},
//     {"start":"2024-01-01T01:00:00Z","value":38.50},
//     ...
//   ]}
//
// Prices are in EUR/MWh with VAT already included. The LCD displays them
// in c/kWh (= ÷ 10).

#include "price_fetch.h"
#include "price_api_response_parser.h"
#include "secrets.h"
#include "time_sync.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>

namespace {

constexpr const char* PRICE_API_DEFAULT_URL = "https://teapots.cloud/elektriba/api/prices";

String cached_url;
String cached_key;
bool has_custom_url = false;
bool cache_is_dirty = true;

void reload_cache()
{
    cache_is_dirty = false;
    Preferences prefs;
    prefs.begin("price_cfg", true);
    String saved_url = prefs.getString("api_url", "");
    String saved_key = prefs.getString("api_key", "");
    prefs.end();

    saved_url.trim();
    if (saved_url.length() > 0 && saved_url.c_str()[saved_url.length() - 1] == '/')
    {
        saved_url.remove(saved_url.length() - 1);
    }
    has_custom_url = saved_url.length() > 0;
    cached_url = has_custom_url ? saved_url : String{PRICE_API_DEFAULT_URL};

    saved_key.trim();
    cached_key = saved_key;
}

String get_base_url()
{
    if (cache_is_dirty)
    {
        reload_cache();
    }
    return cached_url;
}

String get_api_key()
{
    if (cache_is_dirty)
    {
        reload_cache();
    }
    if (has_custom_url || cached_key.length() > 0)
    {
        return cached_key;
    }
    return {PRICE_API_KEY};
}

} // namespace

void price_fetch_invalidate_url_cache()
{
    cache_is_dirty = true;
}

void price_fetch_override_settings(const char* api_url, const char* api_key)
{
    has_custom_url = false;
    if (api_url && strlen(api_url) > 0)
    {
        String cleaned_url = String(api_url);
        cleaned_url.trim();
        if (cleaned_url.length() > 0 && cleaned_url.c_str()[cleaned_url.length() - 1] == '/')
        {
            cleaned_url.remove(cleaned_url.length() - 1);
        }
        if (cleaned_url.length() > 0 && strstr(cleaned_url.c_str(), "://") == nullptr)
        {
            cleaned_url = String("https://") + cleaned_url;
        }
        has_custom_url = cleaned_url.length() > 0;
        cached_url = has_custom_url ? cleaned_url : String{PRICE_API_DEFAULT_URL};
    }
    else
    {
        cached_url = PRICE_API_DEFAULT_URL;
    }
    cached_key = String{api_key != nullptr ? api_key : ""};
    cache_is_dirty = false;
}

bool price_fetch_by_date(PriceDay& out_target, const char* date_to_request)
{
    out_target.prices.clear();
    out_target.date_string[0] = '\0';

    String url = get_base_url();

    if (date_to_request && strlen(date_to_request) > 0)
    {
        url += '/';
        url += date_to_request;
    }
    else
    {
        char today[11];
        get_current_date(today, sizeof(today));
        url += '/';
        url += today;
    }
    url += "?vat=true&resolution=60";

    HTTPClient http_client;
    http_client.begin(url);
    http_client.setTimeout(15000);
    const String key = get_api_key();
    if (key.length() > 0)
    {
        http_client.addHeader("X-API-Key", key.c_str());
    }

    const int http_status = http_client.GET();
    if (http_status != 200)
    {
        http_client.end();
        return false;
    }

    String raw = http_client.getString();
    http_client.end();

    return price_response_parse(raw.c_str(), date_to_request, out_target);
}
