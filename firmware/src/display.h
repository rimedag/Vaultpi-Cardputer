#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Layout — 240 × 135 display
// ---------------------------------------------------------------------------
#define SCREEN_W   240
#define SCREEN_H   135
#define HEADER_H    15   // top bar height (px)
#define FOOTER_H    12   // bottom bar height (px)
#define CONTENT_Y   17   // first content row top  (HEADER_H + 2)
#define CONTENT_H   (SCREEN_H - HEADER_H - FOOTER_H - 2)   // 106 px
#define LINE_H      14   // list row height  — 7 rows fit (7×14 = 98 ≤ 106)
#define VIS          7   // max visible list rows
#define BAR_H        8   // metric progress bar fill height
#define SPARK_MAX   40   // sparkline data points (must match config.h)

// ---------------------------------------------------------------------------
// Colour palette — dark navy + amber accent, RGB565
// ---------------------------------------------------------------------------
#define COL_BG        0x0861   // #080C10  near-black navy
#define COL_BG2       0x1082   // #100818  card/header bg
#define COL_ROW_ALT   0x0C63   // very slight alternate row tint
#define COL_ROW_SEL   0x1A4B   // selection highlight (dark blue)

// Accent — amber used for titles, selected bars, icons
#define COL_AMBER     0xF600   // #f5c000
#define COL_PURPLE    COL_AMBER  // legacy alias

// Header bg
#define COL_HEADER    COL_BG2

// Text
#define COL_TEXT      0xCE79   // #cde0f0  light blue-grey
#define COL_DIM       0x4A69   // #4a5060  muted slate

// Selection
#define COL_SEL_BG    COL_ROW_SEL
#define COL_SEL_FG    0xFFFF   // white on selection

// Status
#define COL_OK        0x2DC6   // #27b854  green
#define COL_ERR       0xF188   // #f04050  red
#define COL_WARN      0xFEC0   // #ffc000  amber-yellow
#define COL_INFO      0x07FF   // #00ffff  cyan

// ---------------------------------------------------------------------------
// PORKCHOP-style theme system — dynamic FG/BG pair
// ---------------------------------------------------------------------------
struct PorkTheme { uint16_t fg; uint16_t bg; const char* name; };
static const PorkTheme THEMES[] = {
    { 0xF92A, 0x0000, "P1NK"  },  // hot pink on black
    { 0x07FF, 0x0000, "CYB3R" },  // cyan on black
    { 0xFDA0, 0x0000, "AMB3R" },  // amber on black
    { 0xFFFF, 0x0000, "GH0ST" },  // white on black
    { 0xF800, 0x0000, "BL00D" },  // red on black
    { 0x07E0, 0x0000, "L1ME"  },  // green on black
};
static const int THEME_COUNT = 6;
extern int g_themeIdx;
inline uint16_t thFG()       { return THEMES[g_themeIdx % THEME_COUNT].fg; }
inline uint16_t thBG()       { return 0x0000; }   // always black
inline const char* thName()  { return THEMES[g_themeIdx % THEME_COUNT].name; }

// ---------------------------------------------------------------------------
// Section carousel colour pairs [bg, accent]  (9 sections)
//  0=Dashboard  1=Services  2=Actions  3=Alerts  4=Activity
//  5=IR Remote  6=Network   7=Settings 8=Device
// ---------------------------------------------------------------------------
static const uint16_t SEC_BG[9] = {
    0x0843,  // Dashboard — deep blue
    0x0422,  // Services  — very dark green
    0x2800,  // Actions   — dark red-orange
    0x3000,  // Alerts    — dark magenta
    0x2009,  // Activity  — dark purple
    0x0244,  // IR Remote — dark teal
    0x0046,  // Network   — dark cyan
    0x0425,  // Settings  — dark teal-green
    0x2803,  // Device    — dark violet
};
static const uint16_t SEC_ACC[9] = {
    0x4DFF,  // Dashboard — bright blue
    0x2FE4,  // Services  — mint green
    0xFC80,  // Actions   — orange
    0xF81F,  // Alerts    — magenta
    0xA81F,  // Activity  — violet
    0x07EF,  // IR Remote — cyan-teal
    0x07FF,  // Network   — cyan
    0x2FEF,  // Settings  — teal
    0xC81F,  // Device    — purple-pink
};

// ---------------------------------------------------------------------------
// Status helpers
// ---------------------------------------------------------------------------
inline uint16_t statusColor(const char* s) {
    if (!s) return COL_DIM;
    if (!strcmp(s,"running") || !strcmp(s,"online") || !strcmp(s,"success")) return COL_OK;
    if (!strcmp(s,"error")   || !strcmp(s,"offline") || !strcmp(s,"failed")) return COL_ERR;
    if (!strcmp(s,"stopped") || !strcmp(s,"degraded")|| !strcmp(s,"warning"))return COL_WARN;
    return COL_DIM;
}
inline uint16_t eventColor(const char* t) {
    if (!t) return COL_DIM;
    if (!strcmp(t,"success")) return COL_OK;
    if (!strcmp(t,"error"))   return COL_ERR;
    if (!strcmp(t,"warning")) return COL_WARN;
    if (!strcmp(t,"info"))    return COL_INFO;
    return COL_DIM;
}
inline const char* statusTag(const char* s) {
    if (!s)                      return " ?? ";
    if (!strcmp(s,"running"))    return " RUN";
    if (!strcmp(s,"online"))     return "  UP";
    if (!strcmp(s,"success"))    return "  OK";
    if (!strcmp(s,"error"))      return " ERR";
    if (!strcmp(s,"failed"))     return "FAIL";
    if (!strcmp(s,"offline"))    return "DOWN";
    if (!strcmp(s,"stopped"))    return "STOP";
    if (!strcmp(s,"degraded"))   return "DEGR";
    if (!strcmp(s,"pending"))    return "WAIT";
    return " ?? ";
}
inline uint16_t alertLevelColor(const char* lvl) {
    if (!lvl)                  return COL_DIM;
    if (!strcmp(lvl,"error"))  return COL_ERR;
    if (!strcmp(lvl,"warning"))return COL_WARN;
    return COL_INFO;
}

// ---------------------------------------------------------------------------
// Battery helper — used in header and main menu
// ---------------------------------------------------------------------------
inline int batteryLevelPercent() {
    int bat = M5Cardputer.Power.getBatteryLevel();
    if (bat >= 0 && bat <= 100) return bat;

    int mv = M5Cardputer.Power.getBatteryVoltage();
    if (mv <= 0) return -1;

    int level = (mv - 3350) * 100 / (4150 - 3350);
    if (level < 0) return 0;
    if (level > 100) return 100;
    return level;
}

inline int batteryVoltageMv() {
    return M5Cardputer.Power.getBatteryVoltage();
}

// Set by updateUsbState() in main.cpp via AXP2101 VBUS IRQ tracking.
// Using IRQ status registers is the only reliable method: isCharging() misses full-charge
// standby, VBUS_GOOD (reg 0x00 bit 5) is deasserted by AXP2101 at full charge on this hardware,
// and getChargeStatus() returns standby(0) for both USB+full and idle-on-battery.
extern bool g_usbConnected;

inline bool batteryIsCharging() {
    return g_usbConnected;
}

inline bool batteryChargeKnown() {
    return M5Cardputer.Power.isCharging() != m5::Power_Class::charge_unknown;
}

inline void formatBatteryStatus(char* out, size_t outLen) {
    int bat = batteryLevelPercent();
    int mv = batteryVoltageMv();
    bool chg = batteryIsCharging();
    bool known = batteryChargeKnown();

    if (bat >= 0 && mv > 0) {
        snprintf(out, outLen, "%d%% %s %dmV", bat, known ? (chg ? "charging" : "battery") : "state?", mv);
    } else if (bat >= 0) {
        snprintf(out, outLen, "%d%% %s", bat, known ? (chg ? "charging" : "battery") : "state?");
    } else if (mv > 0) {
        snprintf(out, outLen, "%dmV %s", mv, known ? (chg ? "charging" : "battery") : "state?");
    } else {
        strlcpy(out, "n/a", outLen);
    }
}

inline void drawWifiIcon(int x, int y, uint16_t bgCol) {
    bool ok = WiFi.status() == WL_CONNECTED;
    int bars = 0;
    if (ok) {
        int rssi = WiFi.RSSI();
        bars = rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : 1;
    }
    uint16_t col = ok ? (bars >= 3 ? COL_OK : bars == 2 ? COL_WARN : COL_ERR) : COL_DIM;
    M5Cardputer.Display.fillRect(x, y, 16, 8, bgCol);
    for (int i = 0; i < 4; i++) {
        int h = 2 + i * 2;
        int bx = x + i * 4;
        int by = y + 8 - h;
        M5Cardputer.Display.fillRect(bx, by, 3, h, (ok && i < bars) ? col : COL_DIM);
    }
}

inline void drawBattery(int x, int y, uint16_t bgCol) {
    int  bat = batteryLevelPercent();
    bool chg = batteryIsCharging();
    char buf[8];
    if      (bat>=0) snprintf(buf, sizeof(buf), chg ? "+%d" : "%d%%", bat);
    else             strlcpy(buf, "---",  sizeof(buf));

    uint16_t col = chg    ? COL_INFO :
                   bat>50 ? COL_OK   :
                   bat>20 ? COL_WARN : COL_ERR;
    M5Cardputer.Display.fillRect(x, y, 37, 9, bgCol);
    M5Cardputer.Display.drawRect(x, y + 1, 9, 7, col);
    M5Cardputer.Display.fillRect(x + 9, y + 3, 2, 3, col);
    if (bat >= 0) {
        int fill = constrain((bat * 7 + 99) / 100, 1, 7);
        M5Cardputer.Display.fillRect(x + 1, y + 2, fill, 5, col);
    }
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(col, bgCol);
    M5Cardputer.Display.setCursor(x + 13, y);
    M5Cardputer.Display.print(buf);
}

// ---------------------------------------------------------------------------
// Header bar — amber title left, optional info centre, battery always right
// ---------------------------------------------------------------------------
inline void drawHeader(const char* title, const char* right = nullptr) {
    uint16_t hbg = thBG();
    uint16_t hfg = thFG();
    M5Cardputer.Display.fillRect(0, 0, SCREEN_W, HEADER_H, hbg);
    M5Cardputer.Display.drawFastHLine(0, HEADER_H - 1, SCREEN_W, hfg);

    drawWifiIcon(SCREEN_W - 58, 4, hbg);
    drawBattery(SCREEN_W - 37, 4, hbg);

    if (right && right[0]) {
        char r[15]; strlcpy(r, right, sizeof(r));
        int rx = SCREEN_W - 62 - (int)strlen(r) * 6 - 5;
        if (rx > 60) {
            M5Cardputer.Display.setTextColor(COL_DIM, hbg);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setCursor(rx, 4);
            M5Cardputer.Display.print(r);
        }
    }

    M5Cardputer.Display.setTextColor(hfg, hbg);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(4, 4);
    M5Cardputer.Display.print(title);
}

// ---------------------------------------------------------------------------
// Footer bar
// ---------------------------------------------------------------------------
inline void drawFooter(const char* hints, const char* right = nullptr) {
    int fy = SCREEN_H - FOOTER_H;
    uint16_t fbg = thBG();
    M5Cardputer.Display.fillRect(0, fy, SCREEN_W, FOOTER_H, fbg);
    M5Cardputer.Display.drawFastHLine(0, fy, SCREEN_W, thFG());
    M5Cardputer.Display.setTextColor(COL_DIM, fbg);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(4, fy + 2);
    M5Cardputer.Display.print(hints);
    if (right && right[0]) {
        M5Cardputer.Display.setTextColor(thFG(), fbg);
        M5Cardputer.Display.setCursor(SCREEN_W - (int)strlen(right) * 6 - 4, fy + 2);
        M5Cardputer.Display.print(right);
    }
}

// ---------------------------------------------------------------------------
// Rounded modal box — PORKCHOP style
// ---------------------------------------------------------------------------
inline void drawModal(int x, int y, int w, int h) {
    M5Cardputer.Display.fillRoundRect(x, y, w, h, 4, thBG());
    M5Cardputer.Display.drawRoundRect(x, y, w, h, 4, thFG());
    M5Cardputer.Display.drawRoundRect(x+1, y+1, w-2, h-2, 3, thFG());
}

// ---------------------------------------------------------------------------
// Scroll caret indicators  ^/v at right edge
// ---------------------------------------------------------------------------
inline void drawScrollCarets(bool up, bool down) {
    uint16_t col = thFG();
    M5Cardputer.Display.setTextSize(1);
    if (up) {
        M5Cardputer.Display.setTextColor(col, thBG());
        M5Cardputer.Display.setCursor(SCREEN_W - 10, CONTENT_Y + 2);
        M5Cardputer.Display.print("^");
    }
    if (down) {
        M5Cardputer.Display.setTextColor(col, thBG());
        M5Cardputer.Display.setCursor(SCREEN_W - 10, SCREEN_H - FOOTER_H - 10);
        M5Cardputer.Display.print("v");
    }
}

// ---------------------------------------------------------------------------
// Metric progress bar — label | bar | value
// ---------------------------------------------------------------------------
inline void drawBar(int y, const char* label, int percent,
                    const char* value, uint16_t barCol,
                    int bx = 34, int bw = 130) {
    M5Cardputer.Display.fillRect(0, y, SCREEN_W, LINE_H, COL_BG);
    M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(4, y + 3);
    M5Cardputer.Display.print(label);
    // track
    M5Cardputer.Display.fillRect(bx, y + 3, bw, BAR_H, COL_BG2);
    M5Cardputer.Display.drawRect(bx, y + 3, bw, BAR_H, COL_DIM);
    // fill
    int fill = constrain((percent * bw) / 100, 0, bw);
    if (fill > 0) M5Cardputer.Display.fillRect(bx, y + 3, fill, BAR_H, barCol);
    // value
    M5Cardputer.Display.setTextColor(COL_TEXT, COL_BG);
    M5Cardputer.Display.setCursor(SCREEN_W - (int)strlen(value) * 6 - 4, y + 3);
    M5Cardputer.Display.print(value);
}

// ---------------------------------------------------------------------------
// Sparkline — connects data[0..count-1] as a small line chart
// data is oldest→newest, values 0-100 representing percentage
// ---------------------------------------------------------------------------
inline void drawSparkline(int x, int y, int w, int h,
                          const uint8_t* data, int count,
                          uint16_t lineCol, uint16_t bgCol = COL_BG) {
    M5Cardputer.Display.fillRect(x, y, w, h, bgCol);
    if (count < 2) return;
    int n     = min(count, w);
    int start = count - n;
    for (int i = 1; i < n; i++) {
        uint8_t v0 = data[start + i - 1];
        uint8_t v1 = data[start + i];
        int y0 = y + h - 1 - ((int)v0 * (h - 1) / 100);
        int y1 = y + h - 1 - ((int)v1 * (h - 1) / 100);
        y0 = constrain(y0, y, y + h - 1);
        y1 = constrain(y1, y, y + h - 1);
        M5Cardputer.Display.drawLine(x + i - 1, y0, x + i, y1, lineCol);
    }
}

// ---------------------------------------------------------------------------
// Key-value row
// ---------------------------------------------------------------------------
inline void drawKV(int y, const char* label, const char* value,
                   uint16_t valCol = COL_TEXT) {
    M5Cardputer.Display.fillRect(0, y, SCREEN_W, LINE_H, COL_BG);
    M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(4, y + 3);
    M5Cardputer.Display.print(label);
    M5Cardputer.Display.setTextColor(valCol, COL_BG);
    M5Cardputer.Display.setCursor(SCREEN_W - (int)strlen(value) * 6 - 4, y + 3);
    M5Cardputer.Display.print(value);
}

// ---------------------------------------------------------------------------
// Horizontal rule
// ---------------------------------------------------------------------------
inline void drawRule(int y) {
    M5Cardputer.Display.drawFastHLine(4, y, SCREEN_W - 8, COL_DIM);
}

// ---------------------------------------------------------------------------
// List item — alternating rows, amber left bar on selection, optional tag
// ---------------------------------------------------------------------------
// PORKCHOP-style: selected item gets full-width thFG() fill, text inverted to thBG()
inline void drawListItem(int y, const char* text, bool selected,
                         const char* tag = nullptr,
                         uint16_t tagCol = COL_DIM) {
    int rowIdx   = (y - CONTENT_Y) / LINE_H;
    uint16_t bg  = selected ? thFG() : (rowIdx % 2 == 0 ? 0x0821 : thBG());
    uint16_t fg  = selected ? thBG() : COL_TEXT;

    M5Cardputer.Display.fillRect(0, y, SCREEN_W, LINE_H, bg);

    M5Cardputer.Display.setTextColor(selected ? thBG() : COL_DIM, bg);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(5, y + 3);
    M5Cardputer.Display.print(selected ? ">" : " ");

    int maxChars = tag ? 25 : 33;
    char buf[40]; strncpy(buf, text, maxChars); buf[maxChars] = '\0';
    M5Cardputer.Display.setTextColor(fg, bg);
    M5Cardputer.Display.setCursor(14, y + 3);
    M5Cardputer.Display.print(buf);

    if (tag) {
        M5Cardputer.Display.setTextColor(selected ? thBG() : tagCol, bg);
        M5Cardputer.Display.setCursor(SCREEN_W - (int)strlen(tag) * 6 - 4, y + 3);
        M5Cardputer.Display.print(tag);
    }
}

// ---------------------------------------------------------------------------
// Status dot (4×4 filled square)
// ---------------------------------------------------------------------------
inline void drawStatusDot(int x, int y, uint16_t col) {
    M5Cardputer.Display.fillRect(x, y, 4, 4, col);
}

// ---------------------------------------------------------------------------
// Boot / offline splash — shown before full UI is ready
// ---------------------------------------------------------------------------
inline void drawMessage(const char* line1, const char* line2 = nullptr,
                        uint16_t col = COL_WARN) {
    M5Cardputer.Display.fillScreen(COL_BG);
    M5Cardputer.Display.fillRect(0, 0,          SCREEN_W, 3, COL_AMBER);
    M5Cardputer.Display.fillRect(0, SCREEN_H-3, SCREEN_W, 3, COL_AMBER);

    int y1 = line2 ? 36 : 55;
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(COL_AMBER, COL_BG);
    int w1 = 7 * 12;
    M5Cardputer.Display.setCursor((SCREEN_W - w1) / 2, y1 - 18);
    M5Cardputer.Display.print("VAULTPI");

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(col, COL_BG);
    M5Cardputer.Display.setCursor((SCREEN_W - (int)strlen(line1) * 6) / 2, y1 + 4);
    M5Cardputer.Display.print(line1);

    if (line2) {
        M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
        M5Cardputer.Display.setCursor((SCREEN_W - (int)strlen(line2) * 6) / 2, y1 + 18);
        M5Cardputer.Display.print(line2);
    }
}
