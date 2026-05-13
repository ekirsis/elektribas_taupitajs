// price_fetch.h — Fetch hourly electricity prices from the price API
//
// This API returns Latvian day-ahead prices in EUR/MWh with VAT already
// included via the ?vat=true query parameter. The output is fully compatible
// with the HourPrice / PriceDay data model.

#pragma once

#include "price_api.h"

/// Fetch hourly prices from the price API for the given date.
/// If `date_to_request` is nullptr or empty, fetches today's prices.
/// Returns true and populates `out_target` on success.
bool price_fetch_by_date(PriceDay& out_target, const char* date_to_request);

/// Force the internal cache to re-read from Preferences on the next
/// call to price_fetch_by_date. Call this after saving/clearing settings
/// in the captive portal or when leaving AP mode.
void price_fetch_invalidate_url_cache();

/// Temporarily override the cached URL and API key without writing to NVS.
/// Used by the captive portal to test new settings before persisting.
/// Sanitises the URL: trims whitespace, strips trailing slash, prepends
/// https:// if no protocol given. An empty or nullptr api_key means
/// no key will be sent at fetch time.
void price_fetch_override_settings(const char* api_url, const char* api_key);
