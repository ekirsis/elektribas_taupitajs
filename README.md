# Automātisks enerģijas taupītājs ar cenas limita funkcionalitāti un displeju.

> C++17
> Arduino Nano ESP32‑S3 + ST7920 128×64 LCD — shows Latvian hourly
> electricity live spot price chart.

**For personal use only. Not for commercial redistribution.**

---

## Features

- **Price display** — current hour in large font, adjacent hours in small, live line chart (~10 h window) with "now" indicator
- **Captive‑portal WiFi setup** — first boot or lost connection: connect to the `Limitrozete` AP, configure WiFi from your phone browser — no serial cable needed
- **Multi‑network persistence** — saves up to 30 networks with deduplication, promotion, LRU eviction; passwords never leave the ESP
- **Price alert** — GPIO 2 goes HIGH when current price ≤ user‑set limit (0–99 c/kWh, adjustable via ± buttons). Turns OFF automatically when data is stale or unavailable
- **Animated feedback** — spinning globe animation during WiFi connect, AP mode connection, NTP sync, and price fetch — plays continuously with no freeze while the LCD shows progress messages
- **Dual‑core FreeRTOS** — WiFi monitoring on Core 0, DNS/HTTP/LCD on Core 1

---

## Hardware

### LCD — ST7920 128×64 via software SPI

U8g2 CS/RS framing mode — only CS, RS, RW are connected. Data and reset pins are unused.

| LCD Pin | ESP32 Pin | Signal |
|---------|-----------|--------|
| CS      | 13        | Chip Select |
| RS      | 11        | Register Select |
| RW      | 10        | Read/Write |
| VSS     | GND       | GND |
| VDD     | 3.3V      | VCC |
| V0      | Pot → GND | Contrast |
| LED-    | GND       | Backlight – |
| LED+    | 3.3V      | Backlight + |

### Buttons & alert output

Pin assignments defined in `include/pins.h`.

| Signal | GPIO | Wiring |
|--------|------|--------|
| Increment limit (+) | **5** | Momentary NO switch to GND, external 10 kΩ pull-up to 3.3 V |
| Decrement limit (−) | **4** | Same |
| AP mode             | **6** | Momentary NO switch to GND (`INPUT_PULLUP`, hardware ISR `FALLING` edge, 500 ms hold confirmation, ISR‑level `digitalRead` guard rejects relay‑spike false triggers) |
| Alert output        | **2** | HIGH when current price ≤ limit (e.g. LED + series resistor to GND) |

The **± buttons** adjust the price limit by ±1 c/kWh (range 0–99). Holding for 500 ms starts auto-repeat at 100 ms intervals. The limit is shown as a dotted line on the chart. Buttons are disabled when the price view is hidden (stale/missing data).

The **AP button** (GPIO 6) triggers captive‑portal WiFi setup at any time via `attachInterrupt(FALLING)`. A 500 ms hold confirms the press; the ISR verifies the pin is still LOW at 50 ms before setting the flag, rejecting inductive‑kickback false triggers from the alert relay. Early "Uzstādīšana..." feedback is shown immediately via `ap_button_pending()`; the consolidated `handle_ap_button()` in `wifi.cpp` manages both feedback and action with no code duplication.

---

## Quick start

```bash
cp include/example.secrets.h include/secrets.h
# edit include/secrets.h — set your PRICE_API_KEY

platformio run -t upload
platformio device monitor
```

The build automatically runs `scripts/build_webui.py` which minifies the web UI sources and generates `src/wifi_setup_page.h`.

---

## How it works

### Boot flow

1. **Init** — LCD, button pins (5, 4, 6) as `INPUT_PULLUP`, alert (2) LOW.
2. **Check saved credentials** — `wifi_has_saved_creds()` checks Preferences.
3. **No networks** → captive‑portal AP immediately (`Limitrozete` with random 8-char password shown on LCD). Once user configures WiFi and NTP syncs, setup proceeds.
4. **Networks exist** → tries NVS recall first (`WiFi.begin()` no‑args, 3 s), then saved networks (3 s each). During each attempt the globe animates with "Savienojas..." on the LCD. On connection → NTP sync (`time_sync_init()` + poll loop with animated "Sinhronizē laiku..." overlay): `configTime(0, 0, NTP)` in UTC, 500 ms settle, then up to 4 polls (1 × 500 ms, then up to 3 × 1 s). Uses separate `configTime` + `setenv("TZ")` + `tzset()` (not `configTzTime`) to avoid an ESP32‑S3 stall bug on Core 0.
5. **All fail** → shows "Pazudis WiFi!", enters non‑blocking recovery loop.
6. **Price fetch** — `ensure_prices` checks completeness per-day via `count_entries`, then fetches each missing date individually via `with_animation` (spinning globe). Incomplete API responses (<24 entries) are silently rejected — `merge_day` only accepts complete days. On empty data → "Cenas nav pieejamas!", retries every 2 min.
7. **Success** — current hour in large font, adjacent hours in small, plus chart.

### Main loop

- **WiFi recovery** — shows cached prices or "Pazudis WiFi!". Retries saved credentials every 60 s. AP button available at any time.
- **Runtime monitor** — checks `WiFi.status()` every iteration; drops to recovery path on disconnect.
- **Normal operation** — `ensure_prices(hour)` checks completeness per-date via `count_entries`. If today has <24 entries, fetches it. If hour ≥16 and tomorrow has <24 entries, attempts to fetch tomorrow (silently skips if API not yet ready; failed tomorrow-fetches are throttled to once per 30 min via `TOMORROW_RETRY_MS`). `merge_day` only accepts complete (24-entry) responses — partial API results are silently rejected, preserving existing data. Data older than yesterday pruned once/day after 2:00 (preserves yesterday hour 23 for the midnight cross-over view window). Buttons remain responsive (10 ms tick). Sleeps until next 5‑minute boundary; button presses wake immediately.
- **Stale data guard** — a global `data_ok` flag gates all price display and relay logic. When data is stale or unavailable: relay stays LOW, price view hidden, ± buttons disabled. The flag is set after every successful fetch that passes data validation. `can_draw_prices_for_dates` validates the view window (current − 1 to current + 4) strictly against wallclock date‑time — every view hour must have a matching `(date, hour)` entry. If any row is missing, the display clears and falls back to an error message. Negative view hours (before midnight) are matched against yesterday's date, hours ≥24 against tomorrow's date.

### Dual‑core (FreeRTOS)

```
Core 0:  wifi_monitor_task — watches WiFi.status(), calls time_sync()
Core 1:  Arduino loop()   — DNS catch‑all + HTTP server + LCD drawing
```

The monitor task is `xTaskCreatePinnedToCore` when the AP starts, `vTaskDelete` when it shuts down.

### Price data

Single source: `teapots.cloud/elektriba/api/prices` (hourly, EUR/MWh, VAT included). Display divides by 10 for c/kWh.

Fetched days are stored in a single flat `std::vector<PriceEntry>` vector. Each entry carries its own date string, so display functions search by `(date, hour)`. At midnight, the previous fetch's yesterday entries are retained so the view window (which includes the hour before current) can still display yesterday's hour 23. Data older than yesterday is pruned once/day after 2:00. `can_draw_prices_for_dates` validates the view window (current − 1 to current + 4) strictly against wallclock‑derived dates — the function takes a single `wallclock_today` parameter and internally computes yesterday/tomorrow via `offset_date_string`. Every view hour must have a matching entry; if validation fails the display falls back to an error message rather than silently dropping rows.

| Module | Role | Deps |
|--------|------|------|
| `include/price_api.h` | `PriceEntry` struct, `find_price()` data lookup by date+hour | none |
| `include/price_data_utils.h` | `merge_day()` replace-by-date (only accepts 24-entry complete days), `count_entries()` completeness check | `<algorithm>`, `<cstring>`, `<cstdio>` |
| `src/price_api_response_parser.h/.cpp` | Price API JSON → `PriceDay` | ArduinoJson |
| `src/price_fetch.cpp` | HTTP fetch, URL/key cache, sanitisation, override, NVS persistence | ArduinoJson, HTTPClient, Preferences |

The fetch layer (`src/price_fetch.cpp`) manages a runtime cache of URL and API key. If a custom URL is saved in NVS (`api_url`), it is used; otherwise the compile‑time default `PRICE_API_DEFAULT_URL` is used. If a custom key is saved (`api_key`), it overrides the compile‑time `PRICE_API_KEY` for the default URL; custom URLs use only the saved key (no fallback). The `X‑API‑Key` header is omitted entirely when key is empty. The returned `PriceDay` is merged into `price_data` via `merge_day` only if it contains a complete 24‑entry day — partial responses are silently rejected, preventing corruption of previously cached data.

---

### AP mode LCD feedback

During AP mode the LCD shows:
- **Idle** — AP instructions (SSID, password, captive‑portal URL)
- **Saved** — spinning globe with "Savienojas..." while the ESP attempts to connect to the submitted network
- **Connected** — spinning globe with "Sinhronizē laiku..." while NTP sync runs on Core 0
- **Synced** — "Laiks sinhronizēts!" for 7 s, then AP shutdown
- **Failed** — error reason for 8 s, then returns to AP instructions

The globe animation plays continuously through Saved and Connected states with no freeze, driven from the Core 1 main loop while Core 0 handles WiFi monitoring and NTP.

---

## Captive‑portal web UI

| Feature | Detail |
|---------|--------|
| Theme | Warm technical blue (`#4a7b9d`) |
| Language | Latvian |
| Network list | Pre‑scanned at AP start; live rescan via "Pārmeklēt no jauna" |
| Selecting | Tap anywhere on the network card (whole `<label>` is clickable) |
| Saved networks | Available ones inline with ✓ badge + delete; unavailable ones in dimmed "Saglabātie tīkli" section |
| Password | Optional — leave empty for open WiFi |
| Password pre‑fill | Not shown — saved passwords never leave the ESP; `/reconnect` reads them server-side |
| Password field | Hidden for saved networks; "Mainīt paroli" toggle (becomes "Atcelt" to undo) |
| Feedback | Polls `/status` every 1 s, real‑time progress |
| Completion | Form hides → "Uzstādīšana pabeigta!" with checkmark → auto‑reloads after 1 min |
| Timeout | Overall attempt fails after 15 s |
| Browser caching | Response served with ETag (SHA1 hash); browser gets `304 Not Modified` |
| AP button in portal | Ignored when no saved credentials exist |
| API settings | Custom price API URL and API key in "Iestatījumi" accordion; test‑fetch before save; clear to restore defaults. Saved key overrides `PRICE_API_KEY` for default URL; custom URL uses only saved key (no fallback if empty) |

### Build pipeline

| File | Role |
|------|------|
| `webui/index.html` | HTML skeleton with `__STYLE__` and `__SCRIPT__` markers |
| `webui/style.css` | Stylesheet |
| `webui/script.js` | Application logic |

Pre‑build script `scripts/build_webui.py` minifies CSS/JS/HTML, computes a SHA1 version hash, and generates `src/wifi_setup_page.h` (PROGMEM, **not tracked in git**).

---

## Project structure

```
├── include/           # Shared headers (price_api.h, price_data_utils.h, pins.h, secrets.h, example.secrets.h)
├── src/               # C++ source
│   ├── main.cpp       # Boot sequence & main loop
│   ├── wifi.cpp/h     # WiFi connection logic
│   ├── wifi_setup.cpp/h  # Captive‑portal server
│   ├── price_api_response_parser.cpp/h
│   ├── price_fetch.cpp/h
│   ├── lcd_display.cpp/h
│   ├── price_chart.cpp/h
│   ├── time_sync.cpp/h
│   ├── animations.cpp/h
│   └── wifi_setup_page.h  # Generated by build_webui.py
├── test/
│   ├── doctest.h
│   └── test_native/   # Host‑compiled tests
├── webui/             # Captive‑portal HTML/CSS/JS sources
├── .clang-tidy            # clang-tidy checks (includes misc-include-cleaner)
├── .clang-format          # C++ formatting rules
├── scripts/
│   ├── build_webui.py      # Minify & generate page header
│   ├── trim_u8g2_fonts.py  # Font subsetting for flash space
│   ├── run_format_check.sh # C++ formatting checks
│   ├── run_lint.sh         # All lint checks (clang-tidy, cppcheck, formatting)
│   └── run_tests.sh        # Format check + unit tests
└── platformio.ini
```

---

## Tests

```bash
./scripts/run_tests.sh
```

Runs the format check first, then unit tests.

| Test file | Cases | Coverage |
|-----------|-------|----------|
| `test_native/test_price_api.cpp` | 5 | API structure, parse, error handling (3 live, 2 fixture) |
| `test_native/test_price_data.cpp` | 9 | `find_price` data lookups (date+hour match, conversion factor, edge cases) |
| `test_native/test_lcd_display.cpp` | 13 | `can_draw_prices_for_dates` strict wallclock validation (cross-midnight, date disambiguation, yesterday crossover, edge hours, today-only rejection at late hours, partial tomorrow rejection) |
| `test_native/test_price_data_utils.cpp` | 15 | `merge_day` (replacement, isolation, idempotency, incomplete-data rejection), `count_entries` (empty, partial, full, multi-date), combined integration |
| `test_native/test_price_fetch.cpp` | 61 | URL cache, key fallback, sanitisation, override, cache invalidation |

**61 test cases, 250 assertions.** Non‑network tests always pass. Live tests fail when the upstream API is unreachable — acts as a format-change alert.

---

## Linting

```bash
scripts/run_lint.sh             # all checks
```

Or individually:

```bash
platformio check                # ESP32 env — clang-tidy
platformio check -e native_test # native env — cppcheck
scripts/run_format_check.sh     # C++ formatting check
```

- **ESP32** — clang‑tidy v15 via PlatformIO (checks in `.clang-tidy`). Warnings as errors. `misc-include-cleaner` (header‑cleanup) requires clang‑tidy 17+; unused includes are removed by hand.
- **Native** — cppcheck on host sources. False positives suppressed in `platformio.ini` under `check_flags`.
- **Formatting** — `.clang-format` defines the style (Allman braces, 4‑space indent, 110‑char limit). Run `scripts/run_format_check.sh` to verify, `clang-format -i <file>` to auto‑fix.

---

## Security

| Concern | Mitigation                                                                                           |
|---------|------------------------------------------------------------------------------------------------------|
| AP password | Random 8‑char (a-z0-9 excluding 0,o for better ux), generated at boot, shown on LCD, never persisted |
| Saved password exfiltration | Passwords never leave the ESP — `/saved` returns SSIDs only, `/reconnect` reads server-side          |
| Network list pollution | SSID max 32 chars, password max 63 chars, max 30 networks with LRU eviction                          |
| Eavesdropping | WPA2 encrypts AP ↔ client traffic                                                                    |

---

## License

All rights reserved. Not for commercial redistribution.
