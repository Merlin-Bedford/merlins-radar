#include "merlin_status.h"
#include <WiFi.h>
#include <WebServer.h>

namespace MerlinStatus {

namespace {

    bool serverStarted = false;
    WebServer* server = nullptr;

    State curState = State::Idle;
    String curMessage = "Merlin online";
    bool speechActive = false;
    uint32_t lastSeenMs = 0;      // Last time we heard from Merlin (for UNREACHABLE check)
    uint32_t lastStatePushMs = 0; // Last time a state was pushed (for AUTO-IDLE)

    AppMode curAppMode = AppMode::Merlin;

    String emergencyQueue[3];
    int emergencyCount = 0;
    bool emergencyActiveFlag = false;
    String activeEmergency = "";
    uint32_t emergencyFlashMs = 0;

    const uint32_t UNREACHABLE_TIMEOUT_MS = 300000;
    bool merlinUnreachable = false;

    // v1.5 dual-feed: UART2 lane for the buggy (Pi feeds JSON lines over
    // GPIO16=RX, GPIO17=TX at 115200). Same state machine as the HTTP lane,
    // so the face lives even with no WiFi at all. Protocol: one JSON object
    // per line, e.g. {"state":"busy","message":"rolling"} or
    // {"cmd":"emergency","message":"..."} / {"cmd":"clear"} / {"cmd":"ping"}.
    bool uartStarted = false;
    String uartLine = "";

    // Pin-hunt probe (v1.7): the ribbon may have landed on any free header
    // pins. Cycle UART2 RX through candidates every 8s; when a real line
    // parses, freeze RX and hunt TX (for the Pi's ack log) through the list.
    // Candidates avoid display SPI (12-15), backlight (21!! the hunt killed
    // it once - never touch 21 again), SD, boot strapping (0,2), speaker (26)
    // and USB-serial pins (3).
    const int UART_CANDS[] = {16, 17, 18, 19, 22, 23, 25, 27, 32, 33, 4};
    const int UART_CAND_N = sizeof(UART_CANDS) / sizeof(UART_CANDS[0]);
    int uartRxIdx = 0, uartTxIdx = 0;
    int uartRxPin = 16, uartTxPin = 17;
    uint32_t uartSwapMs = 0;
    uint32_t uartTxSwapMs = 0;
    bool uartRxLocked = false;
    bool uartTxHuntDone = false;
    bool usbFeedAlive = false;

    // Ken / trading lights
    LightState kenLightVal = LightState::Off;
    LightState tradeLightVal = LightState::Off;
    uint32_t kenLastSeen = 0;
    uint32_t tradeLastSeen = 0;
    const uint32_t LIGHT_STALE_MS = 120000;  // lights go stale (red) if not updated in 2min

    LightState effectiveLight(LightState base, uint32_t lastSeen) {
        if (base == LightState::Off) return LightState::Off;
        if (lastSeen == 0) return LightState::Red;
        if (millis() - lastSeen > LIGHT_STALE_MS) return LightState::Red;
        return base;
    }

    String jsonGetString(const String& json, const char* key) {
        String search = String("\"") + key + "\"";
        int idx = json.indexOf(search);
        if (idx < 0) return "";
        idx = json.indexOf(':', idx + search.length());
        if (idx < 0) return "";
        idx = json.indexOf('"', idx + 1);
        if (idx < 0) return "";
        int end = json.indexOf('"', idx + 1);
        if (end < 0) return "";
        return json.substring(idx + 1, end);
    }

    const char* stateName(State s) {
        switch (s) {
            case State::Idle: return "idle";
            case State::Thinking: return "thinking";
            case State::Busy: return "busy";
            case State::TalkingToKen: return "ken";
            case State::Error: return "error";
        }
        return "idle";
    }

    // Shared by the HTTP lane and the UART lane (v1.5 dual-feed).
    void applyStatus(const String& state, const String& msg) {
        if (state == "idle") curState = State::Idle;
        else if (state == "thinking") curState = State::Thinking;
        else if (state == "busy") curState = State::Busy;
        else if (state == "ken") curState = State::TalkingToKen;
        else if (state == "error") curState = State::Error;

        if (msg.length() > 0) {
            curMessage = (msg.length() > 57) ? msg.substring(0, 54) + "..." : msg;
            speechActive = true;
        } else {
            speechActive = false;  // defaults are NOT speech - buggy face shows MERLIN
            switch (curState) {
                case State::Idle: curMessage = "Idle, ready when you are"; break;
                case State::Thinking: curMessage = "Thinking..."; break;
                case State::Busy: curMessage = "Working on something"; break;
                case State::TalkingToKen: curMessage = "Talking to Ken"; break;
                case State::Error: curMessage = "Something's wrong"; break;
            }
        }

        lastSeenMs = millis();
        lastStatePushMs = millis();
        merlinUnreachable = false;
    }

    void handleStatus() {
        if (server->method() != HTTP_POST) { server->send(405, "text/plain", "POST only"); return; }

        String state = "idle";
        String msg = "";

        // Read from URL query params first (most reliable)
        // /merlin/status?state=busy&message=hello
        for (int i = 0; i < server->args(); i++) {
            String name = server->argName(i);
            String value = server->arg(i);
            if (name == "state") state = value;
            else if (name == "message") msg = value;
        }

        // Also check POST body if present
        if (server->hasArg("state")) state = server->arg("state");

        applyStatus(state, msg);
        server->send(200, "application/json", "{\"ok\":true,\"state\":\"" + String(stateName(curState)) + "\"}");
    }

    void handleEmergency() {
        if (server->method() != HTTP_POST) { server->send(405, "text/plain", "POST only"); return; }

        String msg = "MERLIN ALERT";

        // Read from URL query params
        for (int i = 0; i < server->args(); i++) {
            String name = server->argName(i);
            if (name == "message") msg = server->arg(i);
        }

        if (msg.length() > 77) msg = msg.substring(0, 74) + "...";

        if (emergencyCount < 3) {
            emergencyQueue[emergencyCount] = msg;
            emergencyCount++;
        } else {
            emergencyQueue[0] = emergencyQueue[1];
            emergencyQueue[1] = emergencyQueue[2];
            emergencyQueue[2] = msg;
        }

        activeEmergency = emergencyQueue[emergencyCount - 1];
        emergencyActiveFlag = true;
        emergencyFlashMs = millis();
        server->send(200, "application/json", "{\"ok\":true}");
    }

    void handleClearEmergency() {
        if (server->method() != HTTP_POST) { server->send(405, "text/plain", "POST only"); return; }
        clearEmergency();
        server->send(200, "application/json", "{\"ok\":true}");
    }

    void handleState() {
        String json = "{\"state\":\"";
        switch (curState) {
            case State::Idle: json += "idle"; break;
            case State::Thinking: json += "thinking"; break;
            case State::Busy: json += "busy"; break;
            case State::TalkingToKen: json += "ken"; break;
            case State::Error: json += "error"; break;
        }
        json += "\",\"message\":\"";
        json += curMessage;
        json += "\",\"emergency\":";
        json += emergencyActiveFlag ? "true" : "false";
        if (emergencyActiveFlag) {
            json += ",\"emergency_message\":\"";
            json += activeEmergency;
            json += "\"";
        }
        json += ",\"unreachable\":";
        json += merlinUnreachable ? "true" : "false";
        json += ",\"app_mode\":\"";
        json += (curAppMode == AppMode::Merlin) ? "merlin" : "radar";
        json += "\"";
        json += ",\"uart_rx\":";
        json += String(uartRxPin);
        json += ",\"uart_tx\":";
        json += String(uartTxPin);
        json += ",\"uart_locked\":";
        json += uartRxLocked ? "true" : "false";
        json += ",\"usb_alive\":";
        json += usbFeedAlive ? "true" : "false";
        json += ",\"ver\":\"";
        json += version();
        json += "\"}";
        server->send(200, "application/json", json);
    }

    void handleSetAppMode() {
        if (server->method() != HTTP_POST) { server->send(405, "text/plain", "POST only"); return; }
        String mode = "merlin";
        for (int i = 0; i < server->args(); i++) {
            String name = server->argName(i);
            if (name == "mode") mode = server->arg(i);
        }
        if (mode == "merlin") curAppMode = AppMode::Merlin;
        else if (mode == "radar") curAppMode = AppMode::Radar;
        server->send(200, "application/json", "{\"ok\":true}");
    }

    void handleKenStatus() {
        if (server->method() != HTTP_POST) { server->send(405, "text/plain", "POST only"); return; }
        String ken = "off";      // off | on
        String trade = "off";    // off | on
        for (int i = 0; i < server->args(); i++) {
            String name = server->argName(i);
            String value = server->arg(i);
            if (name == "ken") ken = value;
            else if (name == "trade") trade = value;
        }
        if (ken == "on") { kenLightVal = LightState::Green; kenLastSeen = millis(); }
        else if (ken == "off") { kenLightVal = LightState::Red; kenLastSeen = millis(); }
        if (trade == "on") { tradeLightVal = LightState::Green; tradeLastSeen = millis(); }
        else if (trade == "off") { tradeLightVal = LightState::Red; tradeLastSeen = millis(); }
        server->send(200, "application/json", "{\"ok\":true}");
    }

    // ---- feeds: UART2 (buggy header pins) + UART0 (USB cable) ----
    String usbLine = "";

    void handleUartLine(const String& line, bool viaUart2) {
        if (line.length() == 0) return;
        if (line.charAt(0) != '{') {
            Serial.print("[MerlinStatus] feed junk line: ");
            Serial.println(line.substring(0, 60));
            return;
        }
        Stream& out = viaUart2 ? Serial2 : Serial;
        if (viaUart2 && !uartRxLocked) {
            uartRxLocked = true;
            uartTxSwapMs = millis();
            curMessage = "UART LINK: RX=G" + String(uartRxPin);
            Serial.printf("[MerlinStatus] UART LOCKED: RX=G%d (TX now hunting)\n", uartRxPin);
        }
        if (viaUart2 || usbFeedAlive == false) usbFeedAlive = true;
        String cmd = jsonGetString(line, "cmd");
        String msg = jsonGetString(line, "message");

        if (cmd == "emergency") {
            String m = (msg.length() > 0) ? msg : String("MERLIN ALERT");
            if (m.length() > 77) m = m.substring(0, 74) + "...";
            setEmergency(m.c_str());
            out.println("{\"ok\":true,\"applied\":\"emergency\"}");
            return;
        }
        if (cmd == "clear") {
            clearEmergency();
            out.println("{\"ok\":true,\"applied\":\"clear\"}");
            return;
        }
        if (cmd == "lights") {
            String ken = jsonGetString(line, "ken");
            String trade = jsonGetString(line, "trade");
            if (ken == "on") setKenLight(LightState::Green);
            else if (ken == "off") setKenLight(LightState::Red);
            if (trade == "on") setTradeLight(LightState::Green);
            else if (trade == "off") setTradeLight(LightState::Red);
            out.println("{\"ok\":true,\"applied\":\"lights\"}");
            return;
        }
        if (cmd == "ping") {
            out.println(String("{\"ok\":true,\"applied\":\"ping\",\"state\":\"") + stateName(curState) + "\"}");
            return;
        }

        // Default: plain status line {"state":"...","message":"..."}
        String st = jsonGetString(line, "state");
        if (st == "") st = "idle";
        applyStatus(st, msg);
        out.println(String("{\"ok\":true,\"applied\":\"status\",\"state\":\"") + stateName(curState) + "\"}");
    }

    // USB cable feed: the CP2102 lands on UART0 ("Serial"). One cable carries
    // power AND words - the car Pi writes to /dev/ttyUSBx and the face lives.
    void pumpUsb() {
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n') {
                String line = usbLine;
                usbLine = "";
                handleUartLine(line, false);
            } else if (c != '\r' && usbLine.length() < 400) {
                usbLine += c;
            }
        }
    }

    void pumpUart() {
        if (!uartRxLocked && millis() - uartSwapMs > 8000) {
            uartSwapMs = millis();
            uartRxIdx++;
            if (uartRxIdx >= UART_CAND_N) uartRxIdx = 0;  // loop the hunt
            uartRxPin = UART_CANDS[uartRxIdx];
            Serial2.end();
            Serial2.begin(115200, SERIAL_8N1, uartRxPin, uartTxPin);
            uartLine = "";
            Serial.printf("[MerlinStatus] UART hunt: RX=G%d (TX=G%d)\n", uartRxPin, uartTxPin);
        } else if (uartRxLocked && !uartTxHuntDone && millis() - uartTxSwapMs > 6000) {
            uartTxSwapMs = millis();
            uartTxIdx++;
            if (uartTxIdx >= UART_CAND_N) uartTxHuntDone = true;
            else {
                uartTxPin = UART_CANDS[uartTxIdx];
                Serial2.end();
                Serial2.begin(115200, SERIAL_8N1, uartRxPin, uartTxPin);
                uartLine = "";
                Serial.printf("[MerlinStatus] UART TX hunt: TX=G%d\n", uartTxPin);
            }
        }
        while (Serial2.available()) {
            char c = (char)Serial2.read();
            if (c == '\n') {
                String line = uartLine;
                uartLine = "";
                handleUartLine(line, true);
            } else if (c != '\r') {
                if (uartLine.length() < 400) uartLine += c;
                else uartLine = "";  // runaway line: drop and resync
            }
        }
    }

} // anonymous namespace

void begin() {
    // UART lane always starts, WiFi or not - on the buggy it is the lifeline.
    // NOTE: GPIO16/17 also drive the optional RGB LED on some CYD boards
    // (active-low) - harmless: it just flickers while data flows. led_alert
    // is never started in this Merlin build, so the pins are ours.
    Serial2.begin(115200, SERIAL_8N1, uartRxPin, uartTxPin);  // start RX=G16/TX=G17, then hunts
    uartStarted = true;
    uartSwapMs = millis();
    Serial.println("[MerlinStatus] UART2 pin-hunt armed (RX starts G16)");

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MerlinStatus] WiFi not connected, HTTP server disabled");
        lastSeenMs = millis();
        lastStatePushMs = millis();
        return;
    }

    server = new WebServer(80);
    server->on("/merlin/status", HTTP_POST, handleStatus);
    server->on("/merlin/emergency", HTTP_POST, handleEmergency);
    server->on("/merlin/clear_emergency", HTTP_POST, handleClearEmergency);
    server->on("/merlin/state", HTTP_GET, handleState);
    server->on("/merlin/set_app_mode", HTTP_POST, handleSetAppMode);
    server->on("/ken/status", HTTP_POST, handleKenStatus);
    server->onNotFound([]() {
        server->send(404, "text/plain", "Try: /merlin/state");
    });
    server->begin();
    serverStarted = true;
    lastSeenMs = millis();
    lastStatePushMs = millis();

    // NTP time — Europe/London, DST handled by the POSIX rule:
    //   GMT0  = standard time, UTC+0
    //   BST   = daylight saving, UTC+1 (default +1 hour after the std name)
    //   M3.5.0/1  = starts last Sunday of March, 01:00 UTC
    //   M10.5.0/2 = ends last Sunday of October, 02:00 local
    // A fixed offset ("GMT-1") was wrong: it pinned the clock to UTC+1 all year,
    // so after BST ended (25 Oct 2026) the screen ran one hour fast.
    configTzTime("GMT0BST,M3.5.0/1,M10.5.0/2", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    Serial.printf("[MerlinStatus] HTTP on %s:80\n", WiFi.localIP().toString().c_str());
}

bool timeValid() {
    struct tm t;
    if (!getLocalTime(&t)) return false;
    // getLocalTime returns 1970 if no sync yet
    return (t.tm_year + 1900) > 2020;
}

String timeString() {
    struct tm t;
    if (!getLocalTime(&t) || (t.tm_year + 1900) <= 2020) return "--:--";
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    return String(buf);
}

void syncTime() {
    struct tm t;
    getLocalTime(&t);
}

void update() {
    if (serverStarted && server) server->handleClient();
    if (uartStarted) pumpUart();
    pumpUsb();

    // Auto-idle: if no state push in 30s (on either feed), switch to idle
    if (millis() - lastStatePushMs > 30000 && curState != State::Idle) {
        curState = State::Idle;
        curMessage = "Idle, ready when you are";
        Serial.println("[MerlinStatus] Auto-idle (30s no push)");
    }

    // Unreachable: no heartbeat on EITHER feed for 5min -> warning
    if (millis() - lastSeenMs > UNREACHABLE_TIMEOUT_MS && !merlinUnreachable) {
        merlinUnreachable = true;
        Serial.println("[MerlinStatus] UNREACHABLE (>5min)");
    }
}

State state() { return curState; }
const char* message() { return curMessage.c_str(); }
bool hasSpeech() { return speechActive; }
const char* version() { return "v3.1"; }
bool emergencyActive() { return emergencyActiveFlag; }
bool isUnreachable() { return merlinUnreachable; }
String activeEmergencyMsg() { return activeEmergency; }
AppMode appMode() { return curAppMode; }
void setAppMode(AppMode mode) { curAppMode = mode; }
bool emergencyQueued() { return emergencyCount > 0; }

void setEmergency(const char* msg) {
    String m(msg);
    if (emergencyCount < 3) {
        emergencyQueue[emergencyCount] = m;
        emergencyCount++;
    } else {
        emergencyQueue[0] = emergencyQueue[1];
        emergencyQueue[1] = emergencyQueue[2];
        emergencyQueue[2] = m;
    }
    activeEmergency = emergencyQueue[emergencyCount - 1];
    emergencyActiveFlag = true;
    emergencyFlashMs = millis();
}

void clearEmergency() {
    emergencyActiveFlag = false;
    activeEmergency = "";
    emergencyCount = 0;
    emergencyQueue[0] = "";
    emergencyQueue[1] = "";
    emergencyQueue[2] = "";
}

uint16_t stateColor(State s) {
    switch (s) {
        case State::Idle: return 0x0008 | 0x0500 | 0x0400;
        case State::Thinking: return 0xF700;
        case State::Busy: return 0x07E0;
        case State::TalkingToKen: return 0x0410 | 0x00E0;
        case State::Error: return 0xF800;
    }
    return 0xFFFF;
}

uint8_t stateForColor(State s) {
    switch (s) {
        case State::Idle: return 0;
        case State::Thinking: return 1;
        case State::Busy: return 2;
        case State::TalkingToKen: return 3;
        case State::Error: return 4;
    }
    return 0;
}

LightState kenLight() { return effectiveLight(kenLightVal, kenLastSeen); }
LightState tradeLight() { return effectiveLight(tradeLightVal, tradeLastSeen); }
void setKenLight(LightState s) { kenLightVal = s; if (s != LightState::Off) kenLastSeen = millis(); }
void setTradeLight(LightState s) { tradeLightVal = s; if (s != LightState::Off) tradeLastSeen = millis(); }
uint32_t kenLastSeenMs() { return kenLastSeen; }
uint32_t tradeLastSeenMs() { return tradeLastSeen; }

} // namespace MerlinStatus
