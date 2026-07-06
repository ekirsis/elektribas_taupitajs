#pragma once

#include <U8g2lib.h>
#include <vector>
#include "price_api.h"

void draw_price_chart(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, const std::vector<PriceEntry>& data,
                      int current_hour, int current_minute, int chart_y_top, int price_limit);
