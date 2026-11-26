#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "merlin_status.h"

namespace MerlinScreen {

    // Animated owl + status display. Call update() from loop().
    // The screen draws the owl, status text, time, and emergency flashes.
    void begin(TFT_eSPI& tft);
    void update(TFT_eSPI& tft);

    // Draw just the owl (for reuse in other screens)
    void drawOwl(TFT_eSPI& tft, int16_t cx, int16_t cy, MerlinStatus::State state);
}
