// wifi_setup.h — Captive‑portal WiFi setup mode
//
// When the ESP can't connect, wifi_setup_ap_start() creates a WiFi access
// point with a DNS server that catches all hostnames (captive portal) and an
// HTTP server that serves the setup page. Users connect and submit their home
// WiFi credentials. A FreeRTOS task on Core 0 monitors the connection and
// syncs the clock via NTP. The browser polls GET /status for progress.
//
// Supports open WiFi (empty password). NTP uses configTime + separate
// setenv/tzset to avoid ESP32-S3 core‑0 stalls.
//
// Usage (see wifi.cpp):
//   if (every saved credential fails)
//       wifi_setup_ap_start();
//       while (wifi_setup_is_active()) { wifi_setup_handle(); ... }

#pragma once

#include <Arduino.h>

extern const char WIFI_SETUP_PAGE[];

enum class ConnectionState
{
    Idle,
    Saved,
    Connected,
    Synced,
    Failed
};

void wifi_setup_generate_ap_password();
void wifi_setup_ap_start();
void wifi_setup_handle();
bool wifi_setup_is_active();
void wifi_setup_shutdown();

void wifi_setup_save_credentials(const char* ssid, const char* password);
bool wifi_setup_load_credentials(String& out_ssid, String& out_password);
int wifi_setup_saved_count();
bool wifi_setup_get_saved(int index, String& out_ssid, String& out_password);
void wifi_setup_delete_credentials(const char* ssid);
void wifi_setup_promote(const char* ssid);

ConnectionState wifi_setup_status();
void wifi_setup_set_status(ConnectionState state);

int wifi_setup_original_channel();

const char* wifi_setup_status_str(ConnectionState state);

extern const char* g_fail_reason;

const char* wifi_setup_ap_password();

#define WIFI_SETUP_AP_SSID "Limitrozete"

enum
{
    WIFI_SSID_MAX_LEN = 32,
    WIFI_PASS_MAX_LEN = 63,
    WIFI_MAX_SAVED_NETWORKS = 30
};
