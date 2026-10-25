// time_sync.cpp — NTP time synchronisation
//
// The ESP32 doesn't have a battery-backed RTC, so we fetch the current time
// from an NTP server after connecting to WiFi. This also serves as our
// "internet connectivity check" — if NTP works, we know we have full network
// access (and we get the correct time in one step).
//
// Uses configTime(UTC) + separate setenv("TZ") + tzset() instead of
// configTzTime() — the two-step approach avoids a known ESP32-S3 (Nano) stall
// when SNTP is initialised from Core 0 (FreeRTOS monitor task).
//
// Latvian timezone is configured so all time functions return local time
// (EET / EEST with automatic DST switching).

#include <Arduino.h>
#include <esp_sntp.h>
#include "time_sync.h"
#include "time_utils.h"

namespace {
time_t cached_synced_time = 0; // 0 = not synced yet
} // namespace

char const* LATVIAN_TZ = "EET-2EEST-3,M3.5.0/3:00:00,M10.5.0/4:00:00";

void time_sync_init()
{
    sntp_stop();
    configTime(0, 0, "time.cloudflare.com", "pool.ntp.org", "time.google.com");
}

bool time_sync_poll()
{
    time_t const now = time(nullptr);
    if (now >= MIN_VALID_UNIX_TIMESTAMP)
    {
        setenv("TZ", LATVIAN_TZ, 1);
        tzset();
        cached_synced_time = now;
        return true;
    }
    return false;
}

bool time_sync()
{
    time_sync_init();

    delay(500);
    yield();
    if (time_sync_poll())
    {
        return true;
    }

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        delay(1000);
        yield();
        if (time_sync_poll())
        {
            return true;
        }
    }

    cached_synced_time = 0;
    return false;
}

bool time_synced()
{
    return cached_synced_time >= MIN_VALID_UNIX_TIMESTAMP;
}

void format_date_string(char* out_buffer, int buffer_length, int day_offset)
{
    time_utils::format_local_date_at(time(nullptr), day_offset, out_buffer, static_cast<size_t>(buffer_length));
}

void get_current_date(char* out_buffer, int buffer_length)
{
    format_date_string(out_buffer, buffer_length, 0);
}
