#pragma once

#include <M5Cardputer.h>
#include <time.h>

// Idle scene inspired by M5PORKCHOP:
// - smaller pig
// - drifting clouds
// - night stars
// - rain, birds, and scrolling grass

#define GRASS_Y        91
#define RAIN_DROPS_MAX 25
#define RAIN_Y_MIN     18
#define RAIN_Y_MAX     118

#define STAR_COUNT     15
#define CLOUD_COUNT     3
#define MAX_BIRDS       5

// Smaller pig footprint than the current big ASCII mascot.
#define PIG_LINE_H     12
#define PIG_X          90
#define PIG_Y          28

struct Raindrop {
    int16_t x, y;
    uint8_t spd;
    bool active;
};

struct Star {
    int16_t x, y;
    uint8_t phase;
};

struct Cloud {
    float x;
    int16_t y;
    float spd;
    uint8_t scale;
};

struct Bird {
    float x;
    int16_t y;
    float spd;
    bool active;
    bool rtl;
};

static Raindrop g_rain[RAIN_DROPS_MAX];
static Star     g_stars[STAR_COUNT];
static Cloud    g_clouds[CLOUD_COUNT];
static Bird     g_birds[MAX_BIRDS];

static bool g_rainInited  = false;
static bool g_starsInited = false;
static bool g_cloudsInited = false;
static bool g_birdInited  = false;

static uint8_t       g_grassOff  = 0;
static unsigned long g_grassLast = 0;
static unsigned long g_cloudLast = 0;
static unsigned long g_birdSpawnAt = 0;

inline bool sceneIsNight() {
    time_t now = time(nullptr);
    if (now < 1700000000) return false;
    struct tm ti;
    if (!localtime_r(&now, &ti)) return false;
    return (ti.tm_hour >= 20 || ti.tm_hour < 6);
}

inline int rainCountForMood(int svcDown, int alerts, bool wifiUp) {
    if (!wifiUp)                      return 0;
    if (svcDown >= 3 || alerts >= 4)  return RAIN_DROPS_MAX;
    if (svcDown > 0 || alerts > 0)    return 15;
    return 7;
}

inline void rainInit() {
    for (int i = 0; i < RAIN_DROPS_MAX; i++) {
        g_rain[i].x = (int16_t)random(0, 240);
        g_rain[i].y = (int16_t)random(RAIN_Y_MIN, RAIN_Y_MAX);
        g_rain[i].spd = (uint8_t)random(3, 8);
        g_rain[i].active = (i < 7);
    }
    g_rainInited = true;
}

inline void rainSetCount(int n) {
    if (!g_rainInited) rainInit();
    n = constrain(n, 0, RAIN_DROPS_MAX);
    for (int i = 0; i < RAIN_DROPS_MAX; i++) g_rain[i].active = (i < n);
}

inline void rainStep(uint16_t dropCol, uint16_t bgCol) {
    if (!g_rainInited) rainInit();
    for (int i = 0; i < RAIN_DROPS_MAX; i++) {
        if (!g_rain[i].active) continue;
        M5Cardputer.Display.drawPixel(g_rain[i].x, g_rain[i].y, bgCol);
        M5Cardputer.Display.drawPixel(g_rain[i].x, g_rain[i].y - 1, bgCol);
        g_rain[i].y += g_rain[i].spd;
        if (g_rain[i].y > RAIN_Y_MAX) {
            g_rain[i].y = (int16_t)(RAIN_Y_MIN + random(0, 8));
            g_rain[i].x = (int16_t)random(0, 240);
        }
        M5Cardputer.Display.drawPixel(g_rain[i].x, g_rain[i].y, dropCol);
        M5Cardputer.Display.drawPixel(g_rain[i].x, g_rain[i].y - 1, dropCol);
    }
}

inline void starsInit() {
    for (int i = 0; i < STAR_COUNT; i++) {
        g_stars[i].x = (int16_t)(random(0, 2) ? random(4, 58) : random(180, 236));
        g_stars[i].y = (int16_t)random(20, 68);
        g_stars[i].phase = (uint8_t)random(0, 4);
    }
    g_starsInited = true;
}

inline void drawStars(uint16_t col) {
    if (!sceneIsNight()) return;
    if (!g_starsInited) starsInit();
    unsigned long tick = millis() / 450;
    for (int i = 0; i < STAR_COUNT; i++) {
        uint16_t c = ((tick + g_stars[i].phase) & 3) == 0 ? col : (uint16_t)(col & 0x7BEF);
        M5Cardputer.Display.drawPixel(g_stars[i].x, g_stars[i].y, c);
    }
}

inline void cloudInit() {
    g_clouds[0] = { 20.0f, 18, 0.022f, 1 };
    g_clouds[1] = { 206.0f, 22, -0.016f, 1 };
    g_clouds[2] = { 110.0f, 14, 0.008f, 2 };
    g_cloudsInited = true;
}

inline void drawCloudShape(int cx, int cy, uint16_t col, uint8_t scale) {
    int s = max(1, (int)scale);
    M5Cardputer.Display.fillEllipse(cx,        cy + 2 * s, 12 * s, 6 * s, col);
    M5Cardputer.Display.fillEllipse(cx - 8 * s, cy + 4 * s, 8 * s,  5 * s, col);
    M5Cardputer.Display.fillEllipse(cx + 8 * s, cy + 4 * s, 8 * s,  5 * s, col);
    M5Cardputer.Display.fillEllipse(cx,        cy + 7 * s, 15 * s, 5 * s, col);
}

inline void drawClouds(uint16_t col) {
    if (!g_cloudsInited) cloudInit();
    for (int i = 0; i < CLOUD_COUNT; i++) {
        drawCloudShape((int)g_clouds[i].x, g_clouds[i].y, col, g_clouds[i].scale);
    }
}

inline void cloudStep(uint16_t col, uint16_t bg) {
    if (!g_cloudsInited) cloudInit();
    unsigned long now = millis();
    if (now - g_cloudLast < 120) return;
    g_cloudLast = now;

    for (int i = 0; i < CLOUD_COUNT; i++) {
        drawCloudShape((int)g_clouds[i].x, g_clouds[i].y, bg, g_clouds[i].scale);
        g_clouds[i].x += g_clouds[i].spd;
        if (g_clouds[i].spd > 0.0f && g_clouds[i].x > 262.0f) g_clouds[i].x = -24.0f;
        if (g_clouds[i].spd < 0.0f && g_clouds[i].x < -24.0f) g_clouds[i].x = 262.0f;
        drawCloudShape((int)g_clouds[i].x, g_clouds[i].y, col, g_clouds[i].scale);
    }
}

enum class PigMood : uint8_t {
    Neutral = 0,
    Happy,
    Excited,
    Hunting,
    Sleepy,
    Sad,
    Angry
};

inline PigMood pigMoodForState(int svcDown, int alerts, bool wifiUp) {
    if (!wifiUp) return PigMood::Sleepy;
    if (svcDown >= 2) return PigMood::Angry;
    if (svcDown == 1) return PigMood::Sad;
    if (alerts > 0) return PigMood::Hunting;
    if ((millis() / 6000) & 1) return PigMood::Happy;
    return PigMood::Neutral;
}

inline void drawPig(uint16_t fg, uint16_t bg, int svcDown, int alerts, bool wifiUp) {
    PigMood mood = pigMoodForState(svcDown, alerts, wifiUp);
    const char* row1 = " . . ";
    const char* row2 = "(o00)";
    const char* row3 = "(   )";

    switch (mood) {
        case PigMood::Neutral:
            row1 = " ? ? ";
            row2 = "(o00)";
            row3 = "(   )";
            break;
        case PigMood::Happy:
            row1 = " ^ ^ ";
            row2 = "(^00)";
            row3 = "(   )";
            break;
        case PigMood::Excited:
            row1 = " ^\\^ ";
            row2 = "(O00)";
            row3 = "(   )";
            break;
        case PigMood::Hunting:
            row1 = " | | ";
            row2 = "(=00)";
            row3 = "(   )";
            break;
        case PigMood::Sleepy:
            row1 = " v v ";
            row2 = "(-00)";
            row3 = "(   )";
            break;
        case PigMood::Sad:
            row1 = " . . ";
            row2 = "(T00)";
            row3 = "(   )";
            break;
        case PigMood::Angry:
            row1 = " \\ / ";
            row2 = "(#00)";
            row3 = "(   )";
            break;
    }

    if (mood == PigMood::Neutral || mood == PigMood::Happy) {
        if (((millis() / 2800) % 11) == 0) row2 = "(o --)";
    }

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(fg, bg);
    M5Cardputer.Display.setCursor(PIG_X, PIG_Y);
    M5Cardputer.Display.print(row1);
    M5Cardputer.Display.setCursor(PIG_X, PIG_Y + PIG_LINE_H);
    M5Cardputer.Display.print(row2);
    M5Cardputer.Display.setCursor(PIG_X, PIG_Y + PIG_LINE_H * 2);
    M5Cardputer.Display.print(row3);
}

inline void grassStep() {
    unsigned long now = millis();
    if (now - g_grassLast >= 160) {
        g_grassLast = now;
        g_grassOff = (g_grassOff + 1) % 6;
    }
}

inline void drawGrass(uint16_t fg, uint16_t bg) {
    static const char PAT[] = "/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/";
    M5Cardputer.Display.fillRect(0, GRASS_Y, 240, 16, bg);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(fg, bg);
    M5Cardputer.Display.setCursor(-(int)g_grassOff * 12, GRASS_Y);
    M5Cardputer.Display.print(PAT);
}

inline void birdInit() {
    for (int i = 0; i < MAX_BIRDS; i++) g_birds[i].active = false;
    g_birdSpawnAt = millis() + (unsigned long)random(8000, 16000);
    g_birdInited = true;
}

inline void birdDrawAt(int x, int y, uint16_t col, uint16_t bg, bool erase) {
    uint16_t c = erase ? bg : col;
    M5Cardputer.Display.drawPixel(x - 1, y - 1, c);
    M5Cardputer.Display.drawPixel(x,     y,     c);
    M5Cardputer.Display.drawPixel(x + 1, y - 1, c);
}

inline void birdSpawnFlock(int count, bool rtl) {
    int baseY = (int)random(22, 55);
    float baseX = rtl ? 242.0f : -3.0f;
    float spd = rtl ? -(random(6, 14) * 0.1f + 0.4f)
                    :  (random(6, 14) * 0.1f + 0.4f);
    for (int i = 0; i < MAX_BIRDS; i++) {
        if (count <= 0) break;
        if (!g_birds[i].active) {
            g_birds[i].x = baseX + (rtl ? -i * 9 : i * 9);
            g_birds[i].y = (int16_t)(baseY + random(-4, 4));
            g_birds[i].spd = spd;
            g_birds[i].rtl = rtl;
            g_birds[i].active = true;
            count--;
        }
    }
}

inline void birdStep(uint16_t col, uint16_t bg) {
    if (!g_birdInited) birdInit();
    bool anyActive = false;
    for (int i = 0; i < MAX_BIRDS; i++) {
        if (!g_birds[i].active) continue;
        anyActive = true;
        birdDrawAt((int)g_birds[i].x, g_birds[i].y, col, bg, true);
        g_birds[i].x += g_birds[i].spd;
        bool gone = g_birds[i].rtl ? g_birds[i].x < -4 : g_birds[i].x > 243;
        if (gone) {
            g_birds[i].active = false;
            continue;
        }
        if (g_birds[i].y >= RAIN_Y_MIN && g_birds[i].y < GRASS_Y) {
            birdDrawAt((int)g_birds[i].x, g_birds[i].y, col, bg, false);
        }
    }
    unsigned long now = millis();
    if (!anyActive && now >= g_birdSpawnAt) {
        birdSpawnFlock(random(2, 5), random(0, 2));
        g_birdSpawnAt = now + (unsigned long)random(8000, 16000);
    }
}

inline void pigResetTimers() {
    g_grassLast = millis();
    g_grassOff = 0;
    g_cloudLast = millis();
    g_birdSpawnAt = millis() + (unsigned long)random(8000, 16000);
}

inline void drawSceneBackground(uint16_t bg, uint16_t starCol, uint16_t cloudCol) {
    M5Cardputer.Display.fillRect(0, RAIN_Y_MIN, 240, RAIN_Y_MAX - RAIN_Y_MIN + 4, bg);
    drawStars(starCol);
    drawClouds(cloudCol);
}

inline void drawSceneStatus(const char* line1, const char* line2,
                            uint16_t col, uint16_t bg) {
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(col, bg);
    if (line1 && line1[0]) {
        int px = (240 - (int)strlen(line1) * 6) / 2;
        M5Cardputer.Display.setCursor(max(2, px), 26);
        M5Cardputer.Display.print(line1);
    }
    if (line2 && line2[0]) {
        int px = (240 - (int)strlen(line2) * 6) / 2;
        M5Cardputer.Display.setCursor(max(2, px), 36);
        M5Cardputer.Display.print(line2);
    }
}
