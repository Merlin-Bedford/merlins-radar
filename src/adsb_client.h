#pragma once
#include <Arduino.h>
#include "aircraft.h"
#include "config.h"

namespace AdsbClient {

    struct FetchResult {
        bool     ok = false;
        uint16_t aircraftCount = 0;
        int      httpCode = 0;
        // Nur gesetzt (>=0), wenn httpCode==429 UND der Server einen
        // "Retry-After"-Header mitgeschickt hat (siehe net_task.cpp fuer
        // die Backoff-Logik, die diesen Wert bevorzugt gegenueber der
        // eigenen Schaetzung verwendet).
        int      retryAfterSec = -1;
    };
    // MERLIN-PATCH 2026-11-27: Quellenauswahl (0 = ADSB_API_HOST, primaer,
    // 1 = ADSB_FALLBACK_HOST). Standard bleibt 0, der net_task.cpp haelt
    // einen echten Fehlversuch (HTTP-Fehler ODER 0 Flugzeuge) und versucht
    // dann dieselbe Abfrage gegen Quelle 1 - damit der Radar nicht mehr von
    // einer einzigen Quelle abhaengt (Grund des Ausfalls: adsb.lol drosselt
    // keylos auf 500 Requests/Tag pro IP).
    FetchResult fetch(double homeLat, double homeLon, float radiusKm,
                       Aircraft* table, uint8_t tableCapacity, uint8_t source = 0);
    void primeTime();

}