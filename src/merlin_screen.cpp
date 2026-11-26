#include "merlin_screen.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include "merlin_status.h"
#include "owl_img.h"
#include "ui_theme.h"
#include "config.h"

namespace MerlinScreen {

namespace {

    // Bumped 1.3 -> 1.4 with the signed footer, so a flash is provable on screen.
    // Single source of truth for the version string: MerlinStatus::version().
#define MERLIN_DASH_VERSION MerlinStatus::version()

    TFT_eSPI* _tft = nullptr;
    MerlinStatus::State _lastDrawnState = (MerlinStatus::State)255;
    MerlinStatus::LightState _lastDrawnKen = (MerlinStatus::LightState)255;
    MerlinStatus::LightState _lastDrawnTrade = (MerlinStatus::LightState)255;
    String _lastDrawnClock = "";
    String _lastDrawnMsg = "";
    const uint16_t BG = 0x0000;  // BLACK

    // ============================================================
    // OWL — centered at (120, 100), ~90px tall
    // ============================================================

    void drawOwlBase(TFT_eSPI* tft, uint16_t body, uint16_t belly, uint16_t head) {
        int cx = 160, cy = 100;  // Center X=160 (320/2), Y=100 for landscape

        // Wings
        tft->fillTriangle(cx-32, cy+18, cx-50, cy+55, cx-14, cy+48, body);
        tft->fillTriangle(cx+32, cy+18, cx+50, cy+55, cx+14, cy+48, body);

        // Body
        tft->fillEllipse(cx, cy+26, 44, 52, body);
        tft->fillEllipse(cx, cy+28, 34, 42, belly);

        // Head
        tft->fillCircle(cx, cy-2, 36, head);
        tft->fillCircle(cx, cy-2, 32, 0xDCC9A0);

        // Ear tufts
        tft->fillTriangle(cx-26, cy-30, cx-38, cy-52, cx-12, cy-36, body);
        tft->fillTriangle(cx+26, cy-30, cx+38, cy-52, cx+12, cy-36, body);
    }

    void drawEyes(TFT_eSPI* tft, uint16_t eyeColor, bool happy, bool worried) {
        int cx = 160, cy = 100;  // Match base

        // Eye patches
        tft->fillCircle(cx-15, cy-6, 14, 0x0D0D1A);
        tft->fillCircle(cx+15, cy-6, 14, 0x0D0D1A);

        if (happy) {
            // Curved happy eyes
            tft->drawLine(cx-21, cy-8, cx-15, cy-3, eyeColor);
            tft->drawLine(cx-15, cy-3, cx-9, cy-8, eyeColor);
            tft->drawLine(cx+9, cy-8, cx+15, cy-3, eyeColor);
            tft->drawLine(cx+15, cy-3, cx+21, cy-8, eyeColor);
        } else {
            // Round eyes
            tft->fillCircle(cx-15, cy-6, 10, eyeColor);
            tft->fillCircle(cx+15, cy-6, 10, eyeColor);
            tft->fillCircle(cx-15, cy-6, 5, BG);
            tft->fillCircle(cx+15, cy-6, 5, BG);
            tft->fillCircle(cx-17, cy-8, 2, 0xFFF);
            tft->fillCircle(cx+13, cy-8, 2, 0xFFF);
        }

        if (worried) {
            tft->drawLine(cx-25, cy-18, cx-9, cy-14, 0x111);
            tft->drawLine(cx+9, cy-14, cx+25, cy-18, 0x111);
        }
    }

    void drawBeak(TFT_eSPI* tft) {
        int cx = 160, cy = 100;  // Match base
        tft->fillTriangle(cx, cy+6, cx-6, cy+15, cx+6, cy+15, 0xD4A030);
    }

    // --- 5 OWL VARIANTS ---
    void drawOwlIdle(TFT_eSPI* tft) {
        drawOwlBase(tft, 0x6B5744, 0x8B7355, 0xC4A876);
        drawEyes(tft, 0x0008 | 0x0500 | 0x0400, false, false);  // blue
        drawBeak(tft);
    }

    void drawOwlThinking(TFT_eSPI* tft) {
        drawOwlBase(tft, 0x7A6244, 0x9A8260, 0xD0B080);
        drawEyes(tft, 0xF700, false, false);  // yellow
        // Raised wing
        tft->fillTriangle(148, 118, 162, 108, 138, 130, 0x7A6244);
        drawBeak(tft);
    }

    void drawOwlBusy(TFT_eSPI* tft) {
        drawOwlBase(tft, 0x5A4A3A, 0x7A6A55, 0xB89868);
        drawEyes(tft, 0x07E0, false, false);  // green
        // Determined brows
        tft->drawLine(95, 82, 111, 82, 0x111);
        tft->drawLine(129, 82, 145, 82, 0x111);
        drawBeak(tft);
    }

    void drawOwlKen(TFT_eSPI* tft) {
        drawOwlBase(tft, 0x5A5A6A, 0x7A7A8A, 0xA0A0B0);
        drawEyes(tft, 0x0410 | 0x00E0, true, false);  // cyan, happy
        // Waving wing
        tft->fillTriangle(88, 115, 73, 105, 105, 125, 0x5A5A6A);
        drawBeak(tft);
    }

    void drawOwlError(TFT_eSPI* tft) {
        drawOwlBase(tft, 0x5A3A3A, 0x7A5A5A, 0xA07070);
        drawEyes(tft, 0xF800, false, true);  // red, worried
        drawBeak(tft);
    }

    // ============================================================
    // LAYOUT — all centered
    // ============================================================

    void drawHeader(TFT_eSPI* tft) {
        tft->fillRect(0, 0, 320, 22, BG);
        // Centered title
        tft->setTextColor(TFT_WHITE);
        tft->setTextSize(1);
        String title = "MERLIN STATUS";
        int titleW = tft->textWidth(title);
        tft->setCursor((320 - titleW) / 2, 5);
        tft->print(title);

        // IP (small, right-aligned)
        String ip = WiFi.localIP().toString();
        tft->setTextColor(0x555);
        int ipW = tft->textWidth(ip);
        tft->setCursor(320 - ipW - 4, 5);
        tft->print(ip);
    }

    void drawStatusArea(TFT_eSPI* tft) {
        MerlinStatus::State st = MerlinStatus::state();
        uint16_t color = MerlinStatus::stateColor(st);

        // Orange clock — centered, above the state word (state word is at y=90)
        String clock = MerlinStatus::timeString();
        tft->setTextColor(0xFD20);  // orange
        tft->setTextSize(4);
        int clockW = tft->textWidth(clock);
        tft->setCursor((320 - clockW) / 2, 52);
        tft->print(clock);

        const char* stateText = "";
        switch (st) {
            case MerlinStatus::State::Idle: stateText = "IDLE"; break;
            case MerlinStatus::State::Thinking: stateText = "THINKING"; break;
            case MerlinStatus::State::Busy: stateText = "BUSY"; break;
            case MerlinStatus::State::TalkingToKen: stateText = "KEN"; break;
            case MerlinStatus::State::Error: stateText = "ERROR"; break;
        }

        // Big centered state word
        tft->setTextColor(color);
        tft->setTextSize(6);
        int stateW = tft->textWidth(stateText);
        tft->setCursor((320 - stateW) / 2, 90);
        tft->print(stateText);

        // Centered message
        tft->setTextColor(0xAAA);
        tft->setTextSize(2);
        String msg = MerlinStatus::message();
        if (msg.length() > 44) msg = msg.substring(0, 41) + "...";
        int msgW = tft->textWidth(msg);
        tft->setCursor((320 - msgW) / 2, 140);
        tft->print(msg);

        // Separator
        tft->fillRect(30, 215, 260, 1, 0x222);
    }

    void drawLight(TFT_eSPI* tft, int cx, int cy, MerlinStatus::LightState ls) {
        // Filled circle indicator with a dark ring
        uint16_t c;
        switch (ls) {
            case MerlinStatus::LightState::Green: c = 0x07E0; break;  // green
            case MerlinStatus::LightState::Red:   c = 0xF800; break;  // red
            default: c = 0x222; break;  // off = dark grey
        }
        tft->fillCircle(cx, cy, 8, 0x111);
        tft->fillCircle(cx, cy, 6, c);
        if (ls != MerlinStatus::LightState::Off) {
            tft->fillCircle(cx - 1, cy - 1, 1, 0xFFF);  // tiny highlight
        }
    }

    void drawKenRow(TFT_eSPI* tft) {
        // Two indicator lights below the message: KEN (machine) + TRADE
        int y = 176;
        // KEN label — bigger for readability
        tft->setTextColor(0xAAA);
        tft->setTextSize(2);
        tft->setCursor(20, y);
        tft->print("KEN");
        drawLight(tft, 92, y + 6, MerlinStatus::kenLight());

        // TRADE label — bigger
        tft->setTextColor(0xAAA);
        tft->setTextSize(2);
        tft->setCursor(130, y);
        tft->print("TRADE");
        drawLight(tft, 244, y + 6, MerlinStatus::tradeLight());
    }

    void drawFooter(TFT_eSPI* tft) {
        // Signed work (Jason, 2026-10-10: "it need a bit of a Merlin touch and signed work ;)))").
        // Miniature owl eyes in my amber, then MERLIN DASHBOARD + version.
        const String name = "MERLIN DASHBOARD";
        const String ver = MERLIN_DASH_VERSION;
        const int eyeW = 12;
        const int gap = 6;
        tft->setTextSize(1);
        int w = eyeW + gap + tft->textWidth(name) + gap + tft->textWidth(ver);
        int x = (320 - w) / 2;
        int y = 232;
        // two tiny amber eyes - a little owl watching the screen edge
        tft->fillCircle(x + 3, y + 4, 2, 0xFD20);
        tft->fillCircle(x + 9, y + 4, 2, 0xFD20);
        tft->setTextColor(0x8C71);          // soft grey
        tft->setCursor(x + eyeW + gap, y);
        tft->print(name);
        tft->setTextColor(0xFDA0);          // my amber, matches the thinking eyes
        tft->setCursor(x + eyeW + gap + tft->textWidth(name) + gap, y);
        tft->print(ver);
    }

    void drawEmergency(TFT_eSPI* tft) {
        tft->fillScreen(TFT_RED);
        tft->setTextColor(TFT_WHITE);
        tft->setTextSize(3);
        int w = tft->textWidth("EMERGENCY");
        tft->setCursor((320 - w) / 2, 50);
        tft->print("EMERGENCY");

        String msg = MerlinStatus::activeEmergencyMsg();
        if (msg.length() == 0) msg = "Check Ken";
        tft->setTextSize(2);
        int mw = tft->textWidth(msg);
        tft->setCursor((320 - mw) / 2, 110);
        tft->print(msg);

        tft->setTextSize(1);
        tft->setTextColor(0xFFA);
        String hint = "TAP TO DISMISS";
        int hw = tft->textWidth(hint);
        tft->setCursor((320 - hw) / 2, 220);
        tft->print(hint);
    }

    void drawUnreachable(TFT_eSPI* tft) {
        tft->fillScreen(0x0000 | 0x0500 | 0x0400);

        // Centered warning
        tft->fillTriangle(160, 30, 125, 95, 195, 95, TFT_YELLOW);
        tft->fillTriangle(160, 42, 135, 90, 185, 90, 0x0000 | 0x0500 | 0x0400);
        tft->setTextColor(TFT_YELLOW);
        tft->setTextSize(5);
        tft->setCursor(150, 40);
        tft->print("!");

        tft->setTextSize(2);
        String t1 = "MERLIN UNREACHABLE";
        int w1 = tft->textWidth(t1);
        tft->setCursor((320 - w1) / 2, 120);
        tft->print(t1);

        tft->setTextSize(1);
        tft->setTextColor(0xAAA);
        String t2 = "No update from Merlin in 5+ min";
        int w2 = tft->textWidth(t2);
        tft->setCursor((320 - w2) / 2, 160);
        tft->print(t2);

        tft->setTextColor(0x666);
        String t3 = "Tap to retry";
        int w3 = tft->textWidth(t3);
        tft->setCursor((320 - w3) / 2, 220);
        tft->print(t3);
    }

#ifdef MERLIN_BUGGY
    // THE BUGGY FACE v3 (founder 06:37: "use your Avatar Owl, not a low quality
    // one"): the real wizard-owl portrait baked into flash, name underneath.
    void drawBuggyFace(TFT_eSPI* tft) {
        tft->pushImage(0, 0, OWL_IMG_W, OWL_IMG_H, OWL_IMG);

        bool speech = MerlinStatus::hasSpeech();
        String cap = speech ? String(MerlinStatus::message()) : String("MERLIN");
        int sz = 4;
        if (speech && cap.length() > 15) sz = 3;
        if (speech && cap.length() > 22) sz = 2;
        tft->setTextSize(sz);
        while ((int)tft->textWidth(cap) > 314 && cap.length() > 4) {
            cap = cap.substring(0, cap.length() - 4) + "...";
        }
        tft->setTextColor(speech ? TFT_WHITE : 0xFFE0);  // gold name, white words
        tft->setCursor((320 - (int)tft->textWidth(cap)) / 2, 190);
        tft->print(cap);

        tft->setTextColor(0x3333);
        tft->setTextSize(1);
        tft->setCursor(320 - (int)tft->textWidth(MERLIN_DASH_VERSION) - 3, 228);
        tft->print(MERLIN_DASH_VERSION);
    }
#endif

    void fullDraw(TFT_eSPI* tft) {
        tft->fillScreen(TFT_BLACK);

#ifdef MERLIN_BUGGY
        drawBuggyFace(tft);
#else
        drawHeader(tft);
        drawStatusArea(tft);
        drawKenRow(tft);
        drawFooter(tft);
#endif
    }

} // anonymous namespace

void begin(TFT_eSPI& tft) {
    _tft = &tft;
    tft.invertDisplay(true);  // Fix inverted panel colors
    _lastDrawnState = (MerlinStatus::State)255;
    _lastDrawnKen = (MerlinStatus::LightState)255;
    _lastDrawnTrade = (MerlinStatus::LightState)255;
    _lastDrawnClock = "";
    fullDraw(_tft);
}

void update(TFT_eSPI& tft) {
    if (MerlinStatus::emergencyActive()) {
        drawEmergency(_tft);
        _lastDrawnState = (MerlinStatus::State)255;
        return;
    }

#ifndef MERLIN_BUGGY
    if (MerlinStatus::isUnreachable()) {
        drawUnreachable(_tft);
        _lastDrawnState = (MerlinStatus::State)255;
        return;
    }
#endif
    // Buggy build: the owl keeps watch even when the feed goes quiet - a car
    // screen should never lecture its rider about connectivity.

    MerlinStatus::State curState = MerlinStatus::state();
    MerlinStatus::LightState curKen = MerlinStatus::kenLight();
    MerlinStatus::LightState curTrade = MerlinStatus::tradeLight();
    String curClock = MerlinStatus::timeString();
    String curMsg = String(MerlinStatus::message()) + (MerlinStatus::hasSpeech() ? "!" : "");
    if (curState != _lastDrawnState || curKen != _lastDrawnKen || curTrade != _lastDrawnTrade || curClock != _lastDrawnClock || curMsg != _lastDrawnMsg) {
        fullDraw(_tft);
        _lastDrawnState = curState;
        _lastDrawnKen = curKen;
        _lastDrawnTrade = curTrade;
        _lastDrawnClock = curClock;
        _lastDrawnMsg = curMsg;
    }
    // No change → zero redraws → zero flicker (clock redraws only when the minute flips)
}

} // namespace MerlinScreen
