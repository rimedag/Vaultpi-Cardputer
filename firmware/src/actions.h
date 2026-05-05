#pragma once
#include <ArduinoJson.h>
#include "network.h"

// ---------------------------------------------------------------------------
// Action execution — run an action on the bridge and return result
// ---------------------------------------------------------------------------

struct ActionResult {
    bool ok;
    char message[128];
};

// Run an action by its id string (e.g. "cmd-3", "gitea-gitea-backup", "sys-shutdown")
inline ActionResult runAction(const char* actionId) {
    ActionResult result = {false, ""};
    char path[128];
    snprintf(path, sizeof(path), "/api/v1/actions/%s/run", actionId);

    JsonDocument doc;
    int code = bridgePost(path, doc);
    if (code < 0) {
        strlcpy(result.message, "No response from bridge", sizeof(result.message));
        return result;
    }
    if (code != 200 && code != 202) {
        snprintf(result.message, sizeof(result.message), "HTTP %d", code);
        return result;
    }
    result.ok = doc["ok"] | false;
    const char* msg = doc["message"] | "";
    strlcpy(result.message, msg, sizeof(result.message));
    return result;
}
