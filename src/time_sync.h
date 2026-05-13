// time_sync.h — NTP time synchronisation
//
// Provides time_sync() for fetching time from NTP servers, time_synced() for
// checking validity, and get_current_date() for formatting the date string.
// The Latvian timezone (EET/EEST) is configured inside time_sync.cpp.

#pragma once

#include <time.h>

// NTP timestamps before this are rejected (the ESP was released in 2023,
// so any valid time should be >= this). Used to distinguish "time not yet
// synced" from "time is January 1st 1970" (the unix epoch default).
static constexpr time_t MIN_VALID_UNIX_TIMESTAMP = 1700000000; // ≈ Jan 2023

// Latvian timezone:  EET-2EEST-3 = standard UTC+2, DST UTC+3
extern const char* LATVIAN_TZ;

/// Start NTP sync (calls configTime).
void time_sync_init();

/// Poll NTP once — returns true when time is valid and sets TZ.
bool time_sync_poll();

/// Fetch the current time from NTP servers (blocks up to ~4.5 s).
/// Configures the Latvian timezone so that localtime_r() returns local time.
/// Returns true once the clock is past a reasonable minimum timestamp.
bool time_sync();

bool time_synced();

void get_current_date(char* buffer, int buffer_length);

void format_date_string(char* buffer, int buffer_length, int day_offset);
