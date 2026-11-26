#pragma once
#include <Arduino.h>
#include "config.h"

namespace MerlinStatus {

    enum class State {
        Idle,
        Thinking,
        Busy,
        TalkingToKen,
        Error
    };

    enum class LightState {
        Off,      // dark / unknown
        Green,    // good
        Red       // bad
    };

    enum class AppMode {
        Merlin,
        Radar
    };

    void begin();
    void update();

    // Time (NTP, Europe/London)
    bool timeValid();
    String timeString();  // "HH:MM" 24h
    void syncTime();

    State state();
    const char* message();
    bool hasSpeech();
    const char* version();
    bool emergencyActive();
    bool isUnreachable();
    String activeEmergencyMsg();
    AppMode appMode();
    void setAppMode(AppMode mode);
    bool emergencyQueued();

    // Ken / trading lights
    LightState kenLight();
    LightState tradeLight();
    void setKenLight(LightState s);
    void setTradeLight(LightState s);
    uint32_t kenLastSeenMs();
    uint32_t tradeLastSeenMs();

    void setEmergency(const char* msg);
    void clearEmergency();

    uint16_t stateColor(State s);
    uint8_t stateForColor(State s);
}
