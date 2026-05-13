#include <algorithm>
#include <cmath>
#include "price_chart.h"
#include "time_sync.h"

namespace {
struct HourChartPoint
{
    int hour; // 0–47 (24+ = tomorrow)
    float cents; // price in c/kWh
};

constexpr int CHART_Y_BOTTOM = 63;
constexpr int CHART_RIGHT = 106;

int round_to_int(float v)
{
    return static_cast<int>(lroundf(v));
}

std::vector<HourChartPoint> collect_sorted_points(const std::vector<PriceEntry>& data)
{
    char yesterday_str[11];
    char today_str[11];
    char tomorrow_str[11];
    format_date_string(yesterday_str, sizeof(yesterday_str), -1);
    get_current_date(today_str, sizeof(today_str));
    format_date_string(tomorrow_str, sizeof(tomorrow_str), 1);

    std::vector<HourChartPoint> points;
    for (const auto& e : data)
    {
        int view_hour;
        if (strcmp(e.date_string, today_str) == 0)
            view_hour = e.hour;
        else if (strcmp(e.date_string, tomorrow_str) == 0)
            view_hour = e.hour + 24;
        else if (strcmp(e.date_string, yesterday_str) == 0)
            view_hour = e.hour - 24;
        else
            continue;
        points.push_back({view_hour, e.eur_mwh * 0.1f});
    }

    if (points.size() < 2)
    {
        return points;
    }

    std::sort(points.begin(), points.end(),
              [](const HourChartPoint& a, const HourChartPoint& b) { return a.hour < b.hour; });

    return points;
}

int find_index_by_hour(const std::vector<HourChartPoint>& points, int hour)
{
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (points[i].hour == hour)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int price_to_y(float cents, float min_price, float inv_range, int chart_y_range)
{
    float const norm = (cents - min_price) * inv_range;
    return CHART_Y_BOTTOM - round_to_int(norm * static_cast<float>(chart_y_range));
}

int clamp_dot_y(int y, int chart_y_top)
{
    if (y < chart_y_top + 5)
    {
        return chart_y_top + 5;
    }
    if (y > CHART_Y_BOTTOM - 5)
    {
        return CHART_Y_BOTTOM - 5;
    }
    return y;
}

int clamp_dot_x(int x)
{
    if (x < 1)
    {
        return 1;
    }
    if (x > CHART_RIGHT - 1)
    {
        return CHART_RIGHT - 1;
    }
    return x;
}

bool interpolated_dot_pos(const std::vector<HourChartPoint>& points, int current_hour, int current_minute,
                          float min_price, float inv_range, int chart_y_range, int chart_y_top, int n,
                          int* out_x, int* out_y)
{
    float const frac = static_cast<float>(current_minute) / 60.0F;

    int cur = find_index_by_hour(points, current_hour);
    int nxt = find_index_by_hour(points, current_hour + 1);

    int x;
    int y;
    if (cur >= 0 && nxt >= 0)
    {
        int const x_cur = cur * CHART_RIGHT / (n - 1);
        int const x_nxt = nxt * CHART_RIGHT / (n - 1);
        float y_cur = price_to_y(points[cur].cents, min_price, inv_range, chart_y_range);
        float y_nxt = price_to_y(points[nxt].cents, min_price, inv_range, chart_y_range);
        x = round_to_int(static_cast<float>(x_cur) + frac * static_cast<float>(x_nxt - x_cur));
        y = round_to_int(y_cur + frac * (y_nxt - y_cur));
    }
    else if (cur >= 0)
    {
        x = cur * CHART_RIGHT / (n - 1);
        y = price_to_y(points[cur].cents, min_price, inv_range, chart_y_range);
    }
    else
    {
        return false;
    }

    *out_x = clamp_dot_x(x);
    *out_y = clamp_dot_y(y, chart_y_top);
    return true;
}
} // namespace

void draw_price_chart(U8G2_ST7920_128X64_F_SW_SPI& display, const std::vector<PriceEntry>& data,
                      int current_hour, int current_minute, int chart_y_top, int price_limit)
{
    auto points = collect_sorted_points(data);
    if (points.size() < 2)
    {
        return;
    }

    int const min_hour = current_hour - 1;
    int const max_hour = current_hour + 8;
    points.erase(std::remove_if(points.begin(), points.end(),
                                [min_hour, max_hour](const HourChartPoint& p) {
                                    return p.hour < min_hour || p.hour > max_hour;
                                }),
                 points.end());
    if (points.size() < 2)
    {
        return;
    }

    int chart_y_range = CHART_Y_BOTTOM - chart_y_top;
    int n = static_cast<int>(points.size());

    float min_price = points[0].cents;
    float max_price = points[0].cents;
    for (const auto& pt : points)
    {
        if (pt.cents < min_price)
            min_price = pt.cents;
        if (pt.cents > max_price)
            max_price = pt.cents;
    }

    float const range = max_price - min_price;

    if (range < 0.001F)
    {
        int const flat_y = (chart_y_top + CHART_Y_BOTTOM) / 2;
        display.drawLine(0, flat_y, CHART_RIGHT, flat_y);

        for (int i = 0; i < n; ++i)
        {
            int const x = i * CHART_RIGHT / (n - 1);
            display.drawPixel(x, flat_y - 2);
            display.drawPixel(x, flat_y - 1);
        }

        int dx;
        int dy;
        if (interpolated_dot_pos(points, current_hour, current_minute, 0, 1.0F, 0, chart_y_top, n, &dx, &dy))
        {
            display.drawRFrame(dx - 1, chart_y_top - 3, 4, CHART_Y_BOTTOM - chart_y_top + 4, 1);
        }
        return;
    }

    float inv_range = 1.0F / range;

    int cur_seg = find_index_by_hour(points, current_hour);

    for (int i = 0; i < n - 1; ++i)
    {
        int const x0 = i * CHART_RIGHT / (n - 1);
        int const x1 = (i + 1) * CHART_RIGHT / (n - 1);
        int y0 = price_to_y(points[i].cents, min_price, inv_range, chart_y_range);
        int y1 = price_to_y(points[i + 1].cents, min_price, inv_range, chart_y_range);
        display.drawLine(x0, y0, x1, y0);
        display.drawLine(x1, y0, x1, y1);
        display.drawPixel(x1, y0 - 2);
        display.drawPixel(x1, y0 - 1);
        if (i == cur_seg)
        {
            display.drawLine(x0, y0 - 1, x1, y0 - 1);
        }
    }

    for (int i = 0; i < n; ++i)
    {
        int const x = i * CHART_RIGHT / (n - 1);
        int y = price_to_y(points[i].cents, min_price, inv_range, chart_y_range);
        display.drawPixel(x, y - 2);
        display.drawPixel(x, y - 1);
    }

    int dx;
    int dy;
    if (interpolated_dot_pos(points, current_hour, current_minute, min_price, inv_range, chart_y_range,
                             chart_y_top, n, &dx, &dy))
    {
        display.drawRFrame(dx - 1, chart_y_top - 3, 4, CHART_Y_BOTTOM - chart_y_top + 4, 1);
    }

    if (price_limit > 0)
    {
        const int limit_y = price_to_y(static_cast<float>(price_limit), min_price, inv_range, chart_y_range);
        if (limit_y >= chart_y_top && limit_y <= CHART_Y_BOTTOM)
        {
            for (int x = 2; x < CHART_RIGHT - 1; x += 4)
            {
                display.drawPixel(x, limit_y);
            }
        }
    }
}
