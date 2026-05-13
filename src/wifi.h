// wifi.h — WiFi connection helpers
//
// wifi_connect() is called once from setup(). It tries saved credentials
// (from two independent sources) and returns true/false — no blocking AP.
// wifi_start_ap_mode() runs the captive‑portal AP (blocking, user must
// configure WiFi via browser). wifi_try_saved_creds() retries saved creds.

#pragma once

#include <Arduino.h>

class U8G2_ST7920_128X64_F_SW_SPI;

enum class WifiResult
{
    Connected,
    Failed,
    Cancelled
};

/// Try saved credentials (NVS recall + Preferences). Returns Connected on success,
/// Cancelled if user pressed the AP button, Failed on timeout / no creds.
/// Does NOT start the captive portal — caller handles failure / cancellation.
WifiResult wifi_connect(U8G2_ST7920_128X64_F_SW_SPI& lcd);

/// ISR handler for the AP button (FALLING edge).
void IRAM_ATTR ap_isr();

/// Check if the AP button was pressed (debounced, 500ms).
bool ap_button_pressed();

/// True immediately when the AP button ISR fires (no debounce wait).
bool ap_button_pending();

/// Clear the pending AP button flag without waiting for debounce.
void ap_reset();

/// Check AP button in the main loop: shows "Uzstādīšana..." feedback when
/// pending, returns true once the 500ms hold is confirmed (caller handles
/// recovery and should return from loop()).
bool handle_ap_button(U8G2_ST7920_128X64_F_SW_SPI& lcd);

/// Start captive‑portal AP mode. Blocks until the user configures WiFi
/// and time is synced, or cancels via AP button.
/// Returns true if Synced, false if cancelled.
bool wifi_start_ap_mode(U8G2_ST7920_128X64_F_SW_SPI& lcd);

/// Try all saved Preferences credentials in order (3s timeout each).
/// Shows per-SSID failure feedback on lcd if all attempts fail.
WifiResult wifi_try_saved_creds(U8G2_ST7920_128X64_F_SW_SPI& lcd);

/// Returns true if at least one network is saved in Preferences.
bool wifi_has_saved_creds();

/// Number of saved networks in Preferences.
int wifi_saved_network_count();
