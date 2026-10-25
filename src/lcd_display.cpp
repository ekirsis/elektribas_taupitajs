#include <Arduino.h>
#include <esp_task_wdt.h>
#include <ctime>
#include "lcd_display.h"
#include "price_chart.h"
#include "time_utils.h"
#include "pins.h"

namespace {

void draw_centered_utf8_line(U8G2_ST7920_128X64_F_SW_SPI& display, const char* text, int y)
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

void lcd_draw_message_impl(U8G2_ST7920_128X64_F_SW_SPI& display, const char* const* msg, size_t n)
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

void lcd_overlay_message(U8G2_ST7920_128X64_F_SW_SPI& display, const char* const* msg, size_t n)
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

bool with_animation(U8G2_ST7920_128X64_F_SW_SPI& display, AnimID anim, char const* msg,
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

void draw_price_row(U8G2_ST7920_128X64_F_SW_SPI& display, int y_baseline, const char* hour_range,
                    float price_cents, bool is_main)
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
        // The single-label case (autumn's first 03:00 slot) is handled by
        // time_utils::format_hour_range.
        snprintf(line_buffer, sizeof(line_buffer), "%s  %d.%02d c/kWh", hour_range, whole_part, frac_part);
        display.setFont(u8g2_font_helvR08_tr);
    }
    display.setFontPosBaseline();
    display.drawUTF8(is_main ? 1 : 0, y_baseline, line_buffer);
}

} // namespace

void lcd_draw_prices(U8G2_ST7920_128X64_F_SW_SPI& display, const std::vector<PriceEntry>& data, int price_limit)
{
    time_t const now = time(nullptr);

    bool main_found = false; // NOLINT: mutated by find_price_at via bool&
    const float main_price_cents = find_price_at(data, static_cast<int64_t>(now), main_found);
    if (!main_found)
    {
        display.clearBuffer();
        display.sendBuffer();
        digitalWrite(PIN_ALERT, LOW);
        return;
    }

    static constexpr int ROW_Y_BASELINE[4] = {8, 24, 35, 45};
    static constexpr int HOUR_OFFSET[4] = {-1, 0, 1, 2};

    bool row_found[4] = {};
    float row_prices_cents[4] = {};
    char row_hour_range[4][16] = {};

    for (int i = 0; i < 4; ++i)
    {
        time_t const row_time = now + static_cast<time_t>(HOUR_OFFSET[i]) * 3600;
        bool row_hit = false; // NOLINT: mutated by find_price_at via bool&
        float const row_cents = find_price_at(data, static_cast<int64_t>(row_time), row_hit);
        row_found[i] = row_hit;
        row_prices_cents[i] = row_cents;
        time_utils::format_hour_range(static_cast<int64_t>(row_time), static_cast<int64_t>(row_time) + 3600,
                                      row_hour_range[i], sizeof(row_hour_range[i]));
    }

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
            draw_price_row(display, ROW_Y_BASELINE[i], "", main_price_cents, true);
        }
        else
        {
            draw_price_row(display, ROW_Y_BASELINE[i], row_hour_range[i], row_prices_cents[i], false);
        }
    }

    int const chart_y_top = ROW_Y_BASELINE[3] + 5;
    draw_price_chart(display, data, now, chart_y_top, price_limit);

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
