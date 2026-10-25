#pragma once

#include <U8g2lib.h>
#include <ctime>
#include <vector>
#include "price_api.h"

void draw_price_chart(U8G2_ST7920_128X64_F_SW_SPI& display, const std::vector<PriceEntry>& data, time_t now,
                      int chart_y_top, int price_limit);
