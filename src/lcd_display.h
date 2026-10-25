#pragma once

#include <U8g2lib.h>
#include <type_traits>
#include <vector>
#include "animations.h"
#include "price_api.h"

template<bool... Bs>
struct all_true;

template<>
struct all_true<> : std::true_type
{};

template<bool B, bool... Bs>
struct all_true<B, Bs...> : std::conditional<B, all_true<Bs...>, std::false_type>::type
{};

void lcd_draw_message_impl(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, const char* const* msg, size_t n);

template<typename... Lines>
void lcd_draw_message(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, const Lines&... lines)
{
    static_assert(sizeof...(Lines) > 0 && sizeof...(Lines) <= 5, "lcd_draw_message needs 1–5 lines");
    static_assert(all_true<(!std::is_array<Lines>::value || sizeof(Lines) <= 29)...>::value,
                  "A line exceeds ~28 characters and won't fit the 128px display");

    const char* const msg[] = {lines...};
    lcd_draw_message_impl(display, msg, sizeof...(Lines));
}

void lcd_overlay_message(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, const char* const* msg, size_t n);

template<typename... Lines>
void lcd_draw_animated_message(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, AnimID anim, int frame,
                               const Lines&... lines)
{
    static_assert(sizeof...(Lines) > 0 && sizeof...(Lines) <= 5, "lcd_draw_animated_message needs 1–5 lines");
    static_assert(all_true<(!std::is_array<Lines>::value || sizeof(Lines) <= 29)...>::value,
                  "A line exceeds ~28 characters and won't fit the 128px display");

    anim_draw_frame(display, anim, frame);
    const char* const msg[] = {lines...};
    lcd_overlay_message(display, msg, sizeof...(Lines));
}

void lcd_draw_prices(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, const std::vector<PriceEntry>& data,
                     int price_limit);

/// Run `work_fn(arg)` in a background task (Core 0) while animating the LCD on Core 1.
/// The animation loops frames of `anim` with the one-liner `msg` until the work completes.
/// Returns the result from `work_fn`.
bool with_animation(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, AnimID anim, char const* msg,
                    bool (*work_fn)(void*), void* arg);
