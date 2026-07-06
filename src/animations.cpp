#include "animations.h"
#include <cmath>

// ── Continent definition ──────────────────────────────────────────────────
// Lat/lon pairs in degrees (lon −180…180, lat −90…90).
// Points closer than ~8° merge into visible blobs at R=32 scale.

namespace {

// clang-format off
int const CONTINENTS[] = {
    // Eurasia
    55, -5, 48, 2, 45, 10, 55, 15, 52, 22, 40, 0, 47, 30, 55, 32, 50, 38, 58, 42, 54, 52, 48, 58, 42, 52, 35, 48, 32,
    42, 65, 70, 57, 80, 62, 95, 50, 108, 40, 118, 34, 132, 28, 78, 18, 100,
    // Africa
    35, 0, 28, 5, 18, 8, 12, 12, 0, 18, 0, 28, -10, 22, -22, 26, -30, 28, 10, 38, 26, 32, 15, -2,
    // Americas
    55, -120, 60, -108, 52, -95, 47, -88, 42, -75, 35, -95, 30, -85, 25, -80, 22, -90, 14, -86, 2, -55, -5, -60, -17,
    -52, -27, -58, -32, -65, 58, -132, 62, -125,
    // Australia
    -25, 135, -30, 145, -35, 140, -20, 120,
};
// clang-format on

int const CONTINENT_COUNT = sizeof(CONTINENTS) / (2 * sizeof(int));

float const R = 31.0F;
float const PI_F = 3.14159265358979323846F;

// ── Latitude trig table (−80° to 80°, step 5°) ───────────────────────────
// Precomputed once to avoid repeated sinf/cosf calls in meridian + lat lines.
int constexpr LAT_MIN = -80;
int constexpr LAT_STEP = 5;
int constexpr LAT_N = 33;

struct LatTrig
{
    float sin;
    float cos;
};

LatTrig const* get_lat_trig()
{
    static LatTrig table[LAT_N];
    static bool ready = false;
    if (!ready)
    {
        for (int i = 0; i < LAT_N; ++i)
        {
            float const r = static_cast<float>(LAT_MIN + i * LAT_STEP) * PI_F / 180.0F;
            table[i].sin = sinf(r);
            table[i].cos = cosf(r);
        }
        ready = true;
    }
    return table;
}

void draw_globe_frame(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, int frame)
{
    display.clearBuffer();

    int const cx = 64;
    int const cy = 32;

    // ── Outline ───────────────────────────────────────────────────────────
    display.drawCircle(cx, cy, static_cast<uint8_t>(R));

    // ── Rotation ──────────────────────────────────────────────────────────
    float const alpha = static_cast<float>(frame) * 22.5F * PI_F / 180.0F;

    auto const* lat = get_lat_trig();

    // ── Latitude lines at φ = −60°, −30°, 0°, 30°, 60° ──────────────────
    static int const LAT_DEG[] = {-60, -30, 0, 30, 60};
    for (int const ld : LAT_DEG)
    {
        int const idx = (ld - LAT_MIN) / LAT_STEP;
        int const y = cy - static_cast<int>(roundf(R * lat[idx].sin));
        int const hw = static_cast<int>(roundf(R * lat[idx].cos));
        display.drawHLine(cx - hw, y, hw * 2);
    }

    // ── Longitude lines − 12 meridians, visible when cos(θ+α) > 0 ──────
    for (int mi = 0; mi < 12; ++mi)
    {
        float const theta_rot = static_cast<float>(mi * 30) * PI_F / 180.0F + alpha;
        float const cos_tr = cosf(theta_rot);
        if (cos_tr <= 0.0F)
        {
            continue;
        }
        float const sin_tr = sinf(theta_rot);

        int prev_x = -1;
        int prev_y = -1;
        for (int li = 0; li < LAT_N; ++li)
        {
            float const R_cos = R * lat[li].cos;
            int const x = cx + static_cast<int>(roundf(R_cos * sin_tr));
            int const y = cy - static_cast<int>(roundf(R * lat[li].sin));

            if (prev_x >= 0)
            {
                display.drawLine(prev_x, prev_y, x, y);
            }
            prev_x = x;
            prev_y = y;
        }
    }

    // ── Continent pixels (front-face only, cos(θ+α) > 0) ────────────────
    for (int ci = 0; ci < CONTINENT_COUNT; ++ci)
    {
        size_t const ci2 = static_cast<size_t>(ci) * 2;
        auto const lat_f = static_cast<float>(CONTINENTS[ci2]);
        auto const lon_f = static_cast<float>(CONTINENTS[ci2 + 1]);

        float const lat_r = lat_f * PI_F / 180.0F;
        float const lon_r = lon_f * PI_F / 180.0F;
        float const lon_rot = lon_r + alpha;

        float const cos_lon = cosf(lon_rot);
        if (cos_lon <= 0.0F)
        {
            continue;
        }

        float const sin_lon = sinf(lon_rot);
        float const cos_lat = cosf(lat_r);
        float const sin_lat = sinf(lat_r);

        int const sx = cx + static_cast<int>(roundf(R * cos_lat * sin_lon));
        int const sy = cy - static_cast<int>(roundf(R * sin_lat));

        if (sx < 1 || sx >= 127 || sy < 1 || sy >= 63)
        {
            continue;
        }
        display.drawBox(sx - 1, sy - 1, 3, 3);
    }
}

} // namespace

void anim_draw_frame(U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI& display, AnimID anim, int frame)
{
    (void)anim;
    draw_globe_frame(display, frame);
}

int anim_next_frame(AnimID anim, int frame)
{
    (void)anim;
    return (frame + 1) % 16;
}
