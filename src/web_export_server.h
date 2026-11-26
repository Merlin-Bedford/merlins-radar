#include <Arduino.h>
#include "config.h"

namespace WebExportServer {
    bool consumeRemoteSettingsChanged();
    bool isRadarUiActive();
    void setRadarUiActive(bool active);
    void begin();
    void update();
}
