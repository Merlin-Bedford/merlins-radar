#include "boot_menu.h"
#include "touch_input.h"
#include "merlin_screen.h"
#include <WiFi.h>

namespace BootMenu {

Choice run(TFT_eSPI& tft) {
    // Layout: two big buttons stacked vertically (portrait 240x320)
    // Button 1: Merlin (top half, y=30 to y=150)
    // Button 2: Flight Radar (bottom half, y=170 to y=290)
    // Each button: 200x110 px, centered horizontally

    constexpr int16_t BTN_W = 200;
    constexpr int16_t BTN_H = 110;
    constexpr int16_t BTN_X = (Config::SCREEN_WIDTH - BTN_W) / 2;
    constexpr int16_t BTN1_Y = 30;
    constexpr int16_t BTN2_Y = 170;

    // Background
    tft.fillScreen(0x111);

    // Title
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    int16_t titleW = 8 * 16; // rough width for "SELECT APP"
    tft.setCursor((Config::SCREEN_WIDTH - titleW) / 2, 10);
    tft.print("SELECT APP");

    // Draw Merlin button (blue-ish)
    uint16_t merlinColor = 0x235;
    tft.fillRect(BTN_X, BTN1_Y, BTN_W, BTN_H, merlinColor);
    tft.drawRect(BTN_X, BTN1_Y, BTN_W, BTN_H, TFT_WHITE);

    // Draw a small owl on the Merlin button
    MerlinScreen::drawOwl(tft, Config::SCREEN_WIDTH / 2, BTN1_Y + 35, MerlinStatus::State::Idle);

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    int16_t mw = 6 * 16;
    tft.setCursor((Config::SCREEN_WIDTH - mw) / 2, BTN1_Y + 70);
    tft.print("MERLIN");

    tft.setTextSize(1);
    tft.setTextColor(0xAAA);
    int16_t sw = 12 * 6;
    tft.setCursor((Config::SCREEN_WIDTH - sw) / 2, BTN1_Y + 90);
    tft.print("Status display");

    // Draw Radar button (green-ish)
    uint16_t radarColor = 0x152;
    tft.fillRect(BTN_X, BTN2_Y, BTN_W, BTN_H, radarColor);
    tft.drawRect(BTN_X, BTN2_Y, BTN_W, BTN_H, TFT_WHITE);

    // Simple radar circle icon
    int16_t rcx = Config::SCREEN_WIDTH / 2;
    int16_t rcy = BTN2_Y + 35;
    tft.drawCircle(rcx, rcy, 25, 0x3A5);
    tft.drawCircle(rcx, rcy, 15, 0x3A5);
    tft.drawCircle(rcx, rcy, 5, 0x5D7);
    tft.drawLine(rcx, rcy, rcx + 20, rcy - 15, 0x5D7); // sweep line

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    int16_t rw = 14 * 16;
    tft.setCursor((Config::SCREEN_WIDTH - rw) / 2, BTN2_Y + 70);
    tft.print("MERLIN'S RADAR");

    tft.setTextSize(1);
    tft.setTextColor(0xAAA);
    int16_t sw2 = 16 * 6;
    tft.setCursor((Config::SCREEN_WIDTH - sw2) / 2, BTN2_Y + 90);
    tft.print("Live ADS-B traffic");

    // Wait for tap
    TouchInput::Point tap;
    while (true) {
        if (TouchInput::wasTapped(tap)) {
            if (tap.x >= BTN_X && tap.x < BTN_X + BTN_W) {
                if (tap.y >= BTN1_Y && tap.y < BTN1_Y + BTN_H) {
                    return Choice::Merlin;
                } else if (tap.y >= BTN2_Y && tap.y < BTN2_Y + BTN_H) {
                    return Choice::Radar;
                }
            }
        }
        delay(20);
    }
}

} // namespace BootMenu
