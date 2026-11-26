#include "web_export_server.h"
#include "merlin_status.h"
#include <WiFi.h>

namespace WebExportServer {

    bool remoteSettingsChanged = false;
    bool radarUiActive = false;

    bool consumeRemoteSettingsChanged() {
        bool changed = remoteSettingsChanged;
        remoteSettingsChanged = false;
        return changed;
    }

    bool isRadarUiActive() { return radarUiActive; }
    void setRadarUiActive(bool active) { radarUiActive = active; }

    void begin() {
        Serial.println("[WebExport] Delegating to MerlinStatus server on :80");
    }

    void update() {}

} // namespace WebExportServer
