#include <Arduino.h>
#include <esp_task_wdt.h>
#include "lcd_display.h"
#include "price_chart.h"
#include "time_sync.h"
#include "pins.h"

namespace {

void draw_centered_utf8_line(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, const char* text, int y)
{
    int w = display.getUTF8Width(text);
    if (w > 124)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.24s...", text);
        w = display.getUTF8Width(buf);
        display.drawUTF8((128 - w) / 2, y, buf);
    }
    else
    {
        display.drawUTF8((128 - w) / 2, y, text);
    }
}

} // namespace

void lcd_draw_message_impl(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, const char* const* msg, size_t n)
{
    display.clearBuffer();
    display.setFont(u8g2_font_helvR08_te);

    int const start_y = (64 - static_cast<int>(n) * 12) / 2 + 8;
    for (size_t i = 0; i < n; ++i)
    {
        draw_centered_utf8_line(display, msg[i], start_y + static_cast<int>(i) * 12);
    }
    display.sendBuffer();
}

void lcd_overlay_message(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, const char* const* msg, size_t n)
{
    display.setFont(u8g2_font_helvR08_te);

    int band_top = (64 - static_cast<int>(n) * 12) / 2 + 8 - 9;
    int band_h = (static_cast<int>(n) - 1) * 12 + 13;
    if (band_top < 0)
    {
        band_top = 0;
    }
    if (band_top + band_h > 64)
    {
        band_h = 64 - band_top;
    }
    display.setDrawColor(0);
    display.drawBox(0, band_top, 128, band_h);
    display.setDrawColor(1);

    int const start_y = (64 - static_cast<int>(n) * 12) / 2 + 8;
    for (size_t i = 0; i < n; ++i)
    {
        draw_centered_utf8_line(display, msg[i], start_y + static_cast<int>(i) * 12);
    }
    display.sendBuffer();
}

namespace {

struct WorkContext
{
    bool (*fn)(void*);
    void* arg;
    volatile bool done;
    volatile bool result;
};

void work_task(void* p)
{
    auto* ctx = static_cast<WorkContext*>(p);
    ctx->result = ctx->fn(ctx->arg);
    ctx->done = true;
    vTaskDelete(nullptr);
}

} // namespace

bool with_animation(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, AnimID anim, char const* msg,
                    bool (*work_fn)(void*), void* arg)
{
    WorkContext ctx{work_fn, arg, false, false};
    xTaskCreatePinnedToCore(work_task, "work", 8192, &ctx, 1, nullptr, 0);

    int frame = 0;
    while (!ctx.done)
    {
        lcd_draw_animated_message(display, anim, frame, msg);
        frame = anim_next_frame(anim, frame);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_task_wdt_reset();
    }
    return ctx.result;
}

namespace {

void draw_price_row(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, int y_baseline, int hour, float price_cents,
                    bool is_main)
{
    int const whole_part = static_cast<int>(price_cents);
    int frac_part = static_cast<int>(lroundf((price_cents - static_cast<float>(whole_part)) * 100.0F));
    if (frac_part < 0)
    {
        frac_part = -frac_part;
    }
    if (frac_part >= 100)
    {
        frac_part = 99;
    }

    char line_buffer[48];
    if (is_main)
    {
        snprintf(line_buffer, sizeof(line_buffer), "%d.%02d c/kWh", whole_part, frac_part);
        display.setFont(u8g2_font_10x20_me);
    }
    else
    {
        snprintf(line_buffer, sizeof(line_buffer), "%02d:00-%02d:00  %d.%02d c/kWh", hour, (hour + 1) % 24,
                 whole_part, frac_part);
        display.setFont(u8g2_font_helvR08_tr);
    }
    display.setFontPosBaseline();
    display.drawUTF8(is_main ? 1 : 0, y_baseline, line_buffer);
}

float get_row_price(const std::vector<PriceEntry>& data, int current_hour, int offset, const char* today_str,
                    const char* tomorrow_str, const char* yesterday_str, bool& found)
{
    const int view_hour = current_hour + offset;
    const char* date;
    uint8_t needle;
    if (!resolve_view_hour(view_hour, yesterday_str, today_str, tomorrow_str, date, needle))
    {
        found = false;
        return 0.0F;
    }
    return find_price(data, date, needle, found);
}

} // namespace

void lcd_draw_prices(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, const std::vector<PriceEntry>& data, int price_limit)
{
    const time_t now = time(nullptr);
    tm local_time{};
    localtime_r(&now, &local_time);
    const int current_hour = local_time.tm_hour;
    const int current_minute = local_time.tm_min;

    char yesterday_str[11];
    char today_str[11];
    char tomorrow_str[11];
    format_date_string(yesterday_str, sizeof(yesterday_str), -1);
    get_current_date(today_str, sizeof(today_str));
    format_date_string(tomorrow_str, sizeof(tomorrow_str), 1);

    bool main_found_tmp = false; // NOLINT: mutated by find_price via bool&
    const float main_price_cents =
        find_price(data, today_str, static_cast<uint8_t>(current_hour), main_found_tmp);
    const bool main_found = main_found_tmp;
    if (!main_found)
    {
        display.clearBuffer();
        display.sendBuffer();
        digitalWrite(PIN_ALERT, LOW);
        return;
    }

    static constexpr int ROW_Y_BASELINE[4] = {8, 24, 35, 45};
    static constexpr int HOUR_OFFSET[4] = {-1, 0, 1, 2};

    bool row_found_tmp[4] = {}; // NOLINT: mutated by get_row_price via bool&
    const float row_prices_cents[4] = {
        get_row_price(data, current_hour, HOUR_OFFSET[0], today_str, tomorrow_str, yesterday_str,
                      row_found_tmp[0]),
        get_row_price(data, current_hour, HOUR_OFFSET[1], today_str, tomorrow_str, yesterday_str,
                      row_found_tmp[1]),
        get_row_price(data, current_hour, HOUR_OFFSET[2], today_str, tomorrow_str, yesterday_str,
                      row_found_tmp[2]),
        get_row_price(data, current_hour, HOUR_OFFSET[3], today_str, tomorrow_str, yesterday_str,
                      row_found_tmp[3]),
    };
    const bool (&row_found)[4] = row_found_tmp;

    display.clearBuffer();

    for (int i : {0, 1, 2, 3})
    {
        if (!row_found[i])
        {
            display.clearBuffer();
            display.sendBuffer();
            digitalWrite(PIN_ALERT, LOW);
            return;
        }

        if (i == 1)
        {
            draw_price_row(display, ROW_Y_BASELINE[i], current_hour, main_price_cents, true);
        }
        else
        {
            int label_hour = current_hour + HOUR_OFFSET[i];
            if (label_hour >= 24)
            {
                label_hour -= 24;
            }
            else if (label_hour < 0)
            {
                label_hour += 24;
            }
            draw_price_row(display, ROW_Y_BASELINE[i], label_hour, row_prices_cents[i], false);
        }
    }

    int const chart_y_top = ROW_Y_BASELINE[3] + 5;
    draw_price_chart(display, data, current_hour, current_minute, chart_y_top, price_limit);

    char limit_buf[4];
    snprintf(limit_buf, sizeof(limit_buf), "%2d", price_limit);
    display.setFont(u8g2_font_4x6_tr);
    display.setFontPosBaseline();
    display.drawUTF8(108, 53, "Limit");
    display.setFont(u8g2_font_helvR08_tr);
    display.drawUTF8(112, 64, limit_buf);

    display.sendBuffer();

    if (price_limit > 0 && main_price_cents <= static_cast<float>(price_limit))
    {
        digitalWrite(PIN_ALERT, HIGH);
    }
    else
    {
        digitalWrite(PIN_ALERT, LOW);
    }
}
