#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

namespace BootMenu {

    enum class Choice {
        Merlin,
        Radar
    };

    // Show boot menu (2 big buttons: Merlin / Flight Radar).
    // Blocks until the user taps one. Returns the choice.
    Choice run(TFT_eSPI& tft);
}
