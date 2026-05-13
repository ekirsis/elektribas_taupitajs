// wifi_setup.cpp — Captive‑portal WiFi setup mode
//
// When the ESP can't connect to a known network it starts a WiFi access point,
// DNS server and HTTP server. The user connects to "Limitrozete" from their
// phone, the captive portal page pops up automatically (DNS catches *all*
// hostnames), and they pick their home WiFi + enter the password.
//
// Dual‑core design (FreeRTOS):
//   Core 0 — wifi_monitor_task: watches WiFi.status(), calls time_sync()
//            when the station connects. This is the "slow" work.
//   Core 1 — Arduino loop: runs DNS + HTTP so the browser never waits.
//
// The browser polls GET /status every second to see how the connection
// attempt is going (Idle → Saved → Connected → Synced, or → Failed).
//
// Time sync details:
//   After WiFi connects, a 500ms settle delay gives DHCP/DNS time to finish.
//   NTP sync uses configTime(UTC) + separate setenv("TZ") + tzset() —
//   avoids ESP32-S3 stall when SNTP initialises from Core 0.
//   Up to 4.5s for NTP (500ms initial delay + 4 × 1s polls).
//   If time sync fails, the Connected state has its own 15s timeout
//   before transitioning to Failed.
//   Overall timeout: 15s (connect) + 15s (time sync) max.
//
// Open WiFi (empty password) is supported server-side by the form.

#include "wifi_setup.h"
#include "wifi_setup_page.h"
#include "wifi.h"
#include "time_sync.h"
#include "price_fetch.h"
#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <vector>

namespace {

IPAddress AP_GATEWAY(10, 1, 1, 1);

WebServer* http_server = nullptr;
DNSServer* dns_server = nullptr;
bool ap_is_running = false;

const char AP_PASSWORD_CHARS[] = "abcdefghijklmnpqrstuvwxyz123456789";
char g_ap_password[9] = {0};
bool g_ap_password_generated = false;

std::vector<String> cached_network_ssids;
std::vector<int> cached_network_channels;

ConnectionState connection_status = ConnectionState::Idle;
uint64_t connection_attempt_started_at = 0;
int g_original_ap_channel = 0;

} // namespace

const char* g_fail_reason = nullptr;

namespace {

TaskHandle_t wifi_monitor_task_handle = nullptr;

void wifi_monitor_task(void* /*pvParameters*/)
{
    while (true)
    {
        if (connection_status == ConnectionState::Saved)
        {
            const wl_status_t wifi_status = WiFi.status();

            if (wifi_status == WL_CONNECTED)
            {
                connection_status = ConnectionState::Connected;
                connection_attempt_started_at = millis();
                delay(500);
                yield();
                if (time_sync())
                {
                    connection_status = ConnectionState::Synced;
                }
            }
            else if (wifi_status == WL_CONNECT_FAILED)
            {
                g_fail_reason = "Nepareiza parole";
                connection_status = ConnectionState::Failed;
            }
            else if (wifi_status == WL_NO_SSID_AVAIL)
            {
                g_fail_reason = "Tīkls nav atrodams";
                connection_status = ConnectionState::Failed;
            }

            if (connection_attempt_started_at > 0 && millis() - connection_attempt_started_at > 15000 &&
                connection_status == ConnectionState::Saved)
            {
                g_fail_reason = "Savienojums neizdevās";
                connection_status = ConnectionState::Failed;
            }
        }

        if (connection_status == ConnectionState::Connected)
        {
            if (time_synced())
            {
                connection_status = ConnectionState::Synced;
            }
            else if (connection_attempt_started_at > 0 && millis() - connection_attempt_started_at > 15000)
            {
                g_fail_reason = "Laika sinhronizācija neizdevās";
                connection_status = ConnectionState::Failed;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

} // namespace

void wifi_setup_save_credentials(const char* ssid, const char* password)
{
    Preferences prefs;
    prefs.begin("wifi_cfg", false);

    int count = prefs.getInt("count", 0);

    for (int i = 0; i < count; i++)
    {
        const String key = "ssid_" + String(i);
        if (prefs.getString(key.c_str(), "") == ssid)
        {
            const String pk = "pass_" + String(i);
            prefs.putString(pk.c_str(), password);
            prefs.end();
            return;
        }
    }

    // Cap reached: overwrite the least-recently-used entry (last index)
    if (count >= WIFI_MAX_SAVED_NETWORKS)
    {
        int const last = WIFI_MAX_SAVED_NETWORKS - 1;
        prefs.putString(("ssid_" + String(last)).c_str(), ssid);
        prefs.putString(("pass_" + String(last)).c_str(), password);
        prefs.end();
        return;
    }

    const String sk = "ssid_" + String(count);
    const String pk = "pass_" + String(count);
    prefs.putString(sk.c_str(), ssid);
    prefs.putString(pk.c_str(), password);
    prefs.putInt("count", count + 1);
    prefs.end();
}

bool wifi_setup_load_credentials(String& out_ssid, String& out_password)
{
    Preferences prefs;
    prefs.begin("wifi_cfg", false);

    int count = prefs.getInt("count", -1);

    if (count > 0)
    {
        out_ssid = prefs.getString("ssid_0", "");
        out_password = prefs.getString("pass_0", "");
        prefs.end();
        return out_ssid.length() > 0;
    }

    out_ssid = prefs.getString("ssid", "");
    out_password = prefs.getString("pass", "");

    if (out_ssid.length() > 0)
    {
        prefs.putInt("count", 1);
        prefs.putString("ssid_0", out_ssid.c_str());
        prefs.putString("pass_0", out_password.c_str());
        prefs.remove("ssid");
        prefs.remove("pass");
        prefs.end();
        return true;
    }

    prefs.end();
    return false;
}

int wifi_setup_saved_count()
{
    Preferences prefs;
    prefs.begin("wifi_cfg", true);
    int count = prefs.getInt("count", 0);
    prefs.end();
    return count;
}

bool wifi_setup_get_saved(int index, String& out_ssid, String& out_password)
{
    Preferences prefs;
    prefs.begin("wifi_cfg", true);
    int count = prefs.getInt("count", 0);
    if (index < 0 || index >= count)
    {
        prefs.end();
        return false;
    }
    const String sk = "ssid_" + String(index);
    const String pk = "pass_" + String(index);
    out_ssid = prefs.getString(sk.c_str(), "");
    out_password = prefs.getString(pk.c_str(), "");
    prefs.end();
    return out_ssid.length() > 0;
}

void wifi_setup_delete_credentials(const char* ssid)
{
    Preferences prefs;
    prefs.begin("wifi_cfg", false);

    int count = prefs.getInt("count", 0);
    int found = -1;

    for (int i = 0; i < count; i++)
    {
        const String sk = "ssid_" + String(i);
        if (prefs.getString(sk.c_str(), "") == ssid)
        {
            found = i;
            break;
        }
    }

    if (found >= 0)
    {
        for (int i = found; i < count - 1; i++)
        {
            const String ssk = "ssid_" + String(i + 1);
            const String spk = "pass_" + String(i + 1);
            const String dsk = "ssid_" + String(i);
            const String dpk = "pass_" + String(i);
            prefs.putString(dsk.c_str(), prefs.getString(ssk.c_str(), ""));
            prefs.putString(dpk.c_str(), prefs.getString(spk.c_str(), ""));
        }
        const String lsk = "ssid_" + String(count - 1);
        const String lpk = "pass_" + String(count - 1);
        prefs.remove(lsk.c_str());
        prefs.remove(lpk.c_str());
        prefs.putInt("count", count - 1);
    }

    prefs.end();
}

void wifi_setup_promote(const char* ssid)
{
    Preferences prefs;
    prefs.begin("wifi_cfg", false);

    int const count = prefs.getInt("count", 0);
    if (count < 2)
    {
        prefs.end();
        return;
    }

    if (prefs.getString("ssid_0", "") == ssid)
    {
        prefs.end();
        return;
    }

    int found = -1;
    String found_pass;
    for (int i = 0; i < count; i++)
    {
        const String sk = "ssid_" + String(i);
        if (prefs.getString(sk.c_str(), "") == ssid)
        {
            found = i;
            const String pk = "pass_" + String(i);
            found_pass = prefs.getString(pk.c_str(), "");
            break;
        }
    }

    if (found < 0)
    {
        prefs.end();
        return;
    }

    for (int i = found; i > 0; i--)
    {
        const String ssk = "ssid_" + String(i - 1);
        const String spk = "pass_" + String(i - 1);
        const String dsk = "ssid_" + String(i);
        const String dpk = "pass_" + String(i);
        prefs.putString(dsk.c_str(), prefs.getString(ssk.c_str(), ""));
        prefs.putString(dpk.c_str(), prefs.getString(spk.c_str(), ""));
    }

    prefs.putString("ssid_0", ssid);
    prefs.putString("pass_0", found_pass);
    prefs.end();
}

void wifi_setup_shutdown()
{
    if (!ap_is_running || !http_server || !dns_server)
    {
        return;
    }

    if (wifi_monitor_task_handle)
    {
        vTaskDelete(wifi_monitor_task_handle);
        wifi_monitor_task_handle = nullptr;
    }

    http_server->stop();
    delete http_server;
    http_server = nullptr;

    dns_server->stop();
    delete dns_server;
    dns_server = nullptr;

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    ap_is_running = false;
}

void wifi_setup_handle()
{
    if (ap_is_running && dns_server)
    {
        dns_server->processNextRequest();
    }
    if (http_server)
    {
        http_server->handleClient();
    }
}

bool wifi_setup_is_active()
{
    return ap_is_running;
}

ConnectionState wifi_setup_status()
{
    return connection_status;
}

void wifi_setup_set_status(ConnectionState state)
{
    connection_status = state;
}

int wifi_setup_original_channel()
{
    return g_original_ap_channel;
}

const char* wifi_setup_status_str(ConnectionState state)
{
    switch (state)
    {
    case ConnectionState::Idle:
        return "";
    case ConnectionState::Saved:
        return "saved";
    case ConnectionState::Connected:
        return "connected";
    case ConnectionState::Synced:
        return "synced";
    case ConnectionState::Failed:
        return "failed";
    }
    return "";
}

const char* wifi_setup_ap_password()
{
    return g_ap_password;
}

void wifi_setup_generate_ap_password()
{
    if (g_ap_password_generated)
    {
        return;
    }
    for (int i = 0; i < 8; i++)
    {
        g_ap_password[i] = AP_PASSWORD_CHARS[esp_random() % (sizeof(AP_PASSWORD_CHARS) - 1)];
    }
    g_ap_password[8] = '\0';
    g_ap_password_generated = true;
}

namespace {

void handleRoot();
void handleScan();
void handleRescan();
void handleSave();
void handleDelete();
void handleSaved();
void handleReconnect();
void handleStatus();
void handleApiSettings();
void handleCaptivePortal204();
void handleCaptivePortalIos();
void handleCaptivePortalWindows();

void send_error(const char* msg)
{
    http_server->send(400, "application/json", "{\"success\":false,\"error\":\"" + String(msg) + "\"}");
}

void cache_scan_results(int n)
{
    if (n < 0)
    {
        n = 0;
    }
    cached_network_ssids.clear();
    cached_network_ssids.reserve(n);
    cached_network_channels.clear();
    cached_network_channels.reserve(n);
    for (int i = 0; i < n; i++)
    {
        cached_network_ssids.push_back(WiFi.SSID(i));
        cached_network_channels.push_back(WiFi.channel(i));
    }
    WiFi.scanDelete();
}

} // namespace

void wifi_setup_ap_start()
{
    connection_status = ConnectionState::Idle;
    connection_attempt_started_at = 0;
    cached_network_ssids.clear();
    cached_network_channels.clear();
    g_fail_reason = nullptr;

    WiFi.softAPConfig(AP_GATEWAY, AP_GATEWAY, IPAddress(255, 255, 255, 0));
    WiFi.softAP(WIFI_SETUP_AP_SSID, g_ap_password);

    // Save original AP channel so we can restore it on connection failure
    {
        uint8_t primary;
        wifi_second_chan_t second;
        if (esp_wifi_get_channel(&primary, &second) == ESP_OK)
        {
            g_original_ap_channel = primary;
        }
        else
        {
            g_original_ap_channel = 0;
        }
    }

    // Pre-scan before DNS/HTTP start — no clients are connected yet
    {
        WiFi.scanNetworks(true);
        int n;
        bool scan_aborted = false;
        // NOLINTNEXTLINE(bugprone-infinite-loop) — n is reassigned via WiFi.scanComplete() each iteration
        do
        {
            delay(10);
            esp_task_wdt_reset();
            n = WiFi.scanComplete();
            if (ap_button_pending())
            {
                ap_reset();
                scan_aborted = true;
                break;
            }
        } while (n < 0);
        if (!scan_aborted)
        {
            cache_scan_results(n);
        }
        else
        {
            WiFi.scanDelete();
        }
    }

    dns_server = new DNSServer();
    dns_server->start(53, "*", AP_GATEWAY);

    http_server = new WebServer(80);
    http_server->on("/", handleRoot);
    http_server->on("/scan", handleScan);
    http_server->on("/rescan", HTTP_POST, handleRescan);
    http_server->on("/save", HTTP_POST, handleSave);
    http_server->on("/delete", HTTP_POST, handleDelete);
    http_server->on("/saved", handleSaved);
    http_server->on("/reconnect", HTTP_POST, handleReconnect);
    http_server->on("/status", handleStatus);
    http_server->on("/api-settings", HTTP_GET, handleApiSettings);
    http_server->on("/api-settings", HTTP_POST, handleApiSettings);
    // Captive portal detection bypass — phones/OS test internet by fetching these URLs.
    // Return the expected success response so they think internet is available
    // and suppress the "Sign in to WiFi" notification.
    http_server->on("/generate_204", handleCaptivePortal204);
    http_server->on("/hotspot-detect.html", handleCaptivePortalIos);
    http_server->on("/connecttest.txt", handleCaptivePortalWindows);
    http_server->on("/favicon.ico", handleCaptivePortal204);
    http_server->onNotFound(handleRoot);
    http_server->begin();

    ap_is_running = true;

    xTaskCreatePinnedToCore(wifi_monitor_task, "wifi_monitor", 4096, NULL, 1, &wifi_monitor_task_handle, 0);
}

namespace {

void handleCaptivePortal204()
{
    if (http_server)
    {
        http_server->send(204);
    }
}

void handleCaptivePortalIos()
{
    if (!http_server)
    {
        return;
    }
    http_server->send(200, "text/html",
                      "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2//EN\">"
                      "<HTML><HEAD><TITLE>Success</TITLE></HEAD>"
                      "<BODY>Success</BODY></HTML>");
}

void handleCaptivePortalWindows()
{
    if (!http_server)
    {
        return;
    }
    http_server->send(200, "text/plain", "Microsoft Connect Test");
}

void handleRoot()
{
    if (!http_server)
    {
        return;
    }
    String etag = http_server->header("If-None-Match");
    String expected = "\"" + String(WEBUI_VERSION) + "\"";
    if (etag == expected)
    {
        http_server->send(304);
        return;
    }
    http_server->sendHeader("ETag", expected);
    http_server->send_P(200, "text/html; charset=utf-8", WIFI_SETUP_PAGE);
}

void handleScan()
{
    if (!http_server)
    {
        return;
    }

    // NOLINTNEXTLINE(misc-const-correctness) — to<JsonArray>() mutates through returned ref
    JsonDocument document;
    JsonArray network_list = document.to<JsonArray>();
    for (const auto& ssid : cached_network_ssids)
        network_list.add(ssid);

    String json;
    serializeJson(document, json);
    http_server->send(200, "application/json", json);
}

void handleRescan()
{
    if (!http_server)
    {
        return;
    }

    WiFi.scanNetworks(true);

    int n;
    do
    {
        dns_server->processNextRequest();
        delay(5);
        n = WiFi.scanComplete();
    } while (n < 0 && ap_is_running);

    cache_scan_results(n);
    http_server->send(200, "application/json", "{\"success\":true}");
}

void do_wifi_connect(const char* ssid, const char* password, bool has_password)
{
    int channel = 0;
    for (size_t i = 0; i < cached_network_ssids.size(); i++)
    {
        if (cached_network_ssids[i] == ssid)
        {
            channel = cached_network_channels[i];
            break;
        }
    }

    WiFi.disconnect(false);
    if (has_password)
        WiFi.begin(ssid, password, channel);
    else
        WiFi.begin(ssid, NULL, channel);

    connection_status = ConnectionState::Saved;
    connection_attempt_started_at = millis();
    g_fail_reason = nullptr;

    http_server->send(200, "application/json", "{\"success\":true}");
}

void handleSave()
{
    if (!http_server || !http_server->hasArg("plain"))
    {
        send_error("No data");
        return;
    }

    JsonDocument document;
    DeserializationError parse_error = deserializeJson(document, http_server->arg("plain"));
    if (parse_error)
    {
        send_error("Invalid JSON");
        return;
    }

    const char* submitted_ssid = document["ssid"].as<const char*>();
    const char* submitted_password = document["password"].as<const char*>();
    if (!submitted_ssid || strlen(submitted_ssid) == 0)
    {
        send_error("SSID cannot be empty");
        return;
    }
    if (strlen(submitted_ssid) > WIFI_SSID_MAX_LEN)
    {
        send_error("SSID too long");
        return;
    }
    if (submitted_password && strlen(submitted_password) > WIFI_PASS_MAX_LEN)
    {
        send_error("Password too long");
        return;
    }

    wifi_setup_save_credentials(submitted_ssid, submitted_password);

    do_wifi_connect(submitted_ssid, submitted_password, submitted_password && strlen(submitted_password) > 0);
}

void handleSaved()
{
    if (!http_server)
    {
        return;
    }

    // NOLINTNEXTLINE(misc-const-correctness) — to<JsonArray>() mutates through returned ref
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    int const count = wifi_setup_saved_count();
    for (int i = 0; i < count; i++)
    {
        String ssid;
        String pass;
        wifi_setup_get_saved(i, ssid, pass);
        if (ssid.length() > 0)
        {
            arr.add(ssid);
        }
    }

    String json;
    serializeJson(doc, json);
    http_server->send(200, "application/json", json);
}

void handleReconnect()
{
    if (!http_server || !http_server->hasArg("plain"))
    {
        send_error("No data");
        return;
    }

    JsonDocument document;
    DeserializationError parse_error = deserializeJson(document, http_server->arg("plain"));
    if (parse_error)
    {
        send_error("Invalid JSON");
        return;
    }

    const char* submitted_ssid = document["ssid"].as<const char*>();
    if (!submitted_ssid || strlen(submitted_ssid) == 0)
    {
        send_error("SSID cannot be empty");
        return;
    }
    if (strlen(submitted_ssid) > WIFI_SSID_MAX_LEN)
    {
        send_error("SSID too long");
        return;
    }

    Preferences prefs;
    prefs.begin("wifi_cfg", true);
    int count = prefs.getInt("count", 0);
    String password;
    for (int i = 0; i < count; i++)
    {
        const String sk = "ssid_" + String(i);
        if (prefs.getString(sk.c_str(), "") == submitted_ssid)
        {
            const String pk = "pass_" + String(i);
            password = prefs.getString(pk.c_str(), "");
            break;
        }
    }
    prefs.end();

    do_wifi_connect(submitted_ssid, password.c_str(), password.length() > 0);
}

void handleDelete()
{
    if (!http_server || !http_server->hasArg("plain"))
    {
        send_error("No data");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http_server->arg("plain"));
    if (err)
    {
        send_error("Invalid JSON");
        return;
    }

    const char* ssid = doc["ssid"].as<const char*>();
    if (!ssid || strlen(ssid) == 0)
    {
        send_error("SSID required");
        return;
    }

    wifi_setup_delete_credentials(ssid);
    connection_status = ConnectionState::Idle;
    connection_attempt_started_at = 0;
    http_server->send(200, "application/json", "{\"success\":true}");
}

void handleStatus()
{
    if (!http_server)
    {
        return;
    }

    char response[64];
    snprintf(response, sizeof(response), "{\"status\":\"%s\"}", wifi_setup_status_str(connection_status));
    http_server->send(200, "application/json", response);
}

void handleApiSettings()
{
    if (!http_server)
    {
        return;
    }

    if (http_server->method() == HTTP_GET)
    {
        Preferences prefs;
        prefs.begin("price_cfg", true);
        String saved_url = prefs.getString("api_url", "");
        String saved_key = prefs.getString("api_key", "");
        prefs.end();

        // NOLINTNEXTLINE(misc-const-correctness) — operator[] returns mutating proxy
        JsonDocument doc;
        doc["api_url"] = saved_url;
        doc["api_key"] = saved_key;
        String json;
        serializeJson(doc, json);
        http_server->send(200, "application/json", json);
        return;
    }

    if (http_server->method() == HTTP_POST)
    {
        if (!http_server->hasArg("plain"))
        {
            send_error("No data");
            return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http_server->arg("plain"));
        if (err)
        {
            send_error("Invalid JSON");
            return;
        }

        const char* api_url = doc["api_url"].as<const char*>();
        const char* api_key = doc["api_key"].as<const char*>();
        const char* test_date = doc["test_date"].as<const char*>();

        // Sanitize URL: trim, strip trailing slash, prepend https:// if needed
        String cleaned_url;
        if (api_url && strlen(api_url) > 0)
        {
            cleaned_url = String(api_url);
            cleaned_url.trim();
            if (cleaned_url.length() > 0 && cleaned_url.c_str()[cleaned_url.length() - 1] == '/')
            {
                cleaned_url.remove(cleaned_url.length() - 1);
            }
            if (cleaned_url.length() > 0 && strstr(cleaned_url.c_str(), "://") == nullptr)
            {
                cleaned_url = String("https://") + cleaned_url;
            }
        }

        // Sanitize key: trim only
        String cleaned_key;
        if (api_key && strlen(api_key) > 0)
        {
            cleaned_key = String(api_key);
            cleaned_key.trim();
        }

        // Override cache with new values for test fetch
        price_fetch_override_settings(cleaned_url.length() > 0 ? cleaned_url.c_str() : nullptr,
                                      cleaned_key.length() > 0 ? cleaned_key.c_str() : nullptr);

        // Test fetch if we have connectivity + a test date
        if (test_date && strlen(test_date) > 0 && WiFi.status() == WL_CONNECTED)
        {
            PriceDay test_result;
            if (!price_fetch_by_date(test_result, test_date))
            {
                price_fetch_invalidate_url_cache();
                send_error("Testa pieprasījums neizdevās — pārbaudiet URL un atslēgu");
                return;
            }
        }

        // Persist
        Preferences prefs;
        prefs.begin("price_cfg", false);
        if (cleaned_url.length() > 0)
        {
            prefs.putString("api_url", cleaned_url.c_str());
        }
        else
        {
            prefs.remove("api_url");
        }
        if (cleaned_key.length() > 0)
        {
            prefs.putString("api_key", cleaned_key.c_str());
        }
        else
        {
            prefs.remove("api_key");
        }
        prefs.end();

        // Cache already has the new values from override_settings — no need
        // to invalidate. Keeping dirty=false ensures the next fetch uses them.
        http_server->send(200, "application/json", "{\"success\":true}");
        return;
    }

    http_server->send(405);
}

} // namespace
