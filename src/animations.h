#pragma once

#include <U8g2lib.h>
#include <cstdint>

enum class AnimID : uint8_t
{
    Globe = 0,
};

// Draw a single animation frame into the display buffer.
// The buffer is cleared first; the animation occupies the full 128x64 area.
// Does NOT call sendBuffer() — the caller must do that after adding text.
void anim_draw_frame(U8G2_ST7920_128X64_F_SW_SPI& display, AnimID anim, int frame);

// Advance the frame counter for the given animation (wraps as needed).
int anim_next_frame(AnimID anim, int frame);
