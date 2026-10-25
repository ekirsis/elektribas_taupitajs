#include "../doctest.h"
#include "time_utils.h"

#include <cstdlib>
#include <string>

namespace {

class ScopedLatvianTz
{
public:
    ScopedLatvianTz()
        : had_tz_(std::getenv("TZ") != nullptr),
          old_tz_(had_tz_ ? std::getenv("TZ") : "")
    {
        setenv("TZ", "EET-2EEST-3,M3.5.0/3:00:00,M10.5.0/4:00:00", 1);
        tzset();
    }

    ~ScopedLatvianTz()
    {
        if (had_tz_)
        {
            setenv("TZ", old_tz_.c_str(), 1);
        }
        else
        {
            unsetenv("TZ");
        }
        tzset();
    }

private:
    bool had_tz_;
    std::string old_tz_;
};

} // namespace

TEST_CASE("parse_iso8601_utc converts explicit offsets to UTC")
{
    int64_t value = 0;
    REQUIRE(time_utils::parse_iso8601_utc("2026-10-25T03:00:00+03:00", value));
    CHECK_EQ(value, time_utils::utc_from_civil(2026, 10, 25, 0));

    REQUIRE(time_utils::parse_iso8601_utc("2026-10-25T03:00:00+02:00", value));
    CHECK_EQ(value, time_utils::utc_from_civil(2026, 10, 25, 1));

    REQUIRE(time_utils::parse_iso8601_utc("2026-10-25T00:00:00Z", value));
    CHECK_EQ(value, time_utils::utc_from_civil(2026, 10, 25, 0));

    REQUIRE(time_utils::parse_iso8601_utc("2026-10-25T03:00:00.123Z", value));
    CHECK_EQ(value, time_utils::utc_from_civil(2026, 10, 25, 3));
}

TEST_CASE("parse_iso8601_utc rejects ambiguous or invalid timestamps")
{
    int64_t value = 0;
    CHECK_FALSE(time_utils::parse_iso8601_utc("2026-10-25T03:00:00", value));
    CHECK_FALSE(time_utils::parse_iso8601_utc("2026-02-30T03:00:00+03:00", value));
    CHECK_FALSE(time_utils::parse_iso8601_utc("2026-10-25T03:00:0", value));
    CHECK_FALSE(time_utils::parse_iso8601_utc("2026-10-25T3:00:00Z", value));
    CHECK_FALSE(time_utils::parse_iso8601_utc("not-a-timestamp", value));
    CHECK_FALSE(time_utils::parse_iso8601_utc(nullptr, value));
}

TEST_CASE("local_day_hours reports 23/24/25-hour days")
{
    ScopedLatvianTz const tz;
    CHECK_EQ(time_utils::local_day_hours("2026-06-01"), 24);
    CHECK_EQ(time_utils::local_day_hours("2026-10-25"), 25);
    CHECK_EQ(time_utils::local_day_hours("2026-03-29"), 23);
    CHECK_EQ(time_utils::local_day_hours("invalid"), 0);
    CHECK_EQ(time_utils::local_day_hours("2026-02-30"), 0);
    CHECK_EQ(time_utils::local_day_hours("2026-13-01"), 0);
}

TEST_CASE("format_hour_range labels the slot boundaries on DST days")
{
    ScopedLatvianTz const tz;
    char label[16] = {};

    // Ordinary slot: 2026-06-01 02:00 UTC is 05:00 EEST.
    time_utils::format_hour_range(time_utils::utc_from_civil(2026, 6, 1, 2),
                                  time_utils::utc_from_civil(2026, 6, 1, 3), label, sizeof(label));
    CHECK_EQ(std::string(label), "05:00-06:00");

    // Ordinary slot crossing local midnight: 20:00 UTC is 23:00 EEST.
    time_utils::format_hour_range(time_utils::utc_from_civil(2026, 6, 1, 20),
                                  time_utils::utc_from_civil(2026, 6, 1, 21), label, sizeof(label));
    CHECK_EQ(std::string(label), "23:00-00:00");

    // Autumn fall-back: 2026-10-25 03:00 EEST is 2026-10-25 00:00 UTC, and one
    // hour later the clock jumped back, so the slot ends at 03:00 again.
    time_utils::format_hour_range(time_utils::utc_from_civil(2026, 10, 25, 0),
                                  time_utils::utc_from_civil(2026, 10, 25, 1), label, sizeof(label));
    CHECK_EQ(std::string(label), "03:00");

    // The second 03:00 slot (2026-10-25 01:00 UTC = 03:00 EET) ends at 04:00.
    time_utils::format_hour_range(time_utils::utc_from_civil(2026, 10, 25, 1),
                                  time_utils::utc_from_civil(2026, 10, 25, 2), label, sizeof(label));
    CHECK_EQ(std::string(label), "03:00-04:00");

    // Spring forward: 02:00 EET is 2026-03-29 00:00 UTC, and the slot ends at
    // 04:00 EEST because 03:00 never exists that day.
    time_utils::format_hour_range(time_utils::utc_from_civil(2026, 3, 29, 0),
                                  time_utils::utc_from_civil(2026, 3, 29, 1), label, sizeof(label));
    CHECK_EQ(std::string(label), "02:00-04:00");
}

TEST_CASE("format_local_date_at handles the autumn fall-back midnight")
{
    ScopedLatvianTz const tz;
    // 2026-10-25 00:30 EEST = 2026-10-24 21:30 UTC.
    time_t const now = static_cast<time_t>(time_utils::utc_from_civil(2026, 10, 24, 21, 30));
    char date[11] = {};

    time_utils::format_local_date_at(now, 0, date, sizeof(date));
    CHECK_EQ(std::string(date), "2026-10-25");

    time_utils::format_local_date_at(now, 1, date, sizeof(date));
    CHECK_EQ(std::string(date), "2026-10-26");

    time_utils::format_local_date_at(now, -1, date, sizeof(date));
    CHECK_EQ(std::string(date), "2026-10-24");
}

TEST_CASE("format_local_date_at handles the day before spring-forward")
{
    ScopedLatvianTz const tz;
    // 2026-03-28 23:30 EET = 2026-03-28 21:30 UTC.  Without resetting
    // tm_isdst, mktime() reuses the standard-time flag and returns
    // 2026-03-30 00:30 instead of 2026-03-29 23:30.
    time_t const now = static_cast<time_t>(time_utils::utc_from_civil(2026, 3, 28, 21, 30));
    char date[11] = {};

    time_utils::format_local_date_at(now, 0, date, sizeof(date));
    CHECK_EQ(std::string(date), "2026-03-28");

    time_utils::format_local_date_at(now, 1, date, sizeof(date));
    CHECK_EQ(std::string(date), "2026-03-29");

    time_utils::format_local_date_at(now, -1, date, sizeof(date));
    CHECK_EQ(std::string(date), "2026-03-27");
}
