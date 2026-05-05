#pragma once
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include "config.h"

struct RuntimeConfig {
    // Network
    char wifi_ssid[3][64];
    char wifi_pass[3][64];
    char bridge_host[80];
    char bridge_psk[64];
    char device_nickname[16];
    // Timing
    int  poll_sec;
    int  dim_sec_power;  // 0=never
    int  off_sec_power;  // 0=never
    int  dim_sec_bat;
    int  off_sec_bat;
    int  go_btn_mode;    // 0=Default, 1=Lock
    // Security
    bool pin_enabled;
    char pin_code[5];    // 4 digits + null
    // Audio
    uint8_t spk_volume;  // 0-255
    // IR
    int  ir_device;      // profile index
    // Display / theme
    int  theme_idx;      // 0=P1NK 1=CYB3R 2=AMB3R 3=GH0ST 4=BL00D 5=L1ME
    bool idle_anim;      // rain+mountains+birds on idle screen
    // USB Macro scheduler
    char macro_text[80]; // text to type via HID
    uint8_t macro_hour;  // 0-23 (local time)
    uint8_t macro_min;   // 0-59
    bool macro_enabled;  // arm the macro
    int  tz_offset;      // UTC offset in hours (-12 to +14)
};

static RuntimeConfig rconfig;

static constexpr int SD_SPI_SCK_PIN  = 40;
static constexpr int SD_SPI_MISO_PIN = 39;
static constexpr int SD_SPI_MOSI_PIN = 14;
static constexpr int SD_SPI_CS_PIN   = 12;
static constexpr const char* NOTES_DIR = "/vaultpi_notes";

inline bool ensureNotesStorage() {
    static bool tried = false;
    static bool ready = false;
    if (ready) return true;
    if (!tried) {
        tried = true;
        SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
        ready = SD.begin(SD_SPI_CS_PIN, SPI, 25000000);
        if (ready && !SD.exists(NOTES_DIR)) {
            SD.mkdir(NOTES_DIR);
        }
    }
    return ready;
}

inline void notePath(int idx, char* out, size_t outLen) {
    snprintf(out, outLen, "%s/note%d.txt", NOTES_DIR, idx + 1);
}

inline void normalizeBridgeHost(char* host, size_t maxLen) {
    if (!host || !host[0] || maxLen == 0) return;

    const char* port8001 = ":8001";
    if (strstr(host, port8001)) return;

    char* scheme = strstr(host, "://");
    char* port    = nullptr;
    if (scheme) {
        port = strrchr(scheme + 3, ':');
    } else {
        port = strrchr(host, ':');
    }

    if (port) {
        *port = '\0';
        strlcat(host, ":8001", maxLen);
    } else {
        strlcat(host, ":8001", maxLen);
    }
}

inline void loadConfig() {
    Preferences p;
    p.begin("vaultpi", true);

    for (int i = 0; i < 3; i++) {
        char k[8];
        snprintf(k, sizeof(k), "ssid%d", i);
        String s = p.getString(k, i < WIFI_NET_COUNT ? WIFI_SSIDS[i] : "");
        strlcpy(rconfig.wifi_ssid[i], s.c_str(), 64);
        snprintf(k, sizeof(k), "pass%d", i);
        s = p.getString(k, i < WIFI_NET_COUNT ? WIFI_PASSES[i] : "");
        strlcpy(rconfig.wifi_pass[i], s.c_str(), 64);
    }

    strlcpy(rconfig.bridge_host, p.getString("bridge_host", BRIDGE_HOST).c_str(), 80);
    normalizeBridgeHost(rconfig.bridge_host, sizeof(rconfig.bridge_host));
    strlcpy(rconfig.bridge_psk,  p.getString("bridge_psk",  BRIDGE_PSK ).c_str(), 64);
    strlcpy(rconfig.device_nickname, p.getString("nickname", DEFAULT_DEVICE_NICKNAME).c_str(), sizeof(rconfig.device_nickname));
    if (!rconfig.device_nickname[0]) {
        strlcpy(rconfig.device_nickname, DEFAULT_DEVICE_NICKNAME, sizeof(rconfig.device_nickname));
    }

    rconfig.poll_sec      = p.getInt("poll_sec",  DEFAULT_POLL_SEC);
    rconfig.dim_sec_power = p.getInt("dim_pwr",  DEFAULT_DIM_SEC_POWER);
    rconfig.off_sec_power = p.getInt("off_pwr",  DEFAULT_OFF_SEC_POWER);
    rconfig.dim_sec_bat   = p.getInt("dim_bat",  DEFAULT_DIM_SEC_BAT);
    rconfig.off_sec_bat   = p.getInt("off_bat",  DEFAULT_OFF_SEC_BAT);
    rconfig.go_btn_mode   = p.getInt("go_mode",  DEFAULT_GO_BTN_MODE);
    rconfig.pin_enabled= p.getBool("pin_en",   DEFAULT_PIN_ENABLED);
    strlcpy(rconfig.pin_code, p.getString("pin_code", DEFAULT_PIN_CODE).c_str(), 5);
    rconfig.spk_volume   = (uint8_t)p.getInt("spk_vol",   DEFAULT_SPK_VOLUME);
    rconfig.ir_device    = p.getInt("ir_dev",    DEFAULT_IR_DEVICE);
    rconfig.theme_idx    = p.getInt("theme",     DEFAULT_THEME_IDX);
    rconfig.idle_anim    = p.getBool("idle_anim",DEFAULT_IDLE_ANIM);
    strlcpy(rconfig.macro_text, p.getString("macro_txt","").c_str(), 80);
    rconfig.macro_hour   = (uint8_t)p.getInt("macro_h",  DEFAULT_MACRO_HOUR);
    rconfig.macro_min    = (uint8_t)p.getInt("macro_m",  DEFAULT_MACRO_MIN);
    rconfig.macro_enabled= p.getBool("macro_en", DEFAULT_MACRO_ENABLED);
    rconfig.tz_offset    = p.getInt("tz_off",    DEFAULT_TZ_OFFSET);

    p.end();
}

inline void saveConfig() {
    Preferences p;
    p.begin("vaultpi", false);

    for (int i = 0; i < 3; i++) {
        char k[8];
        snprintf(k, sizeof(k), "ssid%d", i);
        p.putString(k, rconfig.wifi_ssid[i]);
        snprintf(k, sizeof(k), "pass%d", i);
        p.putString(k, rconfig.wifi_pass[i]);
    }

    normalizeBridgeHost(rconfig.bridge_host, sizeof(rconfig.bridge_host));
    p.putString("bridge_host", rconfig.bridge_host);
    p.putString("bridge_psk",  rconfig.bridge_psk);
    p.putString("nickname",    rconfig.device_nickname);
    p.putInt("poll_sec",  rconfig.poll_sec);
    p.putInt("dim_pwr",   rconfig.dim_sec_power);
    p.putInt("off_pwr",   rconfig.off_sec_power);
    p.putInt("dim_bat",   rconfig.dim_sec_bat);
    p.putInt("off_bat",   rconfig.off_sec_bat);
    p.putInt("go_mode",   rconfig.go_btn_mode);
    p.putBool("pin_en",   rconfig.pin_enabled);
    p.putString("pin_code", rconfig.pin_code);
    p.putInt("spk_vol",    (int)rconfig.spk_volume);
    p.putInt("ir_dev",     rconfig.ir_device);
    p.putInt("theme",      rconfig.theme_idx);
    p.putBool("idle_anim", rconfig.idle_anim);
    p.putString("macro_txt", rconfig.macro_text);
    p.putInt("macro_h",    (int)rconfig.macro_hour);
    p.putInt("macro_m",    (int)rconfig.macro_min);
    p.putBool("macro_en",  rconfig.macro_enabled);
    p.putInt("tz_off",     rconfig.tz_offset);

    p.end();
}

// Notes stored separately so they don't bloat the main config namespace
inline void loadNotes(char notes[][NOTE_LEN], int count) {
    for (int i = 0; i < count; i++) {
        notes[i][0] = '\0';
    }

    bool loadedFromSd = false;
    if (ensureNotesStorage()) {
        for (int i = 0; i < count; i++) {
            char path[32];
            notePath(i, path, sizeof(path));
            File f = SD.open(path, FILE_READ);
            if (!f || f.isDirectory()) {
                if (f) f.close();
                continue;
            }
            String s = f.readString();
            f.close();
            s.replace("\r", "");
            s.replace("\n", " ");
            strlcpy(notes[i], s.c_str(), NOTE_LEN);
            loadedFromSd = true;
        }
    }

    if (loadedFromSd) return;

    Preferences p;
    p.begin("vpnotes", true);
    for (int i = 0; i < count; i++) {
        char k[8]; snprintf(k, sizeof(k), "n%d", i);
        String s = p.getString(k, "");
        strlcpy(notes[i], s.c_str(), NOTE_LEN);
    }
    p.end();
}

inline void saveNote(int idx, const char* text) {
    if (ensureNotesStorage()) {
        char path[32];
        notePath(idx, path, sizeof(path));
        if (SD.exists(path)) {
            SD.remove(path);
        }
        if (text && text[0]) {
            File f = SD.open(path, FILE_WRITE);
            if (f) {
                f.print(text);
                f.close();
            }
        }
    }

    Preferences p;
    p.begin("vpnotes", false);
    char k[8]; snprintf(k, sizeof(k), "n%d", idx);
    p.putString(k, text);
    p.end();
}
