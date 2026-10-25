#include <Arduino.h>
#include <U8g2lib.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include "wifi.h"
#include "wifi_setup.h"
#include "price_fetch.h"
#include "time_sync.h"
#include "lcd_display.h"
#include "pins.h"
#include "price_data_utils.h"

U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI lcd(U8G2_R0, 13, 11, 10, 12, 14);

namespace {

std::vector<PriceEntry> price_data;
int price_limit = 0;
bool data_ok = false;
char last_prune_date[11] = "";

constexpr unsigned long DEBOUNCE_MS = 30;
constexpr unsigned long LONGPRESS_MS = 500;
constexpr unsigned long REPEAT_MS = 100;

struct Button
{
    int pin;
    bool last_raw;
    unsigned long last_change_ms;
    bool pressed;
    bool longpress_active;
    unsigned long press_start_ms;
    unsigned long last_repeat_ms;
};

Button btn_inc = {PIN_BUTTON_INCREMENT, HIGH, 0, false, false, 0, 0};
Button btn_dec = {PIN_BUTTON_DECREMENT, HIGH, 0, false, false, 0, 0};

bool wifi_ok = false;
unsigned long last_wifi_retry = 0;
unsigned long last_price_retry = 0;
unsigned long last_time_retry = 0;

constexpr unsigned long TOMORROW_RETRY_MS = 30UL * 60UL * 1000UL;
bool tomorrow_attempt_recorded = false;
unsigned long last_tomorrow_attempt_ms = 0;

bool handle_limit_buttons()
{
    unsigned long now = millis();
    bool changed = false;

    auto update = [&](Button& b, int dir) {
        const bool raw = digitalRead(b.pin);
        if (raw != b.last_raw)
        {
            b.last_raw = raw;
            b.last_change_ms = now;
            return;
        }
        if (now - b.last_change_ms < DEBOUNCE_MS)
        {
            return;
        }

        const bool active = !raw;

        if (active && !b.pressed)
        {
            b.pressed = true;
            b.longpress_active = false;
            b.press_start_ms = now;
            b.last_repeat_ms = now;
            price_limit += dir;
            changed = true;
        }
        else if (!active && b.pressed)
        {
            b.pressed = false;
            b.longpress_active = false;
        }
        else if (active && b.pressed)
        {
            if (!b.longpress_active && now - b.press_start_ms >= LONGPRESS_MS)
            {
                b.longpress_active = true;
                b.last_repeat_ms = now;
                price_limit += dir;
                changed = true;
            }
            else if (b.longpress_active && now - b.last_repeat_ms >= REPEAT_MS)
            {
                b.last_repeat_ms = now;
                price_limit += dir;
                changed = true;
            }
        }
    };

    update(btn_inc, 1);
    update(btn_dec, -1);

    if (price_limit < 0)
    {
        price_limit = 0;
    }
    if (price_limit > 99)
    {
        price_limit = 99;
    }

    return changed;
}

void poll_limit_buttons_for_ms(int ms)
{
    int const ticks = ms / 10;
    for (int i = 0; i < ticks; ++i)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        esp_task_wdt_reset();
        if (data_ok && handle_limit_buttons())
        {
            lcd_draw_prices(lcd, price_data, price_limit);
        }
    }
}

const char MSG_REFRESHING[] = "Lejuplādē cenas...";
const char MSG_NO_DATA[] = "Cenas nav pieejamas!";
const char MSG_NO_DATA_L1[] = "Problēma ar api";
const char MSG_NO_DATA_L2[] = "vai internetu!";

// ── Network work wrapper ─────────────────────────────────────────────────

struct FetchDateArg
{
    PriceDay day;
    char date_str[11];
};

bool fetch_date_work(void* arg)
{
    auto* fetch_arg = static_cast<FetchDateArg*>(arg);
    return price_fetch_by_date(fetch_arg->day, fetch_arg->date_str);
}

// ── Forward declarations ─────────────────────────────────────────────────

void on_fetch_failed();
void wifi_failed_fallback();
bool ensure_prices(int hour);
bool try_render_cached_prices();

// ── Rendering helper ─────────────────────────────────────────────────────

// Tries to render prices from the existing cache. Returns true on success
// (data_ok set, screen drawn). Refuses to render before the clock is synced,
// because lookups are absolute-time based and an unsynced clock sits at the
// 1970 epoch, where no cached entry can match.
bool try_render_cached_prices()
{
    if (!time_synced())
    {
        return false;
    }
    if (can_draw_prices_at(price_data, static_cast<int64_t>(time(nullptr))))
    {
        data_ok = true;
        lcd_draw_prices(lcd, price_data, price_limit);
        return true;
    }
    return false;
}

// ── WiFi recovery ────────────────────────────────────────────────────────

void on_wifi_recovered()
{
    wifi_ok = true;
    wifi_setup_promote(WiFi.SSID().c_str());
    if (!time_synced())
    {
        // The main loop's time-sync block will catch up on the next iteration.
        return;
    }
    struct tm local_time{};
    time_t const now = time(nullptr);
    localtime_r(&now, &local_time);
    ensure_prices(local_time.tm_hour);
}

void on_fetch_failed()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        wifi_failed_fallback();
        return;
    }
    if (try_render_cached_prices())
    {
        return;
    }
    data_ok = false;
    digitalWrite(PIN_ALERT, LOW);
    lcd_draw_message(lcd, MSG_NO_DATA, MSG_NO_DATA_L1, MSG_NO_DATA_L2);
    poll_limit_buttons_for_ms(3000);
}

void show_cached_or_error()
{
    if (try_render_cached_prices())
    {
        return;
    }
    data_ok = false;
    digitalWrite(PIN_ALERT, LOW);
    lcd_draw_message(lcd, MSG_NO_DATA, MSG_NO_DATA_L1, MSG_NO_DATA_L2);
}

void wifi_failed_fallback()
{
    if (try_render_cached_prices())
    {
        return;
    }
    data_ok = false;
    digitalWrite(PIN_ALERT, LOW);
    lcd_draw_message(lcd, "Pazudis WiFi!", "Nospiediet AP pogu,", "lai iestatītu WiFi");
}

// ── Main orchestration ───────────────────────────────────────────────────

bool ensure_prices(int const current_hour)
{
    if (!time_synced())
    {
        // expected_hours_for_date() asks the C library how long a local civil
        // day is, which needs the Latvian TZ that time_sync() exports and a
        // sane clock.  Without both, day-length math and the fetch dates would
        // be meaningless, so leave the cache untouched.
        return false;
    }

    char today_str[11];
    get_current_date(today_str, sizeof(today_str));
    char tomorrow_str[11];
    format_date_string(tomorrow_str, sizeof(tomorrow_str), 1);

    int expected_today = expected_hours_for_date(today_str);
    if (expected_today <= 0)
    {
        expected_today = 24;
    }

    // Today — fetch if incomplete
    if (count_entries(price_data, today_str) < expected_today)
    {
        FetchDateArg fetch_arg;
        strcpy(fetch_arg.date_str, today_str);
        if (!with_animation(lcd, AnimID::Globe, MSG_REFRESHING, fetch_date_work, &fetch_arg))
        {
            on_fetch_failed();
            return false;
        }
        merge_day(price_data, fetch_arg.day);
    }

    int expected_tomorrow = expected_hours_for_date(tomorrow_str);
    if (expected_tomorrow <= 0)
    {
        expected_tomorrow = 24;
    }

    // Tomorrow (after 16:00). Throttled because tomorrow's prices can be
    // genuinely unpublished for hours — without this we'd hit the API on
    // every 5-minute loop iteration the whole evening.
    if (current_hour >= 16 && count_entries(price_data, tomorrow_str) < expected_tomorrow)
    {
        unsigned long const now_ms = millis();
        bool const throttle_expired = (now_ms - last_tomorrow_attempt_ms) >= TOMORROW_RETRY_MS;
        if (!tomorrow_attempt_recorded || throttle_expired)
        {
            last_tomorrow_attempt_ms = now_ms;
            tomorrow_attempt_recorded = true;
            FetchDateArg fetch_arg;
            strcpy(fetch_arg.date_str, tomorrow_str);
            if (with_animation(lcd, AnimID::Globe, MSG_REFRESHING, fetch_date_work, &fetch_arg))
            {
                merge_day(price_data, fetch_arg.day);
            }
        }
    }

    // Final validation — check view window, render if not already drawn
    if (!can_draw_prices_at(price_data, static_cast<int64_t>(time(nullptr))))
    {
        on_fetch_failed();
        return false;
    }

    if (!data_ok)
    {
        data_ok = true;
        lcd_draw_prices(lcd, price_data, price_limit);
    }
    return true;
}

} // namespace

// ── Setup ────────────────────────────────────────────────────────────────

void setup()
{
    lcd.begin();

    // direct command sequence to set brightness
    u8x8_cad_StartTransfer(lcd.getU8x8());
    u8x8_cad_SendCmd(lcd.getU8x8(), 0x081); // set contrast
    u8x8_cad_SendArg(lcd.getU8x8(), 0x080); // ~50% of full range
    u8x8_cad_SendCmd(lcd.getU8x8(), 0x0d9); // pre-charge period
    u8x8_cad_SendArg(lcd.getU8x8(), 0x011); // lowest
    u8x8_cad_SendCmd(lcd.getU8x8(), 0x0db); // VCOMH deselect level
    u8x8_cad_SendArg(lcd.getU8x8(), 0x000); // lowest
    u8x8_cad_EndTransfer(lcd.getU8x8());

    lcd.enableUTF8Print();
    lcd.setFont(u8g2_font_helvR08_tr);

    pinMode(PIN_BUTTON_INCREMENT, INPUT_PULLUP);
    pinMode(PIN_BUTTON_DECREMENT, INPUT_PULLUP);
    pinMode(PIN_BUTTON_AP, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_BUTTON_AP), ap_isr, FALLING);
    pinMode(PIN_ALERT, OUTPUT);
    digitalWrite(PIN_ALERT, LOW);

    if (!wifi_has_saved_creds())
    {
        wifi_ok = wifi_start_ap_mode(lcd);
    }
    else
    {
        WifiResult const result = wifi_connect(lcd);
        if (result == WifiResult::Connected)
        {
            wifi_ok = true;
        }
        else if (result == WifiResult::Cancelled)
        {
            while (true)
            {
                if (wifi_start_ap_mode(lcd))
                {
                    wifi_ok = true;
                    break;
                }
                WifiResult const r = wifi_connect(lcd);
                if (r == WifiResult::Connected)
                {
                    wifi_ok = true;
                    break;
                }
                if (r == WifiResult::Cancelled)
                {
                    continue;
                }
                wifi_failed_fallback();
                last_wifi_retry = millis();
                break;
            }
        }
        else
        {
            wifi_ok = false;
            wifi_failed_fallback();
            last_wifi_retry = millis();
        }
    }

    if (wifi_ok)
    {
        wifi_setup_promote(WiFi.SSID().c_str());
        struct tm setup_tm{};
        time_t const setup_now = time(nullptr);
        localtime_r(&setup_now, &setup_tm);

        if (!ensure_prices(setup_tm.tm_hour))
        {
            wifi_ok = false;
            show_cached_or_error();
            last_wifi_retry = millis();
        }
    }
    else
    {
        wifi_failed_fallback();
        last_wifi_retry = millis();
    }
}

// ── AP button recovery ───────────────────────────────────────────────────

namespace {

void handle_ap_button_recovery()
{
    while (true)
    {
        if (wifi_start_ap_mode(lcd))
        {
            on_wifi_recovered();
            return;
        }
        WifiResult const r = wifi_connect(lcd);
        if (r == WifiResult::Connected)
        {
            wifi_ok = true;
            on_wifi_recovered();
            return;
        }
        if (r == WifiResult::Cancelled)
        {
            continue;
        }
        wifi_ok = false;
        wifi_failed_fallback();
        last_wifi_retry = millis();
        return;
    }
}

} // namespace

// ── Loop ─────────────────────────────────────────────────────────────────

void loop()
{
    if (handle_ap_button(lcd))
    {
        handle_ap_button_recovery();
        return;
    }

    // === WiFi recovery path ===
    if (!wifi_ok)
    {
        unsigned long const now_ms = millis();
        if (now_ms - last_wifi_retry >= 60000)
        {
            last_wifi_retry = now_ms;
            WifiResult const retry_result = wifi_try_saved_creds(lcd);
            if (retry_result == WifiResult::Connected)
            {
                on_wifi_recovered();
            }
            else if (retry_result == WifiResult::Cancelled)
            {
                wifi_ok = wifi_start_ap_mode(lcd);
                if (wifi_ok)
                {
                    on_wifi_recovered();
                }
                else
                {
                    wifi_failed_fallback();
                    last_wifi_retry = millis();
                }
            }
            else
            {
                wifi_failed_fallback();
            }
        }

        poll_limit_buttons_for_ms(500);
        return;
    }

    // === Runtime WiFi monitor ===
    if (WiFi.status() != WL_CONNECTED)
    {
        wifi_ok = false;
        wifi_failed_fallback();
        last_wifi_retry = millis();
        poll_limit_buttons_for_ms(500);
        return;
    }

    // === Time sync ===
    if (!time_synced())
    {
        unsigned long const now_ms = millis();
        if (now_ms - last_time_retry >= 15000)
        {
            last_time_retry = now_ms;
            if (time_sync())
            {
                // synced — continue to normal flow below
            }
            else
            {
                wifi_ok = false;
                show_cached_or_error();
                last_wifi_retry = millis();
            }
        }
        poll_limit_buttons_for_ms(300);
        return;
    }

    // === Normal operation ===
    struct tm local_time{};
    time_t const now = time(nullptr);
    localtime_r(&now, &local_time);

    int const hour = local_time.tm_hour;

    char today_str[11];
    get_current_date(today_str, sizeof(today_str));

    // Prune after 2:00, once per day
    if (hour >= 2 && strcmp(last_prune_date, today_str) != 0)
    {
        char yesterday_str[11];
        format_date_string(yesterday_str, sizeof(yesterday_str), -1);
        price_data.erase(
            std::remove_if(price_data.begin(), price_data.end(),
                           [&](PriceEntry const& e) { return strcmp(e.date_string, yesterday_str) < 0; }),
            price_data.end());
        strcpy(last_prune_date, today_str);
    }

    // === Empty price retry ===
    if (price_data.empty())
    {
        unsigned long const now_ms = millis();
        if (now_ms - last_price_retry >= 120000)
        {
            last_price_retry = now_ms;
            ensure_prices(hour);
        }
        delay(300);
        return;
    }

    // === Ensure view window is complete ===
    if (!ensure_prices(hour))
    {
        wifi_ok = false;
        last_wifi_retry = millis();
        return;
    }

    if (data_ok)
    {
        handle_limit_buttons();
        lcd_draw_prices(lcd, price_data, price_limit);
    }

    int const next_min = ((local_time.tm_min / 5) + 1) * 5;
    int const seconds_until = (next_min - local_time.tm_min) * 60 - local_time.tm_sec;
    int const ticks = seconds_until * 100;
    for (int remain = ticks; remain > 0; --remain)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        esp_task_wdt_reset();

        if (handle_ap_button(lcd))
        {
            handle_ap_button_recovery();
            return;
        }

        if (data_ok && handle_limit_buttons())
        {
            lcd_draw_prices(lcd, price_data, price_limit);
        }
    }
}
