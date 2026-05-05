#pragma once
// ---------------------------------------------------------------------------
// OTA firmware update over HTTP(S)
// Bridge serves /api/v1/firmware/latest  →  { version_num, version, url }
// Bridge serves /api/v1/firmware/download  →  raw binary
// ---------------------------------------------------------------------------
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "network.h"
#include "display.h"

// Bump this whenever firmware changes (matches bridge latest.json metadata)
#define FW_VERSION      "2.5.1"
#define FW_VERSION_NUM  20501   // major*10000 + minor*100 + patch

struct OTAInfo {
    bool    available   = false;
    char    version[16] = "";
    char    url[160]    = "";
    int     size        = 0;
};

// Returns true and fills info if a newer version is available
inline bool otaCheck(OTAInfo& info) {
    JsonDocument doc;
    int code = bridgeGet("/api/v1/firmware/latest", doc);
    if (code != 200) return false;
    int serverNum = doc["version_num"] | 0;
    const char* ver = doc["version"] | "";
    const char* url = doc["url"]     | "";
    strlcpy(info.version, ver, sizeof(info.version));
    if (!url[0]) {
        static char fallbackUrl[192];
        snprintf(fallbackUrl, sizeof(fallbackUrl), "%s/api/v1/firmware/download", g_bridge_host);
        url = fallbackUrl;
    }
    strlcpy(info.url,     url, sizeof(info.url));
    info.size      = doc["size"] | 0;
    info.available = serverNum > FW_VERSION_NUM;
    return true;
}

// Download binary from url and flash via OTA partition
// progressCb called with 0-100 during download (may be nullptr)
inline bool otaApply(const char* url, void (*progressCb)(int) = nullptr) {
    HTTPClient http;
    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    bool isHttps = (strncmp(url, "https://", 8) == 0);
    if (isHttps) http.begin(secureClient, url);
    else          http.begin(url);

    http.addHeader("X-Bridge-Token", g_bridge_psk);
    http.setTimeout(30000);

    int code = http.GET();
    if (code != 200) { http.end(); return false; }

    int total = http.getSize();
    if (total <= 0) { http.end(); return false; }

    if (!Update.begin(total)) { http.end(); return false; }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[512];
    int received = 0;
    while (http.connected() && received < total) {
        int avail = stream->available();
        if (avail == 0) { delay(1); continue; }
        int toRead = min(avail, (int)sizeof(buf));
        int n = stream->readBytes(buf, toRead);
        Update.write(buf, n);
        received += n;
        if (progressCb) progressCb(received * 100 / total);
    }
    http.end();

    if (!Update.end(true))         return false;
    if (!Update.isFinished())      return false;
    return true;
}
