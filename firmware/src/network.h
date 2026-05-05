#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "config.h"

extern char g_bridge_host[80];
extern char g_bridge_psk[64];

inline bool wifiOk() { return WiFi.status() == WL_CONNECTED; }

// Auto-detect HTTP vs HTTPS from control-center host prefix
static inline bool _bridgeIsHttps() {
    return strncmp(g_bridge_host, "https://", 8) == 0;
}

// Shared secure client - reused across requests (avoids heap fragmentation)
static WiFiClientSecure _secureClient;

static inline void _httpBegin(HTTPClient& http, const char* path) {
    String url = String(g_bridge_host) + path;
    if (_bridgeIsHttps()) {
        _secureClient.setInsecure(); // self-signed cert OK on local LAN
        http.begin(_secureClient, url);
    } else {
        http.begin(url);
    }
    http.addHeader("X-Bridge-Token", g_bridge_psk);
    http.addHeader("X-Cardputer-Password", g_bridge_psk);
    http.setTimeout(HTTP_TIMEOUT_MS);
}

// GET request — returns HTTP status code, -1=no wifi, -2=parse error
inline int bridgeGet(const char* path, JsonDocument& doc) {
    if (!wifiOk()) return -1;
    HTTPClient http;
    _httpBegin(http, path);
    int code = http.GET();
    if (code > 0) {
        DeserializationError err = deserializeJson(doc, http.getStream());
        if (err && code == 200) code = -2;
    }
    http.end();
    return code;
}

// GET without PSK header â€” useful for health checks and diagnostics.
inline int bridgeGetNoAuth(const char* path, JsonDocument& doc) {
    if (!wifiOk()) return -1;
    HTTPClient http;
    String url = String(g_bridge_host) + path;
    if (_bridgeIsHttps()) {
        _secureClient.setInsecure();
        http.begin(_secureClient, url);
    } else {
        http.begin(url);
    }
    http.setTimeout(HTTP_TIMEOUT_MS);
    int code = http.GET();
    if (code > 0) {
        DeserializationError err = deserializeJson(doc, http.getStream());
        if (err && code == 200) code = -2;
    }
    http.end();
    return code;
}

// POST with empty body — returns HTTP status, -1=no wifi, -2=parse error
inline int bridgePost(const char* path, JsonDocument& doc) {
    if (!wifiOk()) return -1;
    HTTPClient http;
    _httpBegin(http, path);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST("{}");
    if (code > 0) {
        DeserializationError err = deserializeJson(doc, http.getStream());
        if (err && (code == 200 || code == 202)) code = -2;
    }
    http.end();
    return code;
}

// POST with body string — returns HTTP status
inline int bridgePostBody(const char* path, const String& body, JsonDocument& doc) {
    if (!wifiOk()) return -1;
    HTTPClient http;
    _httpBegin(http, path);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    if (code > 0) {
        DeserializationError err = deserializeJson(doc, http.getStream());
        if (err && (code == 200 || code == 202)) code = -2;
    }
    http.end();
    return code;
}

// DELETE request — for clearing alert queue etc.
inline int bridgeDelete(const char* path) {
    if (!wifiOk()) return -1;
    HTTPClient http;
    _httpBegin(http, path);
    int code = http.sendRequest("DELETE");
    http.end();
    return code;
}

// Fire-and-forget state push. Returns HTTP status so callers can disable it on failure.
inline int bridgePushState(const String& jsonBody) {
    if (!wifiOk()) return -1;
    HTTPClient http;
    String url = String(g_bridge_host) + "/api/v1/cardputer/screen";
    if (_bridgeIsHttps()) {
        _secureClient.setInsecure();
        http.begin(_secureClient, url);
    } else {
        http.begin(url);
    }
    http.addHeader("X-Bridge-Token", g_bridge_psk);
    http.addHeader("X-Cardputer-Password", g_bridge_psk);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(FAST_HTTP_TIMEOUT_MS);
    int code = http.POST(jsonBody);
    http.end();
    return code;
}
