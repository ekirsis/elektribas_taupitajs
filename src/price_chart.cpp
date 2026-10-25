#include <algorithm>
#include <cmath>
#include <cstdint>
#include "price_chart.h"

namespace {
struct HourChartPoint
{
    int64_t start_utc;
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
    std::vector<HourChartPoint> points;
    points.reserve(data.size());
    for (const auto& e : data)
    {
        points.push_back({e.start_utc, e.eur_mwh * 0.1f});
    }

    std::sort(points.begin(), points.end(),
              [](const HourChartPoint& a, const HourChartPoint& b) { return a.start_utc < b.start_utc; });
    points.erase(std::unique(points.begin(), points.end(),
                             [](const HourChartPoint& a, const HourChartPoint& b) {
                                 return a.start_utc == b.start_utc;
                             }),
                 points.end());

    return points;
}

// Index of the hourly point covering `at_utc`, or -1.  Intervals are
// half-open so the duplicated 03:00 slot in autumn resolves to the correct
// absolute hour.
int find_index_at(const std::vector<HourChartPoint>& points, int64_t at_utc)
{
    for (size_t i = 0; i < points.size(); ++i)
    {
        if (points[i].start_utc <= at_utc && at_utc < points[i].start_utc + 3600)
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

bool interpolated_dot_pos(const std::vector<HourChartPoint>& points, int64_t now_utc, float min_price,
                          float inv_range, int chart_y_range, int chart_y_top, int n, int* out_x, int* out_y)
{
    int const cur = find_index_at(points, now_utc);
    if (cur < 0)
    {
        return false;
    }

    int const nxt = cur + 1;
    float const frac = static_cast<float>(now_utc - points[cur].start_utc) / 3600.0F;

    int x;
    int y;
    if (nxt < n)
    {
        int const x_cur = cur * CHART_RIGHT / (n - 1);
        int const x_nxt = nxt * CHART_RIGHT / (n - 1);
        float const y_cur = price_to_y(points[cur].cents, min_price, inv_range, chart_y_range);
        float const y_nxt = price_to_y(points[nxt].cents, min_price, inv_range, chart_y_range);
        x = round_to_int(static_cast<float>(x_cur) + frac * static_cast<float>(x_nxt - x_cur));
        y = round_to_int(y_cur + frac * (y_nxt - y_cur));
    }
    else
    {
        x = cur * CHART_RIGHT / (n - 1);
        y = price_to_y(points[cur].cents, min_price, inv_range, chart_y_range);
    }

    *out_x = clamp_dot_x(x);
    *out_y = clamp_dot_y(y, chart_y_top);
    return true;
}
} // namespace

void draw_price_chart(U8G2_ST7920_128X64_F_SW_SPI& display, const std::vector<PriceEntry>& data, time_t now,
                      int chart_y_top, int price_limit)
{
    auto all_points = collect_sorted_points(data);
    if (all_points.size() < 2)
    {
        return;
    }

    auto const now_utc = static_cast<int64_t>(now);
    int const current_index = find_index_at(all_points, now_utc);
    if (current_index < 0)
    {
        return;
    }

    // Keep the same ten-hour window as before (current - 1 through
    // current + 8) but select it by absolute time.  On a DST day the
    // window may contain a duplicated or missing wall-clock hour.
    int const first = std::max(0, current_index - 1);
    int const last = std::min(static_cast<int>(all_points.size()) - 1, current_index + 8);
    std::vector<HourChartPoint> points(all_points.begin() + first, all_points.begin() + last + 1);

    int chart_y_range = CHART_Y_BOTTOM - chart_y_top;
    int n = static_cast<int>(points.size());

    float min_price = points[0].cents;
    // NOLINTNEXTLINE(misc-const-correctness) — false positive: the min/max loop below assigns it
    float max_price = min_price;
    for (const auto& pt : points)
    {
        if (pt.cents < min_price)
        {
            min_price = pt.cents;
        }
        if (pt.cents > max_price)
        {
            max_price = pt.cents;
        }
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
        if (interpolated_dot_pos(points, now_utc, 0, 1.0F, 0, chart_y_top, n, &dx, &dy))
        {
            display.drawRFrame(dx - 1, chart_y_top - 3, 4, CHART_Y_BOTTOM - chart_y_top + 4, 1);
        }
        return;
    }

    float inv_range = 1.0F / range;

    int const cur_seg = find_index_at(points, now_utc);

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
    if (interpolated_dot_pos(points, now_utc, min_price, inv_range, chart_y_range, chart_y_top, n, &dx, &dy))
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
