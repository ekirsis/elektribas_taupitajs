// wifi.cpp — WiFi connection logic
//
// wifi_connect() runs at boot. It tries NVS recall first (ESP32 remembers
// the last connected network), then iterates all saved networks stored in
// Preferences using the indexed format (ssid_0/ pass_0, ssid_1/ pass_1, …).
//
// Each attempt blocks up to 3 seconds (200ms polling) and calls time_sync()
// on success. When none work, the caller (main.cpp) shows "Pazudis WiFi!"
// and retries periodically.
//
// wifi_start_ap_mode() provides a captive‑portal AP so the user can
// configure WiFi from their phone browser. Credentials are appended to the
// Preferences list via /save (no overwrite). The /delete endpoint removes
// a saved network. /saved returns all saved SSIDs for the web UI.

#include "wifi.h"
#include <WiFi.h>
#include <algorithm>
#include <esp_task_wdt.h>
#include "wifi_setup.h"
#include "pins.h"
#include "time_sync.h"
#include "lcd_display.h"
#include "price_fetch.h"
#include <esp_wifi.h>

// AP button ISR (FALLING edge, GPIO 6)
namespace {

volatile bool ap_btn_flagged = false;
volatile unsigned long ap_btn_time = 0;

} // namespace

void IRAM_ATTR ap_isr()
{
    unsigned long now = millis();
    if (now - ap_btn_time < 50)
    {
        return;
    }
    ap_btn_time = now;
    if (digitalRead(PIN_BUTTON_AP) == LOW)
    {
        ap_btn_flagged = true;
    }
}

bool ap_button_pending()
{
    return ap_btn_flagged;
}

void ap_reset()
{
    ap_btn_flagged = false;
}

bool ap_button_pressed()
{
    if (!ap_btn_flagged)
    {
        return false;
    }
    if (static_cast<unsigned long>(millis() - ap_btn_time) < 500)
    {
        return false;
    }
    ap_btn_flagged = false;
    return true;
}

bool handle_ap_button(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& lcd)
{
    static bool feedback_shown = false;

    if (ap_button_pending() && wifi_has_saved_creds() && !feedback_shown)
    {
        lcd_draw_message(lcd, "Uzstādīšana...");
        feedback_shown = true;
    }

    if (ap_button_pressed() && wifi_has_saved_creds())
    {
        feedback_shown = false;
        return true;
    }

    return false;
}

namespace {

/// Scan visible WiFi networks, return their SSIDs.
/// Aborts and returns empty if AP button is pressed (sets *cancelled).
std::vector<String> wifi_scan_visible(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& lcd, bool* cancelled = nullptr)
{
    std::vector<String> visible;
    if (cancelled)
    {
        *cancelled = false;
    }

    // Clean radio state before scan — a previous failed WiFi.begin() may leave
    // the chip in a transitional state that returns empty scan results
    WiFi.disconnect(false);
    delay(100);
    WiFi.scanNetworks(true);
    int frame = 0;
    for (int i = 0; i < 50; ++i)
    {
        if (ap_button_pending())
        {
            WiFi.scanDelete();
            ap_reset();
            if (cancelled)
            {
                *cancelled = true;
            }
            return visible;
        }
        lcd_draw_animated_message(lcd, AnimID::Globe, frame, "Meklē tīklus...");
        frame = anim_next_frame(AnimID::Globe, frame);
        delay(100);
        yield();
        esp_task_wdt_reset();

        int n = WiFi.scanComplete();
        if (n >= 0)
        {
            visible.reserve(n);
            for (int j = 0; j < n; ++j)
            {
                visible.push_back(WiFi.SSID(j));
            }
            WiFi.scanDelete();
            return visible;
        }
    }
    WiFi.scanDelete();
    return visible;
}

WifiResult wait_for_wifi_and_sync_time(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& lcd, int max_seconds)
{
    int frame = 0;

    // Phase 1: wait for WiFi to connect (animated)
    for (int i = 0; i < max_seconds * 5; ++i)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            break;
        }
        if (ap_button_pending())
        {
            WiFi.disconnect(false);
            ap_reset();
            return WifiResult::Cancelled;
        }
        lcd_draw_animated_message(lcd, AnimID::Globe, frame, "Savienojas...");
        frame = anim_next_frame(AnimID::Globe, frame);
        delay(200);
        yield();
        esp_task_wdt_reset();
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        return WifiResult::Failed;
    }

    // Phase 2: NTP time sync (animated)
    time_sync_init();
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        if (ap_button_pending())
        {
            WiFi.disconnect(false);
            ap_reset();
            return WifiResult::Cancelled;
        }
        lcd_draw_animated_message(lcd, AnimID::Globe, frame, "Sinhronizē laiku...");
        frame = anim_next_frame(AnimID::Globe, frame);

        int const wait_ms = (attempt == 0) ? 500 : 1000;
        delay(wait_ms);
        yield();
        esp_task_wdt_reset();

        // Check BEFORE NTP poll — press during delay takes priority even if
        // NTP happened to respond at the same moment
        if (ap_button_pending())
        {
            WiFi.disconnect(false);
            ap_reset();
            return WifiResult::Cancelled;
        }

        if (time_sync_poll())
        {
            return WifiResult::Connected;
        }
    }
    return WifiResult::Failed;
}

} // namespace

WifiResult wifi_connect(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& lcd)
{
    if (ap_button_pending())
    {
        ap_reset();
        return WifiResult::Cancelled;
    }

    lcd_draw_message(lcd, "Savienojas...");

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);

    WiFi.begin();
    WifiResult result = wait_for_wifi_and_sync_time(lcd, 3);
    if (result == WifiResult::Connected)
    {
        return WifiResult::Connected;
    }
    if (result == WifiResult::Cancelled)
    {
        return WifiResult::Cancelled;
    }

    return wifi_try_saved_creds(lcd);
}

WifiResult wifi_try_saved_creds(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& lcd)
{
    if (ap_button_pending())
    {
        ap_reset();
        return WifiResult::Cancelled;
    }

    int const count = std::min(wifi_setup_saved_count(), 5);
    if (count == 0)
    {
        return WifiResult::Failed;
    }

    // Scan for visible networks so we skip saved networks that aren't around
    bool scan_cancelled = false;
    std::vector<String> visible = wifi_scan_visible(lcd, &scan_cancelled);
    if (scan_cancelled)
    {
        return WifiResult::Cancelled;
    }

    for (int i = 0; i < count; i++)
    {
        String saved_ssid, saved_password;
        wifi_setup_get_saved(i, saved_ssid, saved_password);
        if (saved_ssid.length() == 0)
        {
            continue;
        }

        // Skip networks not found in the scan (non-empty scan = trustworthy)
        if (!visible.empty())
        {
            auto it = std::find(visible.begin(), visible.end(), saved_ssid);
            if (it == visible.end())
            {
                continue;
            }
        }

        // NOLINTNEXTLINE(bugprone-branch-clone) — overloads differ: (ssid) vs (ssid, password)
        if (saved_password.length() > 0)
        {
            WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
        }
        else
        {
            WiFi.begin(saved_ssid.c_str());
        }

        WifiResult result = wait_for_wifi_and_sync_time(lcd, 3);
        if (result == WifiResult::Connected)
        {
            return WifiResult::Connected;
        }
        if (result == WifiResult::Cancelled)
        {
            return WifiResult::Cancelled;
        }

        char wifi_buf[29];
        snprintf(wifi_buf, sizeof(wifi_buf), "ar WiFi: %s", saved_ssid.c_str());
        int frame = 0;
        for (int j = 0; j < 15; ++j)
        {
            if (ap_button_pending())
            {
                WiFi.disconnect(false);
                ap_reset();
                return WifiResult::Cancelled;
            }
            lcd_draw_animated_message(lcd, AnimID::Globe, frame, "Neizdevās savienoties", wifi_buf);
            frame = anim_next_frame(AnimID::Globe, frame);
            delay(200);
            yield();
            esp_task_wdt_reset();
        }
    }
    return WifiResult::Failed;
}

namespace {

void show_ap_instructions(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& lcd)
{
    char pass_line[24];
    snprintf(pass_line, sizeof(pass_line), "parole: %s", wifi_setup_ap_password());
    lcd_draw_message(lcd, "Uzstādīšana", "Savienojaties ar WiFi AP", "SSID: Limitrozete", pass_line,
                     "Apmeklējat: 10.1.1.1");
}

} // namespace

bool wifi_start_ap_mode(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& lcd)
{
    digitalWrite(PIN_ALERT, LOW);
    wifi_setup_generate_ap_password();
    show_ap_instructions(lcd);

    if (ap_button_pressed() && wifi_has_saved_creds())
    {
        lcd_draw_message(lcd, "Atslēdz...");
        delay(500);
        return false;
    }

    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_AP_STA);

    wifi_setup_ap_start();

    if (ap_button_pressed() && wifi_has_saved_creds())
    {
        lcd_draw_message(lcd, "Atslēdz...");
        delay(500);
        wifi_setup_shutdown();
        return false;
    }

    int frame = 0;
    while (wifi_setup_is_active())
    {
        wifi_setup_handle();

        ConnectionState const s = wifi_setup_status();
        bool const connecting = (s == ConnectionState::Saved || s == ConnectionState::Connected);

        bool const exit_pending = ap_button_pending() && wifi_has_saved_creds() && !connecting;

        if (exit_pending)
        {
            lcd_draw_message(lcd, "Atslēdz...");
        }

        if (s == ConnectionState::Synced)
        {
            lcd_draw_message(lcd, "Laiks sinhronizēts!");
            for (int i = 0; i < 70; i++)
            {
                wifi_setup_handle();
                if (ap_button_pressed() && wifi_has_saved_creds())
                {
                    wifi_setup_shutdown();
                    return false;
                }
                delay(100);
            }
            wifi_setup_shutdown();
            price_fetch_invalidate_url_cache();
            return true;
        }

        if (!connecting && ap_button_pressed() && wifi_has_saved_creds())
        {
            wifi_setup_shutdown();
            return false;
        }

        if (!exit_pending)
        {
            if (s == ConnectionState::Saved)
            {
                lcd_draw_animated_message(lcd, AnimID::Globe, frame, "Savienojas...");
            }
            else if (s == ConnectionState::Connected)
            {
                lcd_draw_animated_message(lcd, AnimID::Globe, frame, "Sinhronizē laiku...");
            }
            else if (s == ConnectionState::Failed)
            {
                // Kill STA retry loop — otherwise it chokes the shared radio
                WiFi.disconnect(true);
                // Restore AP to original channel so the phone reconnects quickly
                int orig = wifi_setup_original_channel();
                if (orig > 0)
                {
                    esp_wifi_set_channel(static_cast<uint8_t>(orig), WIFI_SECOND_CHAN_NONE);
                    delay(50);
                }
                // Show error on LCD once (g_fail_reason is non-null on first pass only)
                if (g_fail_reason)
                {
                    lcd_draw_message(lcd, g_fail_reason);
                    delay(8000);
                    show_ap_instructions(lcd);
                    g_fail_reason = nullptr;
                }
            }
            else
            {
                show_ap_instructions(lcd);
            }
        }

        frame = anim_next_frame(AnimID::Globe, frame);
        delay(50);
        yield();
    }

    return false;
}

bool wifi_has_saved_creds()
{
    return wifi_setup_saved_count() > 0;
}

int wifi_saved_network_count()
{
    return wifi_setup_saved_count();
}
