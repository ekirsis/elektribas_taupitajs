#pragma once

#include "price_api.h"

/// Parse a raw JSON response from the price API and populate `out`.
/// expected_date — the date this response should cover ("YYYY-MM-DD")
/// Returns true and populates `out` on success.
bool price_response_parse(const char* json_body, const char* expected_date, PriceDay& out);
