/*
 * VaultPi Cardputer v2.2 — RIMEDAG edition
 * Target: M5Stack Cardputer Adv (ESP32-S3, 240×135, QWERTY)
 *
 * Navigation:
 *   W/S / arrows / ;.   — up/down
 *   Enter / D//         — select
 *   Del / A/,           — back / cancel
 *
 * Global shortcuts (any screen, not in edit mode):
 *   r  refresh    b  Gitea backup    g  Gitea sync-android
 *   x  shutdown   l  Activity Log   t  Notes
 *   n  Net scan   u  OTA check (Device screen)   f  Favorites / pin
 *   1-9,0 direct jump to menu item
 *
 * Main menu (11 items):
 *   Dashboard · Services · Actions · Alerts · Activity · Gitea
 *   IR Remote · Network  · Settings · Device · USB Macro
 */

#include <M5Cardputer.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <time.h>
#include "config.h"
#include "settings_store.h"
#include "display.h"
#include "network.h"
#include "audio.h"
#include "ir_codes.h"
#include "ota.h"
#include "pig.h"

char g_bridge_host[80] = BRIDGE_HOST;
char g_bridge_psk[64]  = BRIDGE_PSK;
int  g_themeIdx        = DEFAULT_THEME_IDX;

static USBHIDKeyboard KbdHID;
static WebServer deviceServer(80);
static bool deviceWebStarted = false;
static bool otaUploadStarted = false;
static bool otaUploadOk = false;
static bool otaUploadSeen = false;

// ─── HID keycodes (orange arrow keys) ────────────────────────────────────────
#define HID_UP    0x52
#define HID_DOWN  0x51
#define HID_LEFT  0x50
#define HID_RIGHT 0x4F
#define HID_ESC   0x29

// ─────────────────────────────────────────────────────────────────────────────
// Screen enum
// ─────────────────────────────────────────────────────────────────────────────
enum class Screen {
    Main,
    Dashboard, Services, SvcAction,
    Actions,   Favorites, Log,      Alerts,
    Gitea,     IRRemote,  NetScan,  Notes,
    Settings,  Info,      USBMacro, Browser,
    Terminal,
    COUNT
};
static const int N_SCREENS = (int)Screen::COUNT;

// Main menu (menu-first home screen)
static const char*  MAIN_LABELS[]   = {
    "DASHBOARD","SERVICES","ACTIONS","ALERTS","ACTIVITY",
    "GITEA","IR REMOTE","NETWORK","SETTINGS","DEVICE","USB MACRO","WEB BROWSER","TERMINAL"
};
static const Screen MAIN_TARGETS[]  = {
    Screen::Dashboard, Screen::Services, Screen::Actions,
    Screen::Alerts,    Screen::Log,
    Screen::Gitea,     Screen::IRRemote,  Screen::NetScan,
    Screen::Settings,   Screen::Info,
    Screen::USBMacro,  Screen::Browser,   Screen::Terminal
};
static const int MAIN_COUNT = 13;
static bool      g_menuOpen = true;   // menu is always the home screen
static bool      offlineMode = false;  // stay offline until user connects manually
static bool      bridgeOnline = false;  // only push mirror/state when verified reachable

// ─────────────────────────────────────────────────────────────────────────────
// Data structures
// ─────────────────────────────────────────────────────────────────────────────
struct DashboardData {
    char hostname[32]  = "vaultpi";
    char uptime[32]    = "n/a";
    int  cpu=0, ram=0, disk=0, temp=0;
    char load[16]      = "n/a";
    int  svcTotal=0, svcUp=0, svcDown=0;
    char lastMsg[80]   = "";
    char lastType[16]  = "info";
    int  sdErrors      = 0;
    bool fresh         = false;
};

struct ServiceItem {
    char id[12]; char name[48]; char kind[8]; char status[12];
};
struct ActionItem {
    char id[48]; char label[48]; char kind[12]; char status[16];
    char runPath[96];
};
struct LogItem {
    char message[80]; char type[16];
};
struct AlertItem {
    char message[96]; char level[12]; bool read;
};
struct NetAP {
    char ssid[33]; int rssi; uint8_t channel; bool open;
};
struct GiteaHeatCell {
    uint8_t level = 0;
};
struct GiteaData {
    char username[32]  = "n/a";
    char fullName[48]  = "";
    char htmlUrl[96]   = "";
    char serverVer[16] = "";
    int  followers     = 0;
    int  following     = 0;
    int  stars         = 0;
    int  repos         = 0;
    int  total         = 0;
    int  activeDays    = 0;
    int  peak          = 0;
    bool fresh         = false;
    GiteaHeatCell cells[84];
};

static const int MAX_SVC = 20, MAX_ACT = 20, MAX_LOG = 12;
static const int MAX_ALERTS = 16, MAX_NET = 24;

static DashboardData dash;
static ServiceItem   services[MAX_SVC]; static int svcCount = 0;
static ActionItem    actions[MAX_ACT];  static int actCount = 0;
static LogItem       logItems[MAX_LOG]; static int logCount = 0;
static AlertItem     alerts[MAX_ALERTS];static int alertCount = 0, unreadCount = 0;
static NetAP         netAPs[MAX_NET];   static int netAPCount = 0;
static GiteaData     gitea;
struct FavoriteItem {
    char kind[8];   // service | action
    char id[48];
    char label[48];
};
static const int MAX_FAV = 8;
static FavoriteItem favorites[MAX_FAV];
static int favoriteCount = 0;

// Fetch status tracking (0=never tried, 200=ok, -1=no wifi, other=error)
static int svcFetchStatus  = 0;
static int actFetchStatus  = 0;
static int logFetchStatus  = 0;
static int alertFetchStatus= 0;

// Sparklines — oldest→newest, 0-100 percent values
static uint8_t sparkCpu[SPARK_MAX]  = {};
static uint8_t sparkRam[SPARK_MAX]  = {};
static uint8_t sparkDisk[SPARK_MAX] = {};
static uint8_t sparkTemp[SPARK_MAX] = {};
static int     sparkCount = 0;

// Notes
static char notes[MAX_NOTES][NOTE_LEN] = {};
static bool notesLoaded = false;

// OTA
static OTAInfo otaInfo;
static bool otaChecked = false;

// ─────────────────────────────────────────────────────────────────────────────
// Navigation state
// ─────────────────────────────────────────────────────────────────────────────
static Screen current   = Screen::Main;
static int    cursors[N_SCREENS] = {};
static int    scrolls[N_SCREENS] = {};
static Screen navHistory[8];
static int    navDepth    = 0;
static int    selectedSvc = -1;

static const char* SVC_ACTIONS[] = { "Start", "Stop", "Restart", "Back" };
static const int   SVC_ACT_COUNT = 4;

// IR remote grid cursor
static int irRow = 0, irCol = 0;

// Net scan state
static bool netScanning     = false;
static bool netConnectMode  = false;   // password entry for a chosen AP
static char netConnectBuf[64] = "";
static int  netConnectIdx   = -1;

// Notes edit state
static int  noteEditIdx = -1;
static bool noteEditMode = false;
static char noteEditBuf[NOTE_LEN] = "";

// ─────────────────────────────────────────────────────────────────────────────
// Settings fields
// ─────────────────────────────────────────────────────────────────────────────
enum class SF {
    Nickname=0,
    W1Ssid, W1Pass, W2Ssid, W2Pass, W3Ssid, W3Pass,
    BridgeHost, BridgePsk,
    PollSec,
    DimPwr, OffPwr,
    DimBat, OffBat,
    GoBtnMode,
    PinEnable, PinCode, SpeakerVol, IRDevice,
    Theme, IdleAnim,
    Save, Discard,
    COUNT
};
static const int SF_COUNT   = (int)SF::COUNT;
static const int SF_SAVE    = (int)SF::Save;
static const int SF_DISCARD = (int)SF::Discard;

static bool        settingsEditMode  = false;
static int         settingsEditField = 0;
static char        settingsEditBuf[80] = "";
static RuntimeConfig pendingConfig;

const char* sfLabel(int i) {
    static const char* L[] = {
        "Nickname",
        "WiFi1 SSID","WiFi1 Pass",
        "WiFi2 SSID","WiFi2 Pass",
        "WiFi3 SSID","WiFi3 Pass",
        "Control Host","Password",
        "Poll (sec)",
        "Dim (plugged)","Off (plugged)",
        "Dim (battery)","Off (battery)",
        "GO Button",
        "PIN Lock","PIN Code","Speaker Vol","IR Device",
        "Theme","Idle Anim",
        "[Save & Apply]","[Discard]"
    };
    return (i>=0 && i<SF_COUNT) ? L[i] : "?";
}
bool sfIsSecret(int i)  { return i==(int)SF::W1Pass||i==(int)SF::W2Pass||i==(int)SF::W3Pass||i==(int)SF::BridgePsk||i==(int)SF::PinCode; }
bool sfIsAction(int i)  { return i>=SF_SAVE; }
bool sfIsToggle(int i)  { return i==(int)SF::PinEnable||i==(int)SF::IdleAnim; }
bool sfIsCycle(int i)   { return i==(int)SF::IRDevice||i==(int)SF::Theme||i==(int)SF::GoBtnMode; }

void sfGet(int i, char* out, int maxLen) {
    switch ((SF)i) {
        case SF::Nickname:  strlcpy(out, pendingConfig.device_nickname, maxLen); break;
        case SF::W1Ssid:    strlcpy(out, pendingConfig.wifi_ssid[0], maxLen); break;
        case SF::W1Pass:    strlcpy(out, pendingConfig.wifi_pass[0], maxLen); break;
        case SF::W2Ssid:    strlcpy(out, pendingConfig.wifi_ssid[1], maxLen); break;
        case SF::W2Pass:    strlcpy(out, pendingConfig.wifi_pass[1], maxLen); break;
        case SF::W3Ssid:    strlcpy(out, pendingConfig.wifi_ssid[2], maxLen); break;
        case SF::W3Pass:    strlcpy(out, pendingConfig.wifi_pass[2], maxLen); break;
        case SF::BridgeHost:strlcpy(out, pendingConfig.bridge_host, maxLen);  break;
        case SF::BridgePsk: strlcpy(out, pendingConfig.bridge_psk,  maxLen);  break;
        case SF::PollSec:   snprintf(out, maxLen, "%d", pendingConfig.poll_sec); break;
        case SF::DimPwr:    if (!pendingConfig.dim_sec_power) strlcpy(out,"Never",maxLen); else snprintf(out,maxLen,"%d",pendingConfig.dim_sec_power); break;
        case SF::OffPwr:    if (!pendingConfig.off_sec_power) strlcpy(out,"Never",maxLen); else snprintf(out,maxLen,"%d",pendingConfig.off_sec_power); break;
        case SF::DimBat:    if (!pendingConfig.dim_sec_bat)   strlcpy(out,"Never",maxLen); else snprintf(out,maxLen,"%d",pendingConfig.dim_sec_bat);   break;
        case SF::OffBat:    if (!pendingConfig.off_sec_bat)   strlcpy(out,"Never",maxLen); else snprintf(out,maxLen,"%d",pendingConfig.off_sec_bat);   break;
        case SF::GoBtnMode: strlcpy(out, pendingConfig.go_btn_mode==1?"Lock":"Default", maxLen); break;
        case SF::PinEnable: strlcpy(out, pendingConfig.pin_enabled?"YES":"NO", maxLen); break;
        case SF::PinCode:   strlcpy(out, pendingConfig.pin_code, maxLen);        break;
        case SF::SpeakerVol:snprintf(out, maxLen, "%d", pendingConfig.spk_volume); break;
        case SF::IRDevice:  strlcpy(out, IR_PROFILES[pendingConfig.ir_device % IR_PROFILE_COUNT].name, maxLen); break;
        case SF::Theme:     strlcpy(out, THEMES[pendingConfig.theme_idx % THEME_COUNT].name, maxLen); break;
        case SF::IdleAnim:  strlcpy(out, pendingConfig.idle_anim ? "ON" : "OFF", maxLen); break;
        default: out[0]='\0'; break;
    }
}
void sfSet(int i, const char* val) {
    switch ((SF)i) {
        case SF::Nickname: {
            strlcpy(pendingConfig.device_nickname, val, sizeof(pendingConfig.device_nickname));
            if (!pendingConfig.device_nickname[0]) {
                strlcpy(pendingConfig.device_nickname, DEFAULT_DEVICE_NICKNAME, sizeof(pendingConfig.device_nickname));
            }
            break;
        }
        case SF::W1Ssid:    strlcpy(pendingConfig.wifi_ssid[0], val, 64); break;
        case SF::W1Pass:    strlcpy(pendingConfig.wifi_pass[0], val, 64); break;
        case SF::W2Ssid:    strlcpy(pendingConfig.wifi_ssid[1], val, 64); break;
        case SF::W2Pass:    strlcpy(pendingConfig.wifi_pass[1], val, 64); break;
        case SF::W3Ssid:    strlcpy(pendingConfig.wifi_ssid[2], val, 64); break;
        case SF::W3Pass:    strlcpy(pendingConfig.wifi_pass[2], val, 64); break;
        case SF::BridgeHost:strlcpy(pendingConfig.bridge_host, val, 80);  break;
        case SF::BridgePsk: strlcpy(pendingConfig.bridge_psk,  val, 64);  break;
        case SF::PollSec:   pendingConfig.poll_sec = max(1, atoi(val)); break;
        case SF::DimPwr: { int v=atoi(val); pendingConfig.dim_sec_power=(v<=0)?0:max(5,v); break; }
        case SF::OffPwr: { int v=atoi(val); pendingConfig.off_sec_power=(v<=0)?0:max(5,v); break; }
        case SF::DimBat: { int v=atoi(val); pendingConfig.dim_sec_bat  =(v<=0)?0:max(5,v); break; }
        case SF::OffBat: { int v=atoi(val); pendingConfig.off_sec_bat  =(v<=0)?0:max(5,v); break; }
        case SF::PinCode: {
            // only accept 4-digit input
            int l = strlen(val);
            if (l == 4) {
                bool allDigits = true;
                for (int k=0;k<4;k++) if (val[k]<'0'||val[k]>'9') { allDigits=false; break; }
                if (allDigits) strlcpy(pendingConfig.pin_code, val, 5);
            }
            break;
        }
        case SF::SpeakerVol:pendingConfig.spk_volume=(uint8_t)constrain(atoi(val),0,255); break;
        default: break;
    }
}
// sfSet overloads for cycle fields (called without editing buf)
void sfCycle(int i) {
    switch ((SF)i) {
        case SF::IRDevice:  pendingConfig.ir_device  = (pendingConfig.ir_device +1) % IR_PROFILE_COUNT; break;
        case SF::Theme:     pendingConfig.theme_idx   = (pendingConfig.theme_idx  +1) % THEME_COUNT;    break;
        case SF::GoBtnMode: pendingConfig.go_btn_mode = (pendingConfig.go_btn_mode+1) % 2;              break;
        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Feedback overlay
// ─────────────────────────────────────────────────────────────────────────────
enum class FB { None, Pending, OK, Err };
static FB            fb      = FB::None;
static char          fbMsg[128] = "";
static unsigned long fbUntil = 0;
static bool          confirmPending = false;

// ─────────────────────────────────────────────────────────────────────────────
// Timing
// ─────────────────────────────────────────────────────────────────────────────
static unsigned long lastPoll       = 0;
static unsigned long lastKey        = 0;
static unsigned long lastStatePush  = 0;
static unsigned long lastWifiRetry   = 0;
static unsigned long lastBridgeRetry = 0;
static unsigned long lastAlertPoll  = 0;
static unsigned long lastRain       = 0;
static bool          dimmed         = false;
static bool          screenOff      = false;
bool                 g_usbConnected = false;  // updated by updateUsbState() via AXP2101 VBUS IRQs

// Web Browser screen state
#define BR_MAX_LINES 80
#define BR_LINE_LEN  39
#define BR_VIS       7
#define BR_MAX_LINKS 24
#define BR_LINK_TITLE_LEN 34
#define BR_LINK_URL_LEN   96
static char  browserLines[BR_MAX_LINES][BR_LINE_LEN + 1];
static char  browserLinkTitles[BR_MAX_LINKS][BR_LINK_TITLE_LEN + 1];
static char  browserLinkUrls[BR_MAX_LINKS][BR_LINK_URL_LEN + 1];
static int   browserLineCount = 0;
static int   browserScroll    = 0;
static int   browserLinkCount = 0;
static int   browserLinkCursor = 0;
static int   browserLinkScroll = 0;
static bool  browserUrlMode   = true;    // true = URL entry bar shown
static bool  browserBookmarkMode = false;
static bool  browserLinkMode = false;
static bool  browserLoading   = false;
static char  browserUrlBuf[80]  = "";
static char  browserFetchedUrl[80] = "";
static char  browserError[64]   = "";
#define BROWSER_HIST_MAX 8
static char  browserUrlHistory[BROWSER_HIST_MAX][80];
static int   browserHistoryDepth = 0;
static bool  browserNavBack      = false;

// ── Terminal screen ────────────────────────────────────────────────────────
#define TERM_MAX_LINES  100
#define TERM_LINE_LEN    38
#define TERM_VIS          6
static char  termLines[TERM_MAX_LINES][TERM_LINE_LEN + 1];
static int   termLineCount = 0;
static int   termScroll    = 0;
static char  termCmdBuf[80] = "";
static bool  termCmdMode   = true;
static bool  termBusy      = false;
static const char* BROWSER_BM_LABELS[] = { "DuckDuckGo Lite", "Hacker News", "Wikipedia", "Back" };
static const char* BROWSER_BM_URLS[] = {
    "https://lite.duckduckgo.com/lite/",
    "https://news.ycombinator.com/",
    "https://en.wikipedia.org/",
    ""
};
static const int BROWSER_BM_COUNT = 4;

// USB Macro state
static bool macroFired    = false;   // fired once per armed session
static bool ntpSynced     = false;
static int  macroEditField = 0;      // 0=text 1=hour 2=min 3=tz 4=enable 5=arm
static bool macroEditMode  = false;
static bool macroEditFresh = false;
static char macroEditBuf[80] = "";

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────────────────────
void navigate(Screen s);
void goBack();
void escapeToMain();
void fetchForScreen(Screen s);
void fetchDash();
void fetchServices();
void fetchActions();
void fetchLog();
void fetchAlerts();
void fetchGitea();
void loadFavorites();
void saveFavorites();
bool toggleFavorite(const char* kind, const char* id, const char* label);
bool isFavorite(const char* kind, const char* id);
const ServiceItem* findServiceById(const char* id);
const ActionItem* findActionById(const char* id);
void executeActionItem(const ActionItem& a);
void drawScreen();
void drawMain();
void drawIdleScene(bool fullRepaint);
void drawDashboard();
void drawServices();
void drawSvcAction();
void drawActions();
void drawFavorites();
void drawLog();
void drawAlerts();
void drawGitea();
void drawIRRemote();
void drawNetScan();
void drawNotes();
void drawSettings();
void drawSettingsEditBar();
void drawInfo();
void drawUSBMacro();
void drawMainMenu();
void drawPinEntry(const char* entered, bool shake);
void runPinGate();
void checkUsbMacro();
void pushScreenState();
void fetchBrowserPage();
void drawBrowser();
void drawTerminal();
void fetchTerminalExec();
void drawBrowserUrlBar();
void browserScrollBy(int delta);
void browserScrollTo(int pos);
void browserLinkMove(int delta);
void openBrowserLink();
void showFb(FB state, const char* msg, unsigned long dur = 3000);
void drawFb();
void handleChar(char c);
void handleHid(uint8_t hid);
void onEnter();
bool parseMacroTimeInput(const char* input, uint8_t& hour, uint8_t& minute);
void onUp();
void onDown();
void onLeft();
void onRight();
void handleDel();
void wakeDisplay();
void tryWifiReconnect();
void ensureDeviceWebServer();
void serviceDeviceWebServer();
bool retryBridgeHost(bool redrawCurrent = true);
void handleDeviceRoot();
void handleDeviceApi();
void handleDeviceUpdate();
void handleDeviceUpdateUpload();
void handleDeviceRetry();
void handleDeviceReboot();
int& cur();
int& scr();
void clampScroll(int count);
void sparkPush(uint8_t* buf, uint8_t val);
const char* fetchErrMsg(int status);
void drawSectionIcon(int idx, int cx, int cy, uint16_t col, uint16_t bg);

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
int& cur() { return cursors[(int)current]; }
int& scr() { return scrolls[(int)current]; }

void clampScroll(int count) {
    if (cur() < scr()) scr() = cur();
    if (cur() >= scr() + VIS) scr() = cur() - VIS + 1;
    scr() = constrain(scr(), 0, max(0, count - VIS));
}

void sparkPush(uint8_t* buf, uint8_t val) {
    if (sparkCount < SPARK_MAX) {
        buf[sparkCount] = val;
        // sparkCount incremented in fetchDash after all 4 pushes
    } else {
        memmove(buf, buf + 1, SPARK_MAX - 1);
        buf[SPARK_MAX - 1] = val;
    }
}

const char* fetchErrMsg(int status) {
    if (status ==  0)  return "Not loaded — r=fetch";
    if (status == -1)  return "No WiFi — check connection";
    if (status == -2)  return "Parse error — r=retry";
    if (status == 200) return nullptr;
    static char buf[28];
    snprintf(buf, sizeof(buf), "HTTP %d — r=retry", status);
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Screen state push → bridge for web mirror (throttled 2 s)
// ─────────────────────────────────────────────────────────────────────────────
int findFavoriteIndex(const char* kind, const char* id) {
    for (int i = 0; i < favoriteCount; i++) {
        if (!strcmp(favorites[i].kind, kind) && !strcmp(favorites[i].id, id)) return i;
    }
    return -1;
}

bool isFavorite(const char* kind, const char* id) {
    return findFavoriteIndex(kind, id) >= 0;
}

const ServiceItem* findServiceById(const char* id) {
    for (int i = 0; i < svcCount; i++) {
        if (!strcmp(services[i].id, id)) return &services[i];
    }
    return nullptr;
}

const ActionItem* findActionById(const char* id) {
    for (int i = 0; i < actCount; i++) {
        if (!strcmp(actions[i].id, id)) return &actions[i];
    }
    return nullptr;
}

void loadFavorites() {
    Preferences p;
    p.begin("vpfavs", true);
    favoriteCount = 0;
    for (int i = 0; i < MAX_FAV; i++) {
        char key[8];
        snprintf(key, sizeof(key), "f%d", i);
        String raw = p.getString(key, "");
        if (!raw.length()) continue;
        int a = raw.indexOf('\t');
        int b = a >= 0 ? raw.indexOf('\t', a + 1) : -1;
        if (a <= 0 || b <= a + 1) continue;
        if (favoriteCount >= MAX_FAV) break;
        FavoriteItem& f = favorites[favoriteCount++];
        strlcpy(f.kind, raw.substring(0, a).c_str(), sizeof(f.kind));
        strlcpy(f.id, raw.substring(a + 1, b).c_str(), sizeof(f.id));
        strlcpy(f.label, raw.substring(b + 1).c_str(), sizeof(f.label));
    }
    p.end();
}

void saveFavorites() {
    Preferences p;
    p.begin("vpfavs", false);
    for (int i = 0; i < MAX_FAV; i++) {
        char key[8];
        snprintf(key, sizeof(key), "f%d", i);
        if (i < favoriteCount) {
            char raw[128];
            snprintf(raw, sizeof(raw), "%s\t%s\t%s",
                     favorites[i].kind, favorites[i].id, favorites[i].label);
            p.putString(key, raw);
        } else {
            p.putString(key, "");
        }
    }
    p.end();
}

bool toggleFavorite(const char* kind, const char* id, const char* label) {
    int idx = findFavoriteIndex(kind, id);
    if (idx >= 0) {
        for (int i = idx; i < favoriteCount - 1; i++) favorites[i] = favorites[i + 1];
        if (favoriteCount > 0) favoriteCount--;
        saveFavorites();
        return false;
    }

    FavoriteItem item = {};
    strlcpy(item.kind, kind, sizeof(item.kind));
    strlcpy(item.id, id, sizeof(item.id));
    strlcpy(item.label, label, sizeof(item.label));

    if (favoriteCount < MAX_FAV) favoriteCount++;
    for (int i = favoriteCount - 1; i > 0; i--) favorites[i] = favorites[i - 1];
    favorites[0] = item;
    saveFavorites();
    return true;
}

void executeActionItem(const ActionItem& a) {
    if (strcmp(a.id, "sys-shutdown") == 0) {
        confirmPending = true;
        showFb(FB::Pending, "Shutdown Pi?  Enter/Y=YES  Del/N=NO", 20000);
        return;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Running: %.38s", a.label);
    showFb(FB::Pending, msg, 10000);
    char path[96];
    if (a.runPath[0]) strlcpy(path, a.runPath, sizeof(path));
    else snprintf(path, sizeof(path), "/api/v1/actions/%s/run", a.id);
    JsonDocument doc;
    int code = bridgePost(path, doc);
    bool ok  = (code >= 200 && code < 300) && (bool)(doc["ok"] | false);
    const char* det = doc["message"] | doc["error"] | "";
    static char httpMsg[24];
    if (!det[0] && code > 0 && !(code >= 200 && code < 300)) {
        snprintf(httpMsg, sizeof(httpMsg), "HTTP %d", code);
        det = httpMsg;
    } else if (!det[0] && code < 0) {
        det = (code == -1) ? "no WiFi" : "no response";
    }
    snprintf(msg, sizeof(msg), ok ? "OK: %.60s" : "Fail: %.58s",
             det[0] ? det : (ok ? "done" : "control error"));
    if (ok) Audio::ok(); else Audio::err();
    showFb(ok ? FB::OK : FB::Err, msg, ok ? 3000 : 5000);
    fetchActions();
    drawScreen();
}

void pushScreenState() {
    unsigned long now = millis();
    if (now - lastStatePush < 30000UL) return;   // at most once per 30 s
    if (!bridgeOnline || !wifiOk()) return;
    lastStatePush = now;

    static const char* SNAMES[] = {
        "main","dashboard","services","svcaction","actions","favorites",
        "log","alerts","gitea","irremote","netscan","notes","settings","info","usbmacro","browser"
    };
    int si = (int)current;
    const char* sn = (si >= 0 && si < N_SCREENS) ? SNAMES[si] : "other";

    int  bat  = batteryLevelPercent();
    bool chg  = batteryIsCharging();
    int  rssi = WiFi.RSSI();
    unsigned long s = now / 1000;
    char up[20]; snprintf(up, sizeof(up), "%luh %02lum %02lus", s/3600, (s%3600)/60, s%60);
    char ip[20]; strlcpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip));

    String body;
    body.reserve(240);
    body += "{\"screen\":\"";   body += sn;
    body += "\",\"battery\":";  body += bat;
    body += ",\"charging\":";   body += chg ? "true" : "false";
    body += ",\"rssi\":";       body += rssi;
    body += ",\"uptime\":\"";   body += up;
    body += "\",\"ip\":\"";     body += ip;
    body += "\",\"firmware\":\""; body += FW_VERSION;
    body += "\",\"mode\":\"normal\"}";
    bridgePushState(body);
}

// ─────────────────────────────────────────────────────────────────────────────
// PIN entry screen — blocking mini-loop called from setup()
// ─────────────────────────────────────────────────────────────────────────────
void drawPinEntry(const char* entered, bool shake) {
    uint16_t bgCol = shake ? COL_ERR : COL_BG;
    M5Cardputer.Display.fillScreen(bgCol);
    M5Cardputer.Display.fillRect(0, 0,          SCREEN_W, 3, COL_AMBER);
    M5Cardputer.Display.fillRect(0, SCREEN_H-3, SCREEN_W, 3, COL_AMBER);

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setTextColor(COL_AMBER, bgCol);
    M5Cardputer.Display.setCursor(68, 18);
    M5Cardputer.Display.print("VAULTPI");

    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(COL_DIM, bgCol);
    M5Cardputer.Display.setCursor(62, 50);
    M5Cardputer.Display.print("PIN REQUIRED");

    // 4 dot indicators
    int len = strlen(entered);
    int dotX = 86;
    for (int i = 0; i < 4; i++) {
        uint16_t col = (i < len) ? COL_AMBER : COL_DIM;
        M5Cardputer.Display.fillCircle(dotX + i * 20, 75, 6, col);
    }

    M5Cardputer.Display.setTextColor(COL_DIM, bgCol);
    M5Cardputer.Display.setCursor(52, 98);
    M5Cardputer.Display.print("type 4 digits  Backspace=back");
}

void updateUsbState() {
    static bool initialized = false;
    if (!initialized) {
        // Read pre-boot IRQ status FIRST — AXP2101 latches VBUS_INSERT on power-on when USB
        // is connected, even before enableIRQ() is called.  Reading before clearing lets us
        // detect "booted with USB already connected" (e.g. full-charge standby where isCharging()
        // returns false and VBUS_GOOD is deasserted by the chip).
        uint32_t bootIrq   = M5Cardputer.Power.Axp2101.getIRQStatuses();
        bool bootInsert    = (bootIrq & m5::AXP2101_IRQ_VBUS_INSERT) != 0;
        bool bootRemove    = (bootIrq & m5::AXP2101_IRQ_VBUS_REMOVE) != 0;
        bool charging      = (M5Cardputer.Power.isCharging() == m5::Power_Class::is_charging);
        g_usbConnected     = charging || (bootInsert && !bootRemove);
        M5Cardputer.Power.Axp2101.enableIRQ(
            m5::AXP2101_IRQ_VBUS_INSERT | m5::AXP2101_IRQ_VBUS_REMOVE);
        M5Cardputer.Power.Axp2101.clearIRQStatuses();
        initialized = true;
        return;
    }
    uint64_t irq   = M5Cardputer.Power.Axp2101.getIRQStatuses();
    bool hasInsert = (irq & m5::AXP2101_IRQ_VBUS_INSERT) != 0;
    bool hasRemove = (irq & m5::AXP2101_IRQ_VBUS_REMOVE) != 0;
    bool charging  = (M5Cardputer.Power.isCharging() == m5::Power_Class::is_charging);
    if (charging)   g_usbConnected = true;
    if (hasInsert)  g_usbConnected = true;
    if (hasRemove)  g_usbConnected = false;
    if (hasInsert || hasRemove) M5Cardputer.Power.Axp2101.clearIRQStatuses();
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    M5Cardputer.begin(true);
    updateUsbState();  // read VBUS IRQ status before anything else touches the PMIC
    M5Cardputer.Display.setBrightness(0);        // blank immediately — hides old frame from previous firmware
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setColorDepth(16);
    M5Cardputer.Display.fillScreen(COL_BG);
    M5Cardputer.Display.setBrightness(FULL_BRIGHTNESS);

    irInit();

    drawMessage("Booting...", "loading config", COL_DIM);

    loadConfig();
    loadFavorites();
    strlcpy(g_bridge_host, rconfig.bridge_host, sizeof(g_bridge_host));
    strlcpy(g_bridge_psk,  rconfig.bridge_psk,  sizeof(g_bridge_psk));
    g_themeIdx = rconfig.theme_idx;
    Audio::init(rconfig.spk_volume);
    KbdHID.begin();
    USB.begin();

    // WiFi connect
    bool connected = false;
    for (int i = 0; i < 3 && !connected; i++) {
        if (!rconfig.wifi_ssid[i][0]) continue;
        char msg[56];
        snprintf(msg, sizeof(msg), "trying %s...", rconfig.wifi_ssid[i]);
        drawMessage("WiFi", msg, COL_WARN);
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true); delay(100);
        WiFi.begin(rconfig.wifi_ssid[i], rconfig.wifi_pass[i]);
        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_RETRY_MS) delay(200);
        if (WiFi.status() == WL_CONNECTED) connected = true;
    }

    if (!connected) {
        offlineMode = true;
        bridgeOnline = false;
        drawMessage("No Network", "offline mode", COL_ERR);
        delay(1800);
    } else {
        char ip[20]; strlcpy(ip, WiFi.localIP().toString().c_str(), sizeof(ip));
        drawMessage("Connected", ip, COL_OK);
        delay(400);
        ensureDeviceWebServer();
        // NTP sync for USB macro scheduler
        long tzSec = (long)rconfig.tz_offset * 3600L;
        configTime(tzSec, 0, "pool.ntp.org", "time.cloudflare.com");
        delay(600);
        struct tm ti; ntpSynced = getLocalTime(&ti, 2000);

        JsonDocument probe;
        int code = bridgePost("/api/cardputer/connect", probe);
        bridgeOnline = (code == 200) && (bool)(probe["ok"] | false);
        if (!bridgeOnline && (code == 401 || code == 403)) {
            offlineMode = true;
            drawMessage("Wrong password", "check Settings", COL_WARN);
            delay(1200);
        }
        // Connection errors: stay non-offline so background retry reconnects silently
    }

    dash.fresh = false;
    svcFetchStatus = -1;
    actFetchStatus = -1;
    logFetchStatus = -1;
    alertFetchStatus = -1;

    // PIN lock gate
    if (rconfig.pin_enabled && rconfig.pin_code[0] != '\0') {
        runPinGate();
    }

    Audio::boot();

    lastPoll       = millis();
    lastKey        = millis();
    lastWifiRetry   = millis();
    lastBridgeRetry = millis();
    lastAlertPoll   = millis();
    current = Screen::Main;
    g_menuOpen = true;
    drawScreen();
}

// ─────────────────────────────────────────────────────────────────────────────
// PIN gate — blocks until correct PIN entered; call with display already on
// ─────────────────────────────────────────────────────────────────────────────
void runPinGate() {
    char entered[5] = "";
    drawPinEntry(entered, false);
    while (true) {
        M5Cardputer.update();
        if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
            delay(10); continue;
        }
        M5Cardputer.update();
        Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();
        char c = ks.word.empty() ? 0 : ks.word[0];
        int  len = strlen(entered);
        if (c >= '0' && c <= '9' && len < 4) {
            entered[len] = c; entered[len+1] = '\0';
            drawPinEntry(entered, false);
        } else if ((ks.del || c == '\b') && len > 0) {
            entered[len-1] = '\0';
            drawPinEntry(entered, false);
        }
        if (strlen(entered) == 4) {
            if (strcmp(entered, rconfig.pin_code) == 0) {
                Audio::ok();
                break;
            } else {
                Audio::err();
                drawPinEntry(entered, true);
                delay(700);
                memset(entered, 0, sizeof(entered));
                drawPinEntry(entered, false);
            }
        }
    }
    drawScreen();
}

// ─────────────────────────────────────────────────────────────────────────────
// Main loop
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    M5Cardputer.update();

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        bool wasOff = screenOff;
        wakeDisplay();
        if (!wasOff) {
            Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();
            bool isEsc = (!ks.word.empty() && (uint8_t)ks.word[0] == 27);
            for (uint8_t hk : ks.hid_keys) if (hk == HID_ESC) { isEsc = true; break; }
            // Backtick key is physically labeled ESC on Cardputer — treat it as ESC
            // except when the user is actively typing text in an edit field
            if (!isEsc && !ks.word.empty() && ks.word[0] == '`' &&
                !settingsEditMode && !noteEditMode && !macroEditMode && !netConnectMode) {
                isEsc = true;
            }
            if (isEsc) {
                if (current == Screen::Browser && browserUrlMode && browserLineCount > 0) {
                    browserUrlMode = false;
                    drawBrowser();
                } else {
                    escapeToMain();
                }
            } else {
                if (!ks.word.empty()) handleChar(ks.word[0]);
                if (ks.enter) onEnter();
                // guard against double-handling backspace (handleChar '\b' already calls handleDel)
                bool wordWasDel = !ks.word.empty() &&
                    ((uint8_t)ks.word[0] == '\b' || (uint8_t)ks.word[0] == 127);
                if (ks.del && !wordWasDel) handleDel();
                for (uint8_t hk : ks.hid_keys) handleHid(hk);
            }
        }
    }

    // GO button (BtnA on back of device)
    if (M5Cardputer.BtnA.wasClicked()) {
        if (rconfig.go_btn_mode == 1) {  // Lock mode
            if (screenOff || dimmed) {
                wakeDisplay();
                if (rconfig.pin_enabled) runPinGate();
            } else {
                M5Cardputer.Display.setBrightness(0);
                dimmed    = true;
                screenOff = true;
            }
        }
    }

    unsigned long now  = millis();
    unsigned long idle = now - lastKey;

    // USB macro scheduler + device web portal
    checkUsbMacro();
    serviceDeviceWebServer();

    // Background data poll
    if (!offlineMode && wifiOk() &&
        now - lastPoll >= (unsigned long)(rconfig.poll_sec * 1000)) {
        lastPoll = now;
        fetchDash();
        fetchAlerts();
        if (!screenOff && current == Screen::Main) drawMainMenu();
        else if (!screenOff && current == Screen::Dashboard) drawDashboard();
    }

    // WiFi reconnect retry when offline
    if (!wifiOk() && now - lastWifiRetry >= WIFI_RECONNECT_MS) {
        lastWifiRetry = now;
        tryWifiReconnect();
    }

    // Silent bridge reconnect when WiFi is up but bridge unreachable
    if (wifiOk() && !bridgeOnline && !offlineMode &&
        now - lastBridgeRetry >= 10000UL) {
        lastBridgeRetry = now;
        JsonDocument probe;
        int code = bridgePost("/api/cardputer/connect", probe);
        if (code == 200 && (bool)(probe["ok"] | false)) {
            bridgeOnline = true;
            fetchDash();
            fetchAlerts();
            if (!screenOff) drawScreen();
        }
    }


    // Clear feedback overlay
    if (fb != FB::None && !confirmPending && now > fbUntil) {
        fb = FB::None;
        drawScreen();
    }

    // Poll AXP2101 VBUS IRQ flags every 500 ms to track USB connect/disconnect
    {
        static unsigned long lastUsbPoll = 0;
        if (now - lastUsbPoll >= 500) { lastUsbPoll = now; updateUsbState(); }
    }

    // Dim/off — use power-aware timeouts (0=never)
    {
        bool onPower  = batteryIsCharging();
        int activeDim = onPower ? rconfig.dim_sec_power : rconfig.dim_sec_bat;
        int activeOff = onPower ? rconfig.off_sec_power : rconfig.off_sec_bat;
        if (!dimmed && activeDim > 0 && idle > (unsigned long)(activeDim * 1000)) {
            M5Cardputer.Display.setBrightness(DIM_BRIGHTNESS);
            dimmed = true;
        }
        if (dimmed && !screenOff && activeOff > 0 &&
            idle > (unsigned long)(activeOff * 1000)) {
            M5Cardputer.Display.setBrightness(0);
            screenOff = true;
        }
    }
}

void wakeDisplay() {
    bool wasOff = screenOff;
    lastKey   = millis();
    screenOff = false;
    dimmed    = false;
    M5Cardputer.Display.setBrightness(FULL_BRIGHTNESS);
    if (wasOff) drawScreen();
}

void tryWifiReconnect() {
    if (offlineMode) return;
    for (int i = 0; i < 3; i++) {
        if (!rconfig.wifi_ssid[i][0]) continue;
        WiFi.disconnect(true); delay(100);
        WiFi.begin(rconfig.wifi_ssid[i], rconfig.wifi_pass[i]);
        unsigned long t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 5000) delay(200);
        if (WiFi.status() == WL_CONNECTED) { offlineMode = false; drawScreen(); return; }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Navigation
// ─────────────────────────────────────────────────────────────────────────────
void navigate(Screen s) {
    if (navDepth < 8) navHistory[navDepth++] = current;
    current = s;
    if (s == Screen::Settings) { pendingConfig = rconfig; settingsEditMode = false; }
    if (s == Screen::Notes && !notesLoaded) { loadNotes(notes, MAX_NOTES); notesLoaded = true; }
    if (s == Screen::Browser) {
        browserUrlMode = true; browserUrlBuf[0] = '\0';
        browserLineCount = 0;  browserError[0]  = '\0';
        browserBookmarkMode = false;
        browserLinkMode = false; browserLinkCount = 0;
        browserLinkCursor = 0; browserLinkScroll = 0;
        browserScroll = 0;     browserLoading   = false;
        browserHistoryDepth = 0;
    }
    if (s == Screen::Terminal) {
        termLineCount = 0; termScroll = 0;
        termCmdBuf[0] = '\0'; termCmdMode = true; termBusy = false;
    }
    fb = FB::None;
    if (!offlineMode) fetchForScreen(s);
    drawScreen();
}

void goBack() {
    fb = FB::None;                        // always clear toast — don't block navigation
    if (confirmPending) { confirmPending = false; drawScreen(); return; }
    if (netConnectMode) { netConnectMode = false; drawNetScan(); return; }
    if (noteEditMode)   { noteEditMode = false; drawNotes(); return; }
    if (macroEditMode)  { macroEditMode = false; drawUSBMacro(); return; }
    if (current == Screen::Browser && browserBookmarkMode) { browserBookmarkMode = false; drawBrowser(); return; }
    if (current == Screen::Browser && browserUrlMode && browserLineCount > 0) { browserUrlMode = false; drawBrowser(); return; }
    if (current == Screen::Main) { g_menuOpen = true; drawMainMenu(); return; }
    current = (navDepth > 0) ? navHistory[--navDepth] : Screen::Main;
    if (current == Screen::Main) g_menuOpen = true;
    drawScreen();
}

void escapeToMain() {
    fb = FB::None;
    confirmPending = false;
    netConnectMode = false;
    noteEditMode = false;
    macroEditMode = false;
    settingsEditMode = false;
    browserBookmarkMode = false;
    browserUrlMode = false;
    browserLinkMode = false;
    browserLoading = false;
    navDepth = 0;
    current = Screen::Main;
    g_menuOpen = true;
    Audio::click();
    drawScreen();
}

bool retryBridgeHost(bool redrawCurrent) {
    if (!wifiOk()) {
        offlineMode = true;
        bridgeOnline = false;
        Audio::err();
        showFb(FB::Err, "WiFi is offline", 2500);
        return false;
    }

    JsonDocument probe;
    int code = bridgePost("/api/cardputer/connect", probe);
    if (code == 401 || code == 403) {
        offlineMode = true;
        bridgeOnline = false;
        Audio::err();
        showFb(FB::Err, "Wrong password", 3500);
        return false;
    }
    if (code != 200) {
        offlineMode = true;
        bridgeOnline = false;
        Audio::err();
        showFb(FB::Err, "Check IP / port 8001", 3000);
        return false;
    }

    offlineMode = false;
    bridgeOnline = true;
    fetchDash();
    if (redrawCurrent) {
        fetchForScreen(current);
        Audio::ok();
        showFb(FB::OK, "Control connected", 1800);
        drawScreen();
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ---------------------------------------------------------------------------
// Device web portal â€” local diagnostics + browser OTA upload
// ---------------------------------------------------------------------------
static const char DEVICE_WEB_PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>VaultPi Cardputer</title>
  <style>
    body{margin:0;background:#07090d;color:#d8e0ea;font:14px/1.4 monospace;padding:20px}
    .card{max-width:760px;margin:0 auto;border:1px solid #263041;border-radius:14px;
      background:linear-gradient(180deg,#0f1520,#0a0f16);box-shadow:0 20px 50px rgba(0,0,0,.45);padding:18px}
    h1{margin:0 0 10px;font-size:20px;letter-spacing:2px;text-transform:uppercase;color:#7dd3fc}
    h2{margin:18px 0 8px;font-size:13px;text-transform:uppercase;color:#94a3b8;letter-spacing:1px}
    .row{display:grid;grid-template-columns:160px 1fr;gap:10px;padding:4px 0;border-bottom:1px solid #18202d}
    .k{color:#6b7280}.v{color:#f8fafc;word-break:break-all}
    .pill{display:inline-block;padding:2px 8px;border-radius:999px;background:#111827;border:1px solid #334155}
    .ok{color:#4ade80}.warn{color:#fbbf24}.err{color:#f87171}
    .btn{display:inline-block;border:1px solid #334155;background:#111827;color:#e2e8f0;padding:8px 12px;
      border-radius:10px;text-decoration:none;cursor:pointer;margin:4px 6px 0 0;font-family:inherit}
    .btn:hover{background:#172033}
    input[type=file]{width:100%;padding:10px;background:#0b1220;border:1px solid #334155;border-radius:10px;color:#cbd5e1}
    small{color:#6b7280}
  </style>
</head>
<body>
<div class="card">
  <h1>VaultPi Cardputer</h1>
  <div id="summary" class="pill">Loading...</div>

  <h2>Diagnostics</h2>
  <div id="diag"></div>

  <h2>Actions</h2>
  <button class="btn" onclick="retryBridge()">Retry control</button>
  <button class="btn" onclick="rebootCardputer()">Reboot</button>
  <p><small>Upload a new <code>.bin</code> firmware file below. The device will reboot after a successful flash.</small></p>
  <form method="POST" action="/update" enctype="multipart/form-data">
    <input type="file" name="firmware" accept=".bin,application/octet-stream" required>
    <button class="btn" type="submit">Flash firmware</button>
  </form>
</div>
<script>
async function refresh() {
  const r = await fetch('/api/v1/device');
  const s = await r.json();
  document.getElementById('summary').textContent =
    `${s.ip || 'n/a'} | ${s.bridge_online ? 'control online' : 'control offline'} | ${s.mode || 'unknown'}`;
  document.getElementById('summary').className = 'pill ' + (s.bridge_online ? 'ok' : 'warn');
  const rows = [
    ['Wi-Fi', s.wifi || '--'],
    ['SSID', s.ssid || '--'],
    ['Control', s.bridge_host || '--'],
    ['Control Center', s.cc_url || '--'],
    ['Control status', s.bridge_online ? 'online' : 'offline'],
    ['Mode', s.mode || '--'],
    ['Screen', s.screen || '--'],
    ['Firmware', s.firmware || '--'],
    ['Uptime', s.uptime || '--'],
    ['RSSI', s.rssi || '--'],
    ['Battery', s.battery || '--'],
    ['Web server', s.web ? 'running' : 'stopped'],
  ];
  document.getElementById('diag').innerHTML = rows.map(([k,v]) =>
    `<div class="row"><div class="k">${k}</div><div class="v">${v}</div></div>`).join('');
}
async function retryBridge() {
  await fetch('/retry-host', {method:'POST'});
  await refresh();
}
async function rebootCardputer() {
  await fetch('/reboot', {method:'POST'});
}
refresh();
setInterval(refresh, 2500);
</script>
</body>
</html>
)HTML";

void handleDeviceRoot() {
    deviceServer.send_P(200, "text/html", DEVICE_WEB_PAGE);
}

void handleDeviceApi() {
    JsonDocument doc;
    doc["ip"] = wifiOk() ? WiFi.localIP().toString() : "";
    doc["ssid"] = wifiOk() ? WiFi.SSID() : "";
    doc["wifi"] = wifiOk() ? "connected" : "offline";
    doc["bridge_host"] = g_bridge_host;
    char ccUrl[90];
    const char* p = strstr(g_bridge_host, ":8001");
    if (p) { int bl = (int)(p - g_bridge_host); snprintf(ccUrl, sizeof(ccUrl), "%.*s:8000", bl, g_bridge_host); }
    else strlcpy(ccUrl, g_bridge_host, sizeof(ccUrl));
    doc["cc_url"] = ccUrl;
    doc["bridge_online"] = bridgeOnline;
    doc["offline_mode"] = offlineMode;
    doc["web"] = deviceWebStarted;
    doc["nickname"] = rconfig.device_nickname;
    doc["screen"] = current == Screen::Main ? "main" : "menu";
    doc["firmware"] = FW_VERSION;
    doc["mode"] = offlineMode ? "offline" : (bridgeOnline ? "normal" : "degraded");
    unsigned long s = millis() / 1000;
    char uptime[20];
    snprintf(uptime, sizeof(uptime), "%luh %02lum %02lus", s / 3600, (s % 3600) / 60, s % 60);
    doc["uptime"] = uptime;
    if (wifiOk()) {
        int rssi = WiFi.RSSI();
        char rssiBuf[24];
        snprintf(rssiBuf, sizeof(rssiBuf), "%ddBm", rssi);
        doc["rssi"] = rssiBuf;
    } else {
        doc["rssi"] = "";
    }
    char batBuf[40];
    formatBatteryStatus(batBuf, sizeof(batBuf));
    doc["battery"] = batBuf;
    doc["charging"] = batteryIsCharging();
    doc["battery_mv"] = batteryVoltageMv();
    String body;
    serializeJson(doc, body);
    deviceServer.send(200, "application/json", body);
}

void handleDeviceRetry() {
    bool ok = retryBridgeHost(false);
    deviceServer.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void handleDeviceReboot() {
    deviceServer.send(200, "text/plain", "rebooting");
    delay(300);
    ESP.restart();
}

void handleDeviceUpdateUpload() {
    HTTPUpload& upload = deviceServer.upload();
    otaUploadSeen = true;

    if (upload.status == UPLOAD_FILE_START) {
        otaUploadOk = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
        otaUploadStarted = true;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (otaUploadStarted && otaUploadOk) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                otaUploadOk = false;
            }
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        otaUploadOk = otaUploadStarted && otaUploadOk && Update.end(true) && Update.isFinished();
        otaUploadStarted = false;
    }
}

void handleDeviceUpdate() {
    bool ok = otaUploadSeen && otaUploadOk && !Update.hasError();
    otaUploadSeen = false;
    otaUploadStarted = false;
    otaUploadOk = false;
    deviceServer.send(ok ? 200 : 500, "text/plain", ok ? "update complete, rebooting" : "update failed");
    if (ok) {
        delay(500);
        ESP.restart();
    }
}

void ensureDeviceWebServer() {
    if (deviceWebStarted || !wifiOk()) return;

    deviceServer.on("/", HTTP_GET, handleDeviceRoot);
    deviceServer.on("/api/v1/device", HTTP_GET, handleDeviceApi);
    deviceServer.on("/retry-host", HTTP_POST, handleDeviceRetry);
    deviceServer.on("/reboot", HTTP_POST, handleDeviceReboot);
    deviceServer.on("/update", HTTP_POST, handleDeviceUpdate, handleDeviceUpdateUpload);
    deviceServer.begin();
    deviceWebStarted = true;
}

void serviceDeviceWebServer() {
    if (deviceWebStarted) deviceServer.handleClient();
}
// ─────────────────────────────────────────────────────────────────────────────
void fetchForScreen(Screen s) {
    switch (s) {
        case Screen::Dashboard: fetchDash();     break;
        case Screen::Services:  fetchServices(); break;
        case Screen::Actions:   fetchActions();  break;
        case Screen::Log:       fetchLog();      break;
        case Screen::Alerts:    fetchAlerts();   break;
        case Screen::Gitea:     fetchGitea();    break;
        case Screen::Info:
            if (!otaChecked) { otaCheck(otaInfo); otaChecked = true; }
            break;
        default: break;
    }
}

void fetchDash() {
    JsonDocument doc;
    int code = bridgeGet("/api/v1/dashboard", doc);
    if (code != 200) { dash.fresh = false; bridgeOnline = false; return; }
    bridgeOnline = true;
    dash.fresh    = true;
    strlcpy(dash.hostname, doc["hostname"] | "vaultpi", sizeof(dash.hostname));
    strlcpy(dash.uptime,   doc["uptime"]   | "n/a",    sizeof(dash.uptime));
    dash.cpu  = doc["cpuPercent"]  | 0;
    dash.ram  = doc["ramPercent"]  | 0;
    dash.disk = doc["diskPercent"] | 0;
    dash.temp = doc["cpuTemp"]     | 0;
    strlcpy(dash.load, doc["loadAvg"] | "n/a", sizeof(dash.load));
    dash.svcTotal = doc["servicesTotal"] | 0;
    dash.svcUp    = doc["servicesUp"]    | 0;
    dash.svcDown  = doc["servicesDown"]  | 0;
    dash.sdErrors = doc["sdErrors"]      | 0;
    JsonObject ev = doc["lastEvent"];
    strlcpy(dash.lastMsg,  ev["message"] | "", sizeof(dash.lastMsg));
    strlcpy(dash.lastType, ev["type"]    | "info", sizeof(dash.lastType));

    // Update sparklines — shift all buffers together
    if (sparkCount < SPARK_MAX) {
        sparkCpu[sparkCount]  = (uint8_t)constrain(dash.cpu,  0, 100);
        sparkRam[sparkCount]  = (uint8_t)constrain(dash.ram,  0, 100);
        sparkDisk[sparkCount] = (uint8_t)constrain(dash.disk, 0, 100);
        sparkTemp[sparkCount] = (uint8_t)constrain(dash.temp, 0, 100);
        sparkCount++;
    } else {
        memmove(sparkCpu,  sparkCpu  + 1, SPARK_MAX - 1);
        memmove(sparkRam,  sparkRam  + 1, SPARK_MAX - 1);
        memmove(sparkDisk, sparkDisk + 1, SPARK_MAX - 1);
        memmove(sparkTemp, sparkTemp + 1, SPARK_MAX - 1);
        sparkCpu[SPARK_MAX-1]  = (uint8_t)constrain(dash.cpu,  0, 100);
        sparkRam[SPARK_MAX-1]  = (uint8_t)constrain(dash.ram,  0, 100);
        sparkDisk[SPARK_MAX-1] = (uint8_t)constrain(dash.disk, 0, 100);
        sparkTemp[SPARK_MAX-1] = (uint8_t)constrain(dash.temp, 0, 100);
    }
}

void fetchServices() {
    if (!wifiOk()) { svcFetchStatus = -1; return; }
    JsonDocument doc;
    svcFetchStatus = bridgeGet("/api/v1/services", doc);
    if (svcFetchStatus != 200) { bridgeOnline = false; return; }
    bridgeOnline = true;
    svcCount = 0;
    for (JsonObject item : doc.as<JsonArray>()) {
        if (svcCount >= MAX_SVC) break;
        ServiceItem& s = services[svcCount++];
        strlcpy(s.id,     item["id"]     | "", sizeof(s.id));
        strlcpy(s.name,   item["name"]   | "", sizeof(s.name));
        strlcpy(s.kind,   item["kind"]   | "", sizeof(s.kind));
        strlcpy(s.status, item["status"] | "", sizeof(s.status));
    }
    if (svcCount > 0 && cur() >= svcCount) cur() = svcCount - 1;
}

void fetchActions() {
    if (!wifiOk()) { actFetchStatus = -1; return; }
    JsonDocument doc;
    actFetchStatus = bridgeGet("/api/v1/actions", doc);
    if (actFetchStatus != 200) { bridgeOnline = false; return; }
    bridgeOnline = true;
    actCount = 0;
    for (JsonObject item : doc.as<JsonArray>()) {
        if (actCount >= MAX_ACT) break;
        ActionItem& a = actions[actCount++];
        strlcpy(a.id,     item["id"]     | "", sizeof(a.id));
        strlcpy(a.label,  item["label"]  | "", sizeof(a.label));
        strlcpy(a.kind,   item["kind"]   | "", sizeof(a.kind));
        strlcpy(a.status, item["status"] | "", sizeof(a.status));
        strlcpy(a.runPath,item["run_path"] | "", sizeof(a.runPath));
    }
    if (actCount > 0 && cur() >= actCount) cur() = actCount - 1;
}

void fetchLog() {
    if (!wifiOk()) { logFetchStatus = -1; return; }
    JsonDocument doc;
    logFetchStatus = bridgeGet("/api/v1/activity", doc);
    if (logFetchStatus != 200) { bridgeOnline = false; return; }
    bridgeOnline = true;
    logCount = 0;
    for (JsonObject item : doc.as<JsonArray>()) {
        if (logCount >= MAX_LOG) break;
        LogItem& l = logItems[logCount++];
        strlcpy(l.message, item["message"] | "", sizeof(l.message));
        strlcpy(l.type,    item["type"]    | "info", sizeof(l.type));
    }
}

void fetchAlerts() {
    if (!wifiOk()) { alertFetchStatus = -1; return; }
    JsonDocument doc;
    alertFetchStatus = bridgeGet("/api/v1/cardputer/alerts", doc);
    if (alertFetchStatus == 404) return;  // old bridge — skip silently, don't go offline
    if (alertFetchStatus != 200) { bridgeOnline = false; return; }
    bridgeOnline = true;
    alertCount = 0;
    unreadCount = 0;
    for (JsonObject item : doc.as<JsonArray>()) {
        if (alertCount >= MAX_ALERTS) break;
        AlertItem& a = alerts[alertCount++];
        strlcpy(a.message, item["message"] | "", sizeof(a.message));
        strlcpy(a.level,   item["level"]   | "info", sizeof(a.level));
        a.read = (bool)(item["read"] | false);
        if (!a.read) unreadCount++;
    }
}

void fetchGitea() {
    if (!wifiOk()) { gitea.fresh = false; return; }
    JsonDocument doc;
    int code = bridgeGet("/api/v1/gitea", doc);
    // 503 = Gitea auth/connection issue (bridge is fine); other non-200 = bridge problem
    if (code != 200) {
        gitea.fresh = false;
        if (code != 503 && code > 0) bridgeOnline = false;
        return;
    }

    bridgeOnline = true;
    gitea.fresh = true;

    JsonObject user = doc["user"].as<JsonObject>();
    JsonObject ver  = doc["version"].as<JsonObject>();
    JsonObject sum  = doc["summary"].as<JsonObject>();
    JsonObject heat = doc["heatmap"].as<JsonObject>();
    strlcpy(gitea.username, user["username"] | "n/a", sizeof(gitea.username));
    strlcpy(gitea.fullName, user["full_name"] | "",   sizeof(gitea.fullName));
    strlcpy(gitea.htmlUrl,  user["html_url"]  | "",   sizeof(gitea.htmlUrl));
    strlcpy(gitea.serverVer,ver["server"]     | "",   sizeof(gitea.serverVer));
    gitea.followers = user["followers"] | 0;
    gitea.following = user["following"] | 0;
    gitea.stars     = user["stars"]     | 0;
    gitea.repos     = user["repos"]     | 0;
    gitea.total     = sum["total"]      | 0;
    gitea.activeDays= sum["active_days"]| 0;
    gitea.peak      = sum["peak"]       | 0;

    for (int i = 0; i < 84; i++) gitea.cells[i].level = 0;
    JsonArray levels = heat["levels"].as<JsonArray>();
    int idx = 0;
    for (JsonVariant v : levels) {
        if (idx >= 84) break;
        gitea.cells[idx++].level = (uint8_t)constrain((int)(v | 0), 0, 4);
    }
}

static uint16_t giteaHeatColor(uint8_t level) {
    switch (level) {
        case 1: return 0x0841;
        case 2: return 0x0460;
        case 3: return 0x07E0;
        case 4: return 0xAFE5;
        default: return thBG();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard handling
// ─────────────────────────────────────────────────────────────────────────────
void onUp() {
    if (settingsEditMode || noteEditMode || macroEditMode || netConnectMode || (browserUrlMode && current == Screen::Browser)) return;
    if (current == Screen::Browser) {
        if (browserBookmarkMode) {
            if (cur() > 0) { cur()--; drawBrowser(); Audio::click(); }
            return;
        }
        if (browserLinkMode) {
            browserLinkMove(-1);
            return;
        }
        browserScrollBy(-1);
        return;
    }
    if (current == Screen::IRRemote) {
        if (irRow > 0) { irRow--; drawIRRemote(); Audio::click(); } return;
    }
    if (current == Screen::Terminal && !termCmdMode) {
        if (termScroll > 0) { termScroll--; drawTerminal(); Audio::click(); } return;
    }
    int count = 0;
    switch (current) {
        case Screen::Main:      count = MAIN_COUNT;    break;
        case Screen::Services:  count = max(1,svcCount);    break;
        case Screen::SvcAction: count = SVC_ACT_COUNT; break;
        case Screen::Actions:   count = max(1,actCount);    break;
        case Screen::Favorites: count = max(1,favoriteCount); break;
        case Screen::Log:       count = max(1,logCount);    break;
        case Screen::Alerts:    count = max(1,alertCount);  break;
        case Screen::Gitea:     count = 1;                  break;
        case Screen::Notes:     count = MAX_NOTES;              break;
        case Screen::Settings:  count = SF_COUNT;               break;
        case Screen::USBMacro:  count = 5;                      break;
        case Screen::NetScan:   count = max(1, netAPCount);     break;
        default: return;
    }
    if (cur() > 0) { cur()--; clampScroll(count); Audio::click(); drawScreen(); }
}

void onDown() {
    if (settingsEditMode || noteEditMode || macroEditMode || netConnectMode || (browserUrlMode && current == Screen::Browser)) return;
    if (current == Screen::Browser) {
        if (browserBookmarkMode) {
            if (cur() < BROWSER_BM_COUNT - 1) { cur()++; drawBrowser(); Audio::click(); }
            return;
        }
        if (browserLinkMode) {
            browserLinkMove(1);
            return;
        }
        browserScrollBy(1);
        return;
    }
    if (current == Screen::IRRemote) {
        if (irRow < IR_GRID_ROWS - 1) { irRow++; drawIRRemote(); Audio::click(); } return;
    }
    if (current == Screen::Terminal && !termCmdMode) {
        int maxScroll = max(0, termLineCount - TERM_VIS);
        if (termScroll < maxScroll) { termScroll++; drawTerminal(); Audio::click(); } return;
    }
    int count = 0;
    switch (current) {
        case Screen::Main:      count = MAIN_COUNT;    break;
        case Screen::Services:  count = max(1,svcCount);    break;
        case Screen::SvcAction: count = SVC_ACT_COUNT; break;
        case Screen::Actions:   count = max(1,actCount);    break;
        case Screen::Favorites: count = max(1,favoriteCount); break;
        case Screen::Log:       count = max(1,logCount);    break;
        case Screen::Alerts:    count = max(1,alertCount);  break;
        case Screen::Gitea:     count = 1;                  break;
        case Screen::Notes:     count = MAX_NOTES;              break;
        case Screen::Settings:  count = SF_COUNT;               break;
        case Screen::USBMacro:  count = 5;                      break;
        case Screen::NetScan:   count = max(1, netAPCount);     break;
        default: return;
    }
    if (cur() < count - 1) { cur()++; clampScroll(count); Audio::click(); drawScreen(); }
}

void onLeft() {
    if (settingsEditMode || noteEditMode) return;
    if (current == Screen::Browser && !browserUrlMode && !browserBookmarkMode) {
        browserScrollBy(-BR_VIS);
        return;
    }
    if (current == Screen::IRRemote) {
        if (irCol > 0) { irCol--; drawIRRemote(); Audio::click(); } return;
    }
    if (current == Screen::Main) {
        cur() = (cur() > 0) ? cur() - 1 : MAIN_COUNT - 1;
        Audio::click(); drawScreen(); return;
    }
    goBack();
}

void onRight() {
    if (settingsEditMode || noteEditMode) return;
    if (current == Screen::Browser && !browserUrlMode && !browserBookmarkMode) {
        browserScrollBy(BR_VIS);
        return;
    }
    if (current == Screen::IRRemote) {
        if (irCol < IR_GRID_COLS - 1) { irCol++; drawIRRemote(); Audio::click(); } return;
    }
    if (current == Screen::Main) {
        cur() = (cur() < MAIN_COUNT - 1) ? cur() + 1 : 0;
        Audio::click(); drawScreen(); return;
    }
    onEnter();
}

void handleDel() {
    if (browserBookmarkMode && current == Screen::Browser) {
        browserBookmarkMode = false;
        browserUrlMode = false;
        drawBrowser();
        return;
    }
    if (current == Screen::Terminal) {
        if (termCmdMode) {
            int len = strlen(termCmdBuf);
            if (len > 0) { termCmdBuf[len-1] = '\0'; drawTerminal(); }
        } else {
            termLineCount = 0; termScroll = 0;
            termCmdMode = true; drawTerminal();
        }
        return;
    }
    if (current == Screen::Browser && browserLinkMode) {
        browserLinkMode = false;
        drawBrowser();
        return;
    }
    if (current == Screen::Browser && !browserUrlMode && !browserBookmarkMode && browserLineCount > 0) {
        if (browserHistoryDepth > 0) {
            strlcpy(browserUrlBuf, browserUrlHistory[--browserHistoryDepth], sizeof(browserUrlBuf));
            browserNavBack = true;
            fetchBrowserPage();
        } else {
            goBack();
        }
        return;
    }
    if (browserUrlMode && current == Screen::Browser) {
        if (browserError[0] && browserLineCount == 0) { goBack(); return; }
        int len = strlen(browserUrlBuf);
        if (len > 0) { browserUrlBuf[len-1] = '\0'; drawBrowserUrlBar(); }
        else          { goBack(); }
        return;
    }
    if (netConnectMode) {
        int len = strlen(netConnectBuf);
        if (len > 0) { netConnectBuf[len - 1] = '\0'; drawNetScan(); }
        else          { netConnectMode = false; drawNetScan(); }
        return;
    }
    if (macroEditMode) {
        if (macroEditFresh) {
            macroEditFresh = false;
        }
        int len = strlen(macroEditBuf);
        if (len > 0) { macroEditBuf[len-1] = '\0'; drawUSBMacro(); }
        else          { macroEditMode = false; drawUSBMacro(); }
        return;
    }
    if (noteEditMode) {
        int len = strlen(noteEditBuf);
        if (len > 0) { noteEditBuf[len-1] = '\0'; drawNotes(); }
        else          { noteEditMode = false; drawNotes(); }
        return;
    }
    if (settingsEditMode) {
        int len = strlen(settingsEditBuf);
        if (len > 0) { settingsEditBuf[len-1] = '\0'; drawSettingsEditBar(); }
        else          { settingsEditMode = false; drawSettings(); }
        return;
    }
    goBack();
}

bool parseMacroTimeInput(const char* input, uint8_t& hour, uint8_t& minute) {
    if (!input || !input[0]) return false;

    const char* colon = strchr(input, ':');
    int h = -1;
    int m = -1;

    if (colon) {
        char* end = nullptr;
        h = (int)strtol(input, &end, 10);
        if (end != colon) return false;
        m = (int)strtol(colon + 1, &end, 10);
        if (end == colon + 1 || *end != '\0') return false;
    } else {
        char digits[5] = "";
        int n = 0;
        for (const char* p = input; *p; ++p) {
            if (!isdigit((unsigned char)*p)) return false;
            if (n >= 4) return false;
            digits[n++] = *p;
        }
        if (n == 0) return false;
        int value = atoi(digits);
        if (n <= 2) {
            h = value;
            m = 0;
        } else {
            h = value / 100;
            m = value % 100;
        }
    }

    if (h < 0 || h > 23 || m < 0 || m > 59) return false;
    hour = (uint8_t)h;
    minute = (uint8_t)m;
    return true;
}

void onEnter() {
    // ── Shutdown confirm ──────────────────────────────────────────────────
    if (confirmPending) {
        confirmPending = false;
        fb = FB::None;
        showFb(FB::Pending, "Shutting down Pi...", 10000);
        JsonDocument doc;
        int code = bridgePost("/api/v1/actions/sys-shutdown/run", doc);
        if (code >= 200 && code < 300) {
            bool ok = (bool)(doc["ok"] | false);
            const char* detail = doc["message"] | doc["error"] | "";
            if (ok) {
                Audio::ok();
                showFb(FB::OK, detail[0] ? detail : "Pi is shutting down", 6000);
            } else {
                Audio::err();
                char msg[96];
                snprintf(msg, sizeof(msg), "Fail: %s", detail[0] ? detail : "rejected by Pi");
                showFb(FB::Err, msg, 6000);
            }
        } else {
            Audio::err();
            char msg[48];
            if (code < 0) snprintf(msg, sizeof(msg), "No response (offline)");
            else snprintf(msg, sizeof(msg), "No response (HTTP %d)", code);
            showFb(FB::Err, msg, 5000);
        }
        return;
    }

    // ── Terminal: run command or enter command mode ───────────────────────
    if (current == Screen::Terminal) {
        if (termCmdMode) {
            if (strlen(termCmdBuf) > 0) fetchTerminalExec();
        } else {
            termCmdMode = true;
            drawTerminal();
        }
        return;
    }

    // ── Browser: submit URL or open new URL ──────────────────────────────
    if (current == Screen::Browser) {
        if (browserBookmarkMode) {
            if (cur() >= BROWSER_BM_COUNT - 1) {
                browserBookmarkMode = false;
                browserUrlMode = false;
                drawBrowser();
            } else {
                strlcpy(browserUrlBuf, BROWSER_BM_URLS[cur()], sizeof(browserUrlBuf));
                browserBookmarkMode = false;
                browserUrlMode = true;
                fetchBrowserPage();
            }
        } else if (browserUrlMode) {
            if (strlen(browserUrlBuf) > 0) fetchBrowserPage();
        } else if (browserLinkMode) {
            openBrowserLink();
        } else if (browserLinkCount > 0) {
            browserLinkMode = true;
            browserBookmarkMode = false;
            drawBrowser();
        } else {
            browserUrlMode = true;
            browserBookmarkMode = false;
            drawBrowser();
        }
        return;
    }

    // ── Note edit mode — save ─────────────────────────────────────────────
    if (noteEditMode) {
        strlcpy(notes[noteEditIdx], noteEditBuf, NOTE_LEN);
        saveNote(noteEditIdx, noteEditBuf);
        noteEditMode = false;
        Audio::ok();
        drawNotes();
        return;
    }

    // ── Settings edit mode — save field ──────────────────────────────────
    if (settingsEditMode) {
        sfSet(settingsEditField, settingsEditBuf);
        settingsEditMode = false;
        Audio::ok();
        drawSettings();
        return;
    }

    switch (current) {
        case Screen::Main:
            g_menuOpen = true;
            Audio::click();
            navigate(MAIN_TARGETS[constrain(cur(), 0, MAIN_COUNT - 1)]);
            break;

        case Screen::Services:
            if (svcCount > 0) {
                selectedSvc = cur();
                cursors[(int)Screen::SvcAction] = 0;
                Audio::click();
                navigate(Screen::SvcAction);
            }
            break;

        case Screen::SvcAction: {
            int ai = cursors[(int)Screen::SvcAction];
            if (ai == SVC_ACT_COUNT - 1) { goBack(); break; }
            if (selectedSvc < 0 || selectedSvc >= svcCount) break;
            const char* lbl = SVC_ACTIONS[ai];
            char lower[10] = {};
            for (int i=0;i<9&&lbl[i];i++) lower[i]=tolower((unsigned char)lbl[i]);
            char msg[64];
            snprintf(msg, sizeof(msg), "%s: %s...", lbl, services[selectedSvc].name);
            showFb(FB::Pending, msg, 10000);
            char path[80];
            snprintf(path, sizeof(path), "/api/v1/services/%s/%s",
                     services[selectedSvc].id, lower);
            JsonDocument doc;
            int code = bridgePost(path, doc);
            bool ok  = (code>=200&&code<300) && (bool)(doc["ok"]|false);
            const char* det = doc["message"] | doc["error"] | "";
            static char svcHttpMsg[24];
            if (!det[0] && code > 0 && !(code>=200&&code<300)) {
                snprintf(svcHttpMsg, sizeof(svcHttpMsg), "HTTP %d", code);
                det = svcHttpMsg;
            } else if (!det[0] && code < 0) {
                det = (code==-1) ? "no WiFi" : "no response";
            }
            snprintf(msg, sizeof(msg), ok?"Done: %.56s":"Fail: %.56s", det[0]?det:(ok?"OK":"control error"));
            if (ok) Audio::ok(); else Audio::err();
            showFb(ok?FB::OK:FB::Err, msg);
            fetchServices();
            break;
        }

        case Screen::Actions: {
            if (actCount == 0) break;
            executeActionItem(actions[cur()]);
            break;
        }

        case Screen::Gitea: {
            fetchGitea();
            Audio::click();
            showFb(FB::OK, gitea.fresh ? "Gitea refreshed" : "Gitea offline", 1500);
            break;
        }

        case Screen::Favorites: {
            if (favoriteCount == 0) break;
            FavoriteItem& fav = favorites[cur()];
            if (!strcmp(fav.kind, "service")) {
                const ServiceItem* svc = findServiceById(fav.id);
                if (!svc) {
                    showFb(FB::Err, "Pinned service no longer exists", 3000);
                    Audio::err();
                    break;
                }
                for (int i = 0; i < svcCount; i++) {
                    if (!strcmp(services[i].id, svc->id)) {
                        selectedSvc = i;
                        cur() = i;
                        cursors[(int)Screen::SvcAction] = 0;
                        Audio::click();
                        navigate(Screen::SvcAction);
                        break;
                    }
                }
            } else {
                const ActionItem* act = findActionById(fav.id);
                if (!act) {
                    showFb(FB::Err, "Pinned action no longer exists", 3000);
                    Audio::err();
                    break;
                }
                executeActionItem(*act);
            }
            break;
        }

        case Screen::Alerts: {
            if (alertCount == 0) break;
            // Mark all as read, clear on Pi side
            bridgeDelete("/api/v1/cardputer/alerts");
            alertCount  = 0;
            unreadCount = 0;
            Audio::ok();
            showFb(FB::OK, "Alerts cleared", 2000);
            break;
        }

        case Screen::IRRemote: {
            int keyIdx = irRow * IR_GRID_COLS + irCol;
            int profIdx = rconfig.ir_device % IR_PROFILE_COUNT;
            const IRProfile& prof = IR_PROFILES[profIdx];
            if (keyIdx < prof.count) {
                irSend(prof.keys[keyIdx]);
                Audio::irSent();
                char msg[32];
                snprintf(msg, sizeof(msg), "Sent: %s", prof.keys[keyIdx].label);
                showFb(FB::OK, msg, 1200);
            }
            break;
        }

        case Screen::NetScan: {
            if (netConnectMode) {
                // Attempt connection with typed password
                if (netConnectIdx < 0 || netConnectIdx >= netAPCount) {
                    netConnectMode = false; drawNetScan(); break;
                }
                const NetAP& ap = netAPs[netConnectIdx];
                showFb(FB::Pending, "Connecting...", 15000);
                WiFi.disconnect(true); delay(100);
                if (ap.open) WiFi.begin(ap.ssid);
                else         WiFi.begin(ap.ssid, netConnectBuf);
                unsigned long t0 = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(200);
                if (WiFi.status() == WL_CONNECTED) {
                    offlineMode = false;
                    ensureDeviceWebServer();
                    // Save to first free WiFi slot
                    for (int i = 0; i < 3; i++) {
                        if (!rconfig.wifi_ssid[i][0] ||
                            strcmp(rconfig.wifi_ssid[i], ap.ssid) == 0) {
                            strlcpy(rconfig.wifi_ssid[i], ap.ssid, 64);
                            strlcpy(rconfig.wifi_pass[i], ap.open ? "" : netConnectBuf, 64);
                            saveConfig(); break;
                        }
                    }
                    // Re-sync NTP with new connection
                    long tzSec = (long)rconfig.tz_offset * 3600L;
                    configTime(tzSec, 0, "pool.ntp.org", "time.cloudflare.com");
                    Audio::ok(); showFb(FB::OK, "Connected!", 3000);
                } else {
                    Audio::err(); showFb(FB::Err, "Connection failed", 4000);
                }
                netConnectMode = false;
                drawNetScan();
                break;
            }
            if (netAPCount == 0 || cur() < 0 || cur() >= netAPCount) {
                // No AP selected — run a scan
                netScanning = true;
                drawNetScan();
                int n = WiFi.scanNetworks(false, true);
                netAPCount = 0;
                if (n > 0) {
                    int limit = min(n, MAX_NET);
                    for (int i = 0; i < limit; i++) {
                        NetAP& a = netAPs[netAPCount++];
                        strlcpy(a.ssid, WiFi.SSID(i).c_str(), sizeof(a.ssid));
                        a.rssi    = WiFi.RSSI(i);
                        a.channel = (uint8_t)WiFi.channel(i);
                        a.open    = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
                    }
                    WiFi.scanDelete();
                }
                netScanning = false;
                drawNetScan();
            } else {
                // AP selected — connect (open) or prompt password (secured)
                netConnectIdx = cur();
                const NetAP& ap = netAPs[netConnectIdx];
                if (ap.open) {
                    showFb(FB::Pending, "Connecting...", 10000);
                    WiFi.disconnect(true); delay(100);
                    WiFi.begin(ap.ssid);
                    unsigned long t0 = millis();
                    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(200);
                    if (WiFi.status() == WL_CONNECTED) {
                        offlineMode = false;
                        ensureDeviceWebServer();
                        for (int i = 0; i < 3; i++) {
                            if (!rconfig.wifi_ssid[i][0]) {
                                strlcpy(rconfig.wifi_ssid[i], ap.ssid, 64);
                                rconfig.wifi_pass[i][0] = '\0';
                                saveConfig(); break;
                            }
                        }
                        Audio::ok(); showFb(FB::OK, "Connected!", 3000);
                    } else {
                        Audio::err(); showFb(FB::Err, "Connection failed", 4000);
                    }
                    drawNetScan();
                } else {
                    memset(netConnectBuf, 0, sizeof(netConnectBuf));
                    netConnectMode = true;
                    Audio::click();
                    drawNetScan();
                }
            }
            break;
        }

        case Screen::Notes: {
            // Enter edit mode for selected note
            noteEditIdx = cur();
            strlcpy(noteEditBuf, notes[noteEditIdx], NOTE_LEN);
            noteEditMode = true;
            Audio::click();
            drawNotes();
            break;
        }

        case Screen::Settings: {
            int fi = cur();
            if (fi == SF_DISCARD) { goBack(); break; }
            if (fi == SF_SAVE) {
                rconfig = pendingConfig;
                saveConfig();
                strlcpy(g_bridge_host, rconfig.bridge_host, sizeof(g_bridge_host));
                strlcpy(g_bridge_psk,  rconfig.bridge_psk,  sizeof(g_bridge_psk));
                g_themeIdx = rconfig.theme_idx;
                Audio::init(rconfig.spk_volume);
                Audio::ok();
                showFb(FB::OK, "Settings saved!", 2500);
                goBack();
                break;
            }
            if (sfIsToggle(fi)) {
                if (fi == (int)SF::PinEnable)  pendingConfig.pin_enabled = !pendingConfig.pin_enabled;
                if (fi == (int)SF::IdleAnim)   pendingConfig.idle_anim   = !pendingConfig.idle_anim;
                drawSettings(); break;
            }
            if (sfIsCycle(fi)) {
                sfCycle(fi);
                drawSettings(); break;
            }
            settingsEditField = fi;
            sfGet(fi, settingsEditBuf, sizeof(settingsEditBuf));
            settingsEditMode = true;
            Audio::click();
            drawSettings();
            break;
        }

        case Screen::USBMacro: {
            if (macroEditMode) {
                // Save the field being edited
                int fi = macroEditField;
                if (fi == 0) { // TIME — parse HH:MM
                    uint8_t h = rconfig.macro_hour;
                    uint8_t m = rconfig.macro_min;
                    if (!parseMacroTimeInput(macroEditBuf, h, m)) {
                        Audio::err();
                        drawUSBMacro();
                        showFb(FB::Err, "Use HH:MM, e.g. 09:30", 2500);
                        break;
                    }
                    rconfig.macro_hour = h;
                    rconfig.macro_min  = m;
                } else if (fi == 1) {
                    strlcpy(rconfig.macro_text, macroEditBuf, sizeof(rconfig.macro_text));
                } else if (fi == 2) {
                    rconfig.tz_offset = constrain(atoi(macroEditBuf), -12, 14);
                    long tzSec = (long)rconfig.tz_offset * 3600L;
                    configTime(tzSec, 0, "pool.ntp.org");
                } else if (fi == 3) {
                    rconfig.macro_enabled = (macroEditBuf[0]=='A'||macroEditBuf[0]=='a'||macroEditBuf[0]=='1');
                    macroFired = false;
                }
                saveConfig();
                macroEditMode = false;
                macroEditFresh = false;
                Audio::ok();
                drawUSBMacro();
                break;
            }
            int fi = constrain(cur(), 0, 4);
            if (fi == 3) { // toggle armed
                rconfig.macro_enabled = !rconfig.macro_enabled;
                macroFired = false;
                saveConfig();
                Audio::click();
                drawUSBMacro();
                break;
            }
            if (fi == 4) break; // LAST field - read only
            macroEditField = fi;
            if (fi == 0) snprintf(macroEditBuf, sizeof(macroEditBuf), "%02d:%02d",
                                   rconfig.macro_hour, rconfig.macro_min);
            else if (fi == 1) strlcpy(macroEditBuf, rconfig.macro_text, sizeof(macroEditBuf));
            else if (fi == 2) snprintf(macroEditBuf, sizeof(macroEditBuf), "%d", rconfig.tz_offset);
            macroEditMode = true;
            macroEditFresh = true;
            Audio::click();
            drawUSBMacro();
            break;
        }

        default: break;
    }
}

void handleChar(char c) {
    if (c == 27) { escapeToMain(); return; }
    if (c == '\n' || c == '\r') { onEnter(); return; }
    if (c == '\b' || c == 127)  { handleDel(); return; }

    // Terminal command entry
    if (current == Screen::Terminal) {
        if (termCmdMode) {
            int len = strlen(termCmdBuf);
            if (len < (int)sizeof(termCmdBuf) - 1 && (uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
                termCmdBuf[len] = c; termCmdBuf[len+1] = '\0';
                drawTerminal();
            }
        } else if (c == '/' || c == ':') {
            termCmdMode = true;
            drawTerminal();
        }
        return;
    }

    // Browser URL entry
    if (browserUrlMode && current == Screen::Browser) {
        int len = strlen(browserUrlBuf);
        if (len < (int)sizeof(browserUrlBuf) - 1 && (uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
            browserUrlBuf[len] = c; browserUrlBuf[len+1] = '\0';
            drawBrowserUrlBar();
        }
        return;
    }

    // WiFi connect password entry
    if (netConnectMode) {
        int len = strlen(netConnectBuf);
        if (len < 63 && (uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
            netConnectBuf[len] = c; netConnectBuf[len + 1] = '\0';
            drawNetScan();
        }
        return;
    }

    // Macro edit mode
    if (macroEditMode) {
        if (macroEditFresh) {
            macroEditBuf[0] = '\0';
            macroEditFresh = false;
        }
        if (macroEditField == 0 && !(isdigit((unsigned char)c) || c == ':')) {
            return;
        }
        int len = strlen(macroEditBuf);
        int maxLen = (macroEditField == 0) ? 5 : (int)sizeof(macroEditBuf) - 1;
        if (len < maxLen && (uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
            macroEditBuf[len] = c; macroEditBuf[len+1] = '\0';
            drawUSBMacro();
        }
        return;
    }

    // Note edit mode
    if (noteEditMode) {
        int len = strlen(noteEditBuf);
        if (len < NOTE_LEN - 1 && (uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
            noteEditBuf[len] = c; noteEditBuf[len+1] = '\0';
            drawNotes();
        }
        return;
    }

    // Settings edit mode
    if (settingsEditMode) {
        int len = strlen(settingsEditBuf);
        if (len < 79 && (uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) {
            settingsEditBuf[len] = c; settingsEditBuf[len+1] = '\0';
            drawSettingsEditBar();
        }
        return;
    }

    // Confirm dialog: Y = yes, anything else = cancel
    if (confirmPending) {
        if (c == 'y' || c == 'Y') onEnter(); else handleDel();
        return;
    }

    if (current == Screen::Browser) {
        if (c == 'b' || c == 'B') {
            browserBookmarkMode = true;
            browserUrlMode = false;
            browserLinkMode = false;
            cur() = 0;
            drawBrowser();
            return;
        }
        if (!browserUrlMode && !browserBookmarkMode) {
            if (c == 'o' || c == 'O') {
                if (browserLinkCount > 0) {
                    browserLinkMode = !browserLinkMode;
                    Audio::click();
                    drawBrowser();
                }
                return;
            }
            if (browserLinkMode && (c == '\t' || c == ' ' || c == 'e' || c == 'E')) {
                browserLinkMove(1);
                return;
            }
            if (browserLinkMode && (c == 'q' || c == 'Q')) {
                browserLinkMove(-1);
                return;
            }
            if (browserLinkMode) {
                if (c == 'g') { browserLinkCursor = 0; browserLinkScroll = 0; Audio::click(); drawBrowser(); return; }
                if (c == 'G') {
                    browserLinkCursor = max(0, browserLinkCount - 1);
                    browserLinkScroll = max(0, browserLinkCount - BR_VIS);
                    Audio::click();
                    drawBrowser();
                    return;
                }
            }
            if (c == '\t' || c == ' ' || c == 'e' || c == 'E') {
                browserScrollBy(BR_VIS);
                return;
            }
            if (c == 'q' || c == 'Q') {
                browserScrollBy(-BR_VIS);
                return;
            }
            if (c == 'g') {
                browserScrollTo(0);
                return;
            }
            if (c == 'G') {
                browserScrollTo(max(0, browserLineCount - BR_VIS));
                return;
            }
            if (c == 'u' || c == 'U') {
                browserScrollBy(-(BR_VIS / 2 + 1));
                return;
            }
            if (c == 'j' || c == 'J') {
                browserScrollBy(1);
                return;
            }
            if (c == 'k' || c == 'K') {
                browserScrollBy(-1);
                return;
            }
        }
        if (c == '/') {
            browserBookmarkMode = false;
            browserUrlMode = true;
            browserLinkMode = false;
            browserUrlBuf[0] = '\0';
            drawBrowser();
            return;
        }
    }

    // WASD + Cardputer symbol navigation
    if (c=='w'||c=='W'||c==';') { onUp();   return; }
    if (c=='s'||c=='S'||c=='.') { onDown(); return; }
    if (c=='a'||c=='A'||c==','||c=='\b'||c==127) {
        if (current==Screen::Main) onLeft(); else goBack(); return;
    }
    if (c=='d'||c=='D'||c=='/') {
        if (current==Screen::Main) onRight(); else onEnter(); return;
    }

    if (c=='h'||c=='H') {
        if (current == Screen::Info || current == Screen::NetScan || current == Screen::Settings) {
            retryBridgeHost(true);
            return;
        }
    }

    if (c=='f'||c=='F') {
        if (current == Screen::Services && svcCount > 0) {
            const ServiceItem& svc = services[cur()];
            bool added = toggleFavorite("service", svc.id, svc.name);
            char msg[56]; snprintf(msg, sizeof(msg), "%s: %s",
                                   added ? "Pinned" : "Unpinned", svc.name);
            Audio::click();
            showFb(added ? FB::OK : FB::Pending, msg, 1800);
            drawServices();
            return;
        }
        if (current == Screen::SvcAction && selectedSvc >= 0 && selectedSvc < svcCount) {
            const ServiceItem& svc = services[selectedSvc];
            bool added = toggleFavorite("service", svc.id, svc.name);
            char msg[56]; snprintf(msg, sizeof(msg), "%s: %s",
                                   added ? "Pinned" : "Unpinned", svc.name);
            Audio::click();
            showFb(added ? FB::OK : FB::Pending, msg, 1800);
            drawSvcAction();
            return;
        }
        if (current == Screen::Actions && actCount > 0) {
            const ActionItem& a = actions[cur()];
            bool added = toggleFavorite("action", a.id, a.label);
            char msg[56]; snprintf(msg, sizeof(msg), "%s: %.34s",
                                   added ? "Pinned" : "Unpinned", a.label);
            Audio::click();
            showFb(added ? FB::OK : FB::Pending, msg, 1800);
            drawActions();
            return;
        }
        if (current == Screen::Favorites && favoriteCount > 0) {
            FavoriteItem fav = favorites[cur()];
            bool added = toggleFavorite(fav.kind, fav.id, fav.label);
            if (cur() >= favoriteCount) cur() = max(0, favoriteCount - 1);
            clampScroll(max(1, favoriteCount));
            char msg[56]; snprintf(msg, sizeof(msg), "%s: %.34s",
                                   added ? "Pinned" : "Unpinned", fav.label);
            Audio::click();
            showFb(added ? FB::OK : FB::Pending, msg, 1800);
            drawFavorites();
            return;
        }
        navigate(Screen::Favorites); return;
    }

    // Global shortcuts
    switch (c) {
        case 'r': case 'R':
            if (offlineMode) {
                retryBridgeHost(true);
                return;
            }
            fetchDash();
            fetchForScreen(current);
            drawScreen();
            return;

        case 'b': case 'B':
            showFb(FB::Pending, "Gitea backup starting...", 8000);
            { JsonDocument doc;
              int code = bridgePost("/api/v1/actions/gitea-gitea-backup/run", doc);
              bool ok = (code>=200&&code<300)&&(bool)(doc["ok"]|false);
              const char* bMsg = doc["message"]|doc["error"]|"";
              if (ok) Audio::ok(); else Audio::err();
              showFb(ok?FB::OK:FB::Err, ok?(bMsg[0]?bMsg:"Backup started"):(bMsg[0]?bMsg:"Backup failed")); }
            return;

        case 'g': case 'G':
            showFb(FB::Pending, "Syncing to Android...", 8000);
            { JsonDocument doc;
              int code = bridgePost("/api/v1/actions/gitea-gitea-sync-android/run", doc);
              bool ok = (code>=200&&code<300)&&(bool)(doc["ok"]|false);
              const char* gMsg = doc["message"]|doc["error"]|"";
              if (ok) Audio::ok(); else Audio::err();
              showFb(ok?FB::OK:FB::Err, ok?(gMsg[0]?gMsg:"Sync started"):(gMsg[0]?gMsg:"Sync failed")); }
            return;

        case 'x': case 'X':
            confirmPending = true;
            showFb(FB::Pending, "Shutdown Pi?  Enter/Y=YES  Del/N=NO", 20000);
            return;

        case 'l': case 'L':
            navigate(Screen::Log); return;

        case 't': case 'T':
            navigate(Screen::Notes); return;

        case 'n': case 'N':
            navigate(Screen::NetScan); return;

        case 'u': case 'U':
            if (current == Screen::Info) {
                if (!otaChecked || !otaInfo.available) {
                    showFb(FB::Pending, "Checking for update...", 6000);
                    bool ok = otaCheck(otaInfo);
                    otaChecked = true;
                    if (!ok) {
                        Audio::err();
                        showFb(FB::Err, "OTA bridge unreachable", 4000);
                    } else if (otaInfo.available) {
                        char msg[56];
                        snprintf(msg, sizeof(msg), "v%s ready! Press U again to flash", otaInfo.version);
                        Audio::warn(); showFb(FB::OK, msg, 8000);
                    } else {
                        char msg[56];
                        snprintf(msg, sizeof(msg), "Latest installed: v%s", otaInfo.version);
                        Audio::ok(); showFb(FB::OK, msg, 4000);
                    }
                    drawScreen();
                } else {
                    showFb(FB::Pending, "Flashing OTA... do not power off", 120000);
                    bool ok = otaApply(otaInfo.url, nullptr);
                    if (ok) {
                        showFb(FB::OK, "Flash done! Restarting...", 2000);
                        delay(2000);
                        ESP.restart();
                    } else {
                        Audio::err();
                        showFb(FB::Err, "OTA failed — check bridge log", 6000);
                    }
                }
            }
            return;
    }

    // Main menu digit shortcuts (1-9, 0 = item 10)
    if (current == Screen::Main && ((c >= '1' && c <= '9') || c == '0')) {
        int idx = (c == '0') ? 9 : (c - '1');
        if (idx < MAIN_COUNT) navigate(MAIN_TARGETS[idx]);
    }
}

void handleHid(uint8_t hid) {
    if (hid == HID_ESC)   { escapeToMain(); return; }
    if (current == Screen::Browser && !browserUrlMode && !browserBookmarkMode) {
        if (hid == HID_LEFT)  { browserScrollBy(-BR_VIS); return; }
        if (hid == HID_RIGHT) { browserScrollBy(BR_VIS); return; }
    }
    if (hid == HID_UP)    { onUp();    return; }
    if (hid == HID_DOWN)  { onDown();  return; }
    if (hid == HID_LEFT)  { onLeft();  return; }
    if (hid == HID_RIGHT) { onRight(); return; }
}

// ─────────────────────────────────────────────────────────────────────────────
// Feedback overlay
// ─────────────────────────────────────────────────────────────────────────────
void showFb(FB state, const char* msg, unsigned long dur) {
    fb      = state;
    fbUntil = millis() + dur;
    strlcpy(fbMsg, msg, sizeof(fbMsg));
    drawFb();
}

void drawFb() {
    if (confirmPending) {
        int bx=6, by=18, bw=SCREEN_W-12, bh=88;
        M5Cardputer.Display.fillRect(bx, by, bw, bh, COL_BG);
        M5Cardputer.Display.drawRect(bx, by, bw, bh, COL_ERR);
        M5Cardputer.Display.fillRect(bx, by, bw, 14, COL_ERR);
        M5Cardputer.Display.setTextColor(0xFFFF, COL_ERR);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(bx+6, by+3);
        M5Cardputer.Display.print("!! CONFIRM REQUIRED !!");
        M5Cardputer.Display.setTextColor(COL_WARN, COL_BG);
        M5Cardputer.Display.setCursor(bx+6, by+20);
        char line[36]; strncpy(line, fbMsg, 35); line[35]='\0';
        M5Cardputer.Display.print(line);
        M5Cardputer.Display.setTextColor(COL_OK, COL_BG);
        M5Cardputer.Display.setCursor(bx+6, by+40);
        M5Cardputer.Display.print("Enter / Y  =  YES, do it");
        M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
        M5Cardputer.Display.setCursor(bx+6, by+54);
    M5Cardputer.Display.print("Backspace/A=NO, cancel");
        return;
    }
    uint16_t col; const char* tag;
    switch (fb) {
        case FB::Pending: col=COL_WARN; tag="... "; break;
        case FB::OK:      col=COL_OK;   tag=" OK "; break;
        case FB::Err:     col=COL_ERR;  tag="ERR!"; break;
        default: return;
    }
    int oy = (SCREEN_H - 32) / 2;
    M5Cardputer.Display.fillRect(0, oy, SCREEN_W, 32, COL_BG);
    M5Cardputer.Display.drawRect(0, oy, SCREEN_W, 32, col);
    M5Cardputer.Display.fillRect(0, oy, 30, 32, col);
    M5Cardputer.Display.setTextColor(COL_BG, col);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(3, oy+12);
    M5Cardputer.Display.print(tag);
    M5Cardputer.Display.setTextColor(COL_TEXT, COL_BG);
    M5Cardputer.Display.setCursor(34, oy+5);
    char l1[32]; strncpy(l1, fbMsg, 31); l1[31]='\0';
    M5Cardputer.Display.print(l1);
    if (strlen(fbMsg) > 31) {
        M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
        M5Cardputer.Display.setCursor(34, oy+18);
        char l2[32]; strncpy(l2, fbMsg+31, 31); l2[31]='\0';
        M5Cardputer.Display.print(l2);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// drawScreen dispatcher
// ─────────────────────────────────────────────────────────────────────────────
void drawScreen() {
    switch (current) {
        case Screen::Main:      drawMainMenu();  break;
        case Screen::Dashboard: drawDashboard(); break;
        case Screen::Services:  drawServices();  break;
        case Screen::SvcAction: drawSvcAction(); break;
        case Screen::Actions:   drawActions();   break;
        case Screen::Favorites: drawFavorites(); break;
        case Screen::Log:       drawLog();       break;
        case Screen::Alerts:    drawAlerts();    break;
        case Screen::Gitea:     drawGitea();     break;
        case Screen::IRRemote:  drawIRRemote();  break;
        case Screen::NetScan:   drawNetScan();   break;
        case Screen::Notes:     drawNotes();     break;
        case Screen::Settings:  drawSettings();  break;
        case Screen::Info:      drawInfo();      break;
        case Screen::USBMacro:  drawUSBMacro();  break;
        case Screen::Browser:   drawBrowser();   break;
        case Screen::Terminal:  drawTerminal();  break;
        default: break;
    }
    if (fb != FB::None) drawFb();
    pushScreenState();
}

// ─────────────────────────────────────────────────────────────────────────────
// Section icons (hand-drawn pixel art, 9 icons)
// ─────────────────────────────────────────────────────────────────────────────
void drawSectionIcon(int idx, int cx, int cy, uint16_t col, uint16_t bg) {
    switch (idx) {
        case 0: { // Dashboard — bar chart
            M5Cardputer.Display.drawFastHLine(cx-20, cy+19, 40, col);
            M5Cardputer.Display.fillRect(cx-19, cy+5,  11, 14, col);
            M5Cardputer.Display.fillRect(cx- 4, cy-15, 11, 34, col);
            M5Cardputer.Display.fillRect(cx+10, cy- 4, 11, 23, col);
            break;
        }
        case 1: { // Services — grid nodes
            M5Cardputer.Display.fillCircle(cx-14, cy-12, 5, col);
            M5Cardputer.Display.fillCircle(cx+14, cy-12, 5, col);
            M5Cardputer.Display.fillCircle(cx-14, cy+13, 5, col);
            M5Cardputer.Display.fillCircle(cx+14, cy+13, 5, col);
            M5Cardputer.Display.fillRect(cx- 9, cy-13, 18, 3, col);
            M5Cardputer.Display.fillRect(cx- 9, cy+12, 18, 3, col);
            M5Cardputer.Display.fillRect(cx-15, cy- 6,  3, 20, col);
            M5Cardputer.Display.fillRect(cx+13, cy- 6,  3, 20, col);
            break;
        }
        case 2: { // Actions — lightning bolt
            M5Cardputer.Display.fillTriangle(cx+3,cy-20,cx+12,cy-20,cx-5,cy+2,  col);
            M5Cardputer.Display.fillTriangle(cx+3,cy-20,cx-5, cy+2, cx-9,cy+2,  col);
            M5Cardputer.Display.fillTriangle(cx-3,cy+20,cx-12,cy+20,cx+5,cy-2,  col);
            M5Cardputer.Display.fillTriangle(cx-3,cy+20,cx+5, cy-2, cx+9,cy-2,  col);
            break;
        }
        case 3: { // Alerts — bell
            M5Cardputer.Display.fillCircle(cx, cy-6, 12, col);
            M5Cardputer.Display.fillCircle(cx, cy-6,  6, bg);
            M5Cardputer.Display.fillRect(cx-14, cy+4, 28, 6, col);
            M5Cardputer.Display.fillRect(cx- 4, cy+10, 8, 5, bg);
            M5Cardputer.Display.fillCircle(cx, cy+17, 4, col);
            break;
        }
        case 4: { // Activity — document with lines
            int dx=cx-14, dy=cy-18, dw=28, dh=36, fold=8;
            M5Cardputer.Display.fillRect(dx,          dy,      dw-fold, dh,      col);
            M5Cardputer.Display.fillRect(dx+dw-fold,  dy+fold, fold,    dh-fold, col);
            M5Cardputer.Display.fillTriangle(dx+dw-fold, dy, dx+dw, dy+fold, dx+dw-fold, dy+fold, bg);
            for (int li=0; li<5; li++)
                M5Cardputer.Display.drawFastHLine(dx+4, dy+10+li*6, dw-fold-5, bg);
            break;
        }
        case 5: { // IR Remote — signal bars + device
            M5Cardputer.Display.fillRect(cx-6, cy-18, 12, 20, col);
            M5Cardputer.Display.fillCircle(cx, cy+6, 4, col);
            // signal arcs (approximated as arcs of rectangles)
            for (int r=1; r<=3; r++) {
                int ry = cy - 22 - r*5;
                M5Cardputer.Display.drawFastHLine(cx-r*5, ry, r*10, col);
            }
            break;
        }
        case 6: { // Network — antenna + waves
            // Vertical pole
            M5Cardputer.Display.fillRect(cx-2, cy-6, 4, 24, col);
            // Crossbar
            M5Cardputer.Display.fillRect(cx-12, cy+6, 24, 3, col);
            // Signal semicircles
            for (int r=1; r<=3; r++) {
                int rr = r * 6;
                M5Cardputer.Display.drawCircle(cx, cy-6, rr, col);
            }
            break;
        }
        case 7: { // Settings — gear
            M5Cardputer.Display.fillCircle(cx, cy, 12, col);
            M5Cardputer.Display.fillCircle(cx, cy,  5, bg);
            static const int8_t TX[]={0,10,14,10,0,-10,-14,-10};
            static const int8_t TY[]={-15,-10,0,10,15,10,0,-10};
            for (int ti=0;ti<8;ti++)
                M5Cardputer.Display.fillRect(cx+TX[ti]-3, cy+TY[ti]-3, 6, 6, col);
            break;
        }
        case 8: { // Device — Cardputer outline
            M5Cardputer.Display.fillRect(cx-20, cy-15, 40, 24, col);
            M5Cardputer.Display.fillRect(cx-16, cy-12, 32, 16, bg);
            for (int kr=0; kr<4; kr++)
                M5Cardputer.Display.drawFastHLine(cx-18, cy+12+kr*4, 36, col);
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Idle scene helpers
// ─────────────────────────────────────────────────────────────────────────────

void drawIdleSceneText() {
    // No-op — pig communicates mood; status lives in footer
}

void drawIdleScene(bool fullRepaint) {
    uint16_t fg  = thFG();
    uint16_t bg  = thBG();
    uint16_t dim = 0x2104;   // dim star colour
    uint16_t cloudCol = 0x4228;

    if (fullRepaint) {
        M5Cardputer.Display.fillRect(0, RAIN_Y_MIN, SCREEN_W,
                                     RAIN_Y_MAX - RAIN_Y_MIN + 4, bg);
        drawSceneBackground(bg, dim, cloudCol);
        rainInit();
        rainSetCount(rainCountForMood(dash.svcDown, unreadCount, wifiOk()));
        birdInit();
        pigResetTimers();
    } else {
        cloudStep(cloudCol, bg);
        drawStars(dim);
        rainSetCount(rainCountForMood(dash.svcDown, unreadCount, wifiOk()));
        rainStep(fg, bg);
        birdStep(fg, bg);
        grassStep();
    }
    // Pig and grass always drawn (covers any rain holes on those pixels)
    drawPig(fg, bg, dash.svcDown, unreadCount, wifiOk());
    drawGrass(fg, bg);
}

// ─────────────────────────────────────────────────────────────────────────────
// Overlay menu — PORKCHOP style, drawn on top of idle scene
// ─────────────────────────────────────────────────────────────────────────────
// Full-screen menu — exact PORKCHOP layout
// Title textSize(2) centred, separator line, items textSize(2) 18px rows,
// full-width inverted selection highlight, scroll carets.
void drawMainMenu() {
    int idx = constrain(cur(), 0, MAIN_COUNT - 1);

    // Fill screen (menu replaces idle scene completely)
    M5Cardputer.Display.fillScreen(thBG());

    // ── Top bar ──────────────────────────────────────────────────────────────
    M5Cardputer.Display.fillRect(0, 0, SCREEN_W, HEADER_H, thBG());
    M5Cardputer.Display.drawFastHLine(0, HEADER_H - 1, SCREEN_W, thFG());
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(thFG(), thBG());
    M5Cardputer.Display.setCursor(4, 4);
    char nick[13];
    strlcpy(nick, rconfig.device_nickname[0] ? rconfig.device_nickname : DEFAULT_DEVICE_NICKNAME, sizeof(nick));
    M5Cardputer.Display.print(nick);
    if (unreadCount > 0) {
        char b[6]; snprintf(b, sizeof(b), "!%d", unreadCount);
        M5Cardputer.Display.setTextColor(COL_ERR, thBG());
        int bx = 4 + (int)strlen(nick) * 6 + 6;
        M5Cardputer.Display.setCursor(min(bx, SCREEN_W - 68), 4);
        M5Cardputer.Display.print(b);
    }
    drawWifiIcon(SCREEN_W - 58, 4, thBG());
    drawBattery(SCREEN_W - 37, 4, thBG());

    // ── Title ─────────────────────────────────────────────────────────────────
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(thFG(), thBG());
    M5Cardputer.Display.setTextDatum(TC_DATUM);
    M5Cardputer.Display.drawString("< VAULTPI OS >", SCREEN_W / 2, HEADER_H + 3);
    M5Cardputer.Display.setTextDatum(TL_DATUM);

    // Separator line
    int sepY = HEADER_H + 14;
    M5Cardputer.Display.drawFastHLine(10, sepY, SCREEN_W - 20, thFG());

    // ── Items ─────────────────────────────────────────────────────────────────
    int rowH     = 18;
    int visItems = 5;
    int itemsY   = sepY + 2;
    int scroll   = constrain(idx - visItems + 1, 0, max(0, MAIN_COUNT - visItems));
    if (idx < scroll) scroll = idx;

    for (int i = scroll; i < min(scroll + visItems, MAIN_COUNT); i++) {
        bool sel = (i == idx);
        int  iy  = itemsY + (i - scroll) * rowH;

        if (sel) {
            // Full-width highlight in fg colour, text in bg colour (PORKCHOP style)
            M5Cardputer.Display.fillRect(5, iy - 1, SCREEN_W - 10, rowH, thFG());
            M5Cardputer.Display.setTextColor(thBG(), thFG());
        } else {
            M5Cardputer.Display.fillRect(5, iy - 1, SCREEN_W - 10, rowH, thBG());
            M5Cardputer.Display.setTextColor(thFG(), thBG());
        }

        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setCursor(10, iy + 1);
        M5Cardputer.Display.print(sel ? "> " : "  ");
        M5Cardputer.Display.print(MAIN_LABELS[i]);

        // Alert badge on ALERTS item
        if (i == 3 && unreadCount > 0) {
            char bd[5]; snprintf(bd, sizeof(bd), "!%d", unreadCount);
            M5Cardputer.Display.setTextColor(sel ? thBG() : COL_ERR,
                                              sel ? thFG() : thBG());
            M5Cardputer.Display.setCursor(SCREEN_W - (int)strlen(bd) * 12 - 10, iy + 1);
            M5Cardputer.Display.print(bd);
        }
    }

    // ── Scroll carets (textSize 1) ────────────────────────────────────────────
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(thFG(), thBG());
    if (scroll > 0) {
        M5Cardputer.Display.setCursor(SCREEN_W - 12, itemsY + 2);
        M5Cardputer.Display.print("^");
    }
    if (scroll + visItems < MAIN_COUNT) {
        M5Cardputer.Display.setCursor(SCREEN_W - 12, itemsY + visItems * rowH - 10);
        M5Cardputer.Display.print("v");
    }

    // ── Bottom bar ────────────────────────────────────────────────────────────
    M5Cardputer.Display.fillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, thBG());
    M5Cardputer.Display.drawFastHLine(0, SCREEN_H - FOOTER_H, SCREEN_W, thFG());
    M5Cardputer.Display.setTextColor(COL_DIM, thBG());
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(4, SCREEN_H - FOOTER_H + 3);
    M5Cardputer.Display.print("WS=nav  Enter=go  Backspace/A=back");
}

// ─────────────────────────────────────────────────────────────────────────────
// Main screen is just the menu now.
void drawMain() {
    drawMainMenu();
}

// ─────────────────────────────────────────────────────────────────────────────
// Dashboard — bars with sparklines
// ─────────────────────────────────────────────────────────────────────────────
void drawDashboard() {
    M5Cardputer.Display.fillScreen(COL_BG);
    drawHeader("DASHBOARD", dash.fresh ? nullptr : "OFFLINE");

    // Layout: label(30) + bar(96) + value(30) + sparkline(38) + margin(6) = 200px of 240
    int y = CONTENT_Y;
    char val[8];
    int bx = 30, bw = 94;
    int spx = 200, spw = 36, sph = BAR_H;

    snprintf(val, sizeof(val), "%d%%", dash.cpu);
    drawBar(y, "CPU ", dash.cpu, val,
            dash.cpu>85?COL_ERR:dash.cpu>65?COL_WARN:COL_OK, bx, bw);
    drawSparkline(spx, y+3, spw, sph, sparkCpu, sparkCount,
                  dash.cpu>85?COL_ERR:dash.cpu>65?COL_WARN:COL_OK); y+=LINE_H;

    snprintf(val, sizeof(val), "%d%%", dash.ram);
    drawBar(y, "RAM ", dash.ram, val,
            dash.ram>85?COL_ERR:dash.ram>70?COL_WARN:COL_OK, bx, bw);
    drawSparkline(spx, y+3, spw, sph, sparkRam, sparkCount,
                  dash.ram>85?COL_ERR:dash.ram>70?COL_WARN:COL_OK); y+=LINE_H;

    snprintf(val, sizeof(val), "%d%%", dash.disk);
    drawBar(y, "DISK", dash.disk, val,
            dash.disk>90?COL_ERR:dash.disk>75?COL_WARN:COL_OK, bx, bw);
    drawSparkline(spx, y+3, spw, sph, sparkDisk, sparkCount,
                  dash.disk>90?COL_ERR:dash.disk>75?COL_WARN:COL_OK); y+=LINE_H;

    snprintf(val, sizeof(val), "%dC", dash.temp);
    drawBar(y, "TEMP", constrain(dash.temp,0,100), val,
            dash.temp>75?COL_ERR:dash.temp>60?COL_WARN:COL_AMBER, bx, bw);
    drawSparkline(spx, y+3, spw, sph, sparkTemp, sparkCount,
                  dash.temp>75?COL_ERR:dash.temp>60?COL_WARN:COL_AMBER); y+=LINE_H;

    char loadup[32];
    snprintf(loadup, sizeof(loadup), "load %s", dash.load);
    drawKV(y, loadup, dash.uptime, COL_DIM); y+=LINE_H;

    drawRule(y+1); y+=6;

    char svcLine[32];
    snprintf(svcLine, sizeof(svcLine), "%dup  %ddn  /%d svc",
             dash.svcUp, dash.svcDown, dash.svcTotal);
    drawKV(y, "SVC", svcLine, dash.svcDown>0?COL_WARN:COL_OK); y+=LINE_H;

    if (dash.sdErrors > 0) {
        char sdMsg[20]; snprintf(sdMsg, sizeof(sdMsg), "%d SD errors", dash.sdErrors);
        drawKV(y, "SD ", sdMsg, COL_ERR);
    } else if (dash.lastMsg[0]) {
        char trunc[38]; strncpy(trunc, dash.lastMsg, 37); trunc[37]='\0';
        drawKV(y, ">", trunc, eventColor(dash.lastType));
    }

    drawFooter("WS=scroll  A=back  r=refresh",
               wifiOk() ? WiFi.SSID().c_str() : "offline");
}

// ─────────────────────────────────────────────────────────────────────────────
// Services
// ─────────────────────────────────────────────────────────────────────────────
void drawServices() {
    M5Cardputer.Display.fillScreen(COL_BG);
    char hdr[28];
    snprintf(hdr, sizeof(hdr), "SERVICES  %d", svcCount);
    drawHeader(hdr);

    if (svcCount == 0) {
        M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
        M5Cardputer.Display.setTextSize(1);
        const char* em = fetchErrMsg(svcFetchStatus);
        M5Cardputer.Display.setCursor(8, CONTENT_Y+14);
        M5Cardputer.Display.print(em ? em : "No services configured");
        drawFooter("r=retry  A=back", ""); return;
    }

    int y = CONTENT_Y;
    int end = min(scr()+VIS, svcCount);
    for (int i = scr(); i < end; i++) {
        const char* tag = statusTag(services[i].status);
        uint16_t tc     = statusColor(services[i].status);
        bool sel        = (i == cur());
        char label[56];
        snprintf(label, sizeof(label), "%s%s", isFavorite("service", services[i].id) ? "* " : "", services[i].name);
        drawListItem(y, label, sel, tag, sel?COL_SEL_FG:tc);
        y += LINE_H;
    }
    char pos[14]=""; if(svcCount>VIS) snprintf(pos,sizeof(pos),"%d-%d/%d",scr()+1,end,svcCount);
    drawFooter("WS=nav  D=actions  f=pin  A=back", pos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Service action sub-menu
// ─────────────────────────────────────────────────────────────────────────────
void drawSvcAction() {
    if (selectedSvc < 0 || selectedSvc >= svcCount) { goBack(); return; }
    const ServiceItem& svc = services[selectedSvc];
    M5Cardputer.Display.fillScreen(COL_BG);
    drawHeader(svc.name);

    int y = CONTENT_Y + 2;
    M5Cardputer.Display.setTextColor(statusColor(svc.status), COL_BG);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(6, y);
    M5Cardputer.Display.printf("Status: %s", svc.status); y += LINE_H;
    M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
    M5Cardputer.Display.setCursor(6, y);
    M5Cardputer.Display.printf("Kind:   %s", svc.kind); y += LINE_H;
    M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
    M5Cardputer.Display.setCursor(6, y);
    M5Cardputer.Display.print("Choose action:"); y += LINE_H + 2;

    int c = cursors[(int)Screen::SvcAction];
    for (int i = 0; i < SVC_ACT_COUNT; i++) {
        uint16_t col = (i==SVC_ACT_COUNT-1) ? COL_DIM : COL_AMBER;
        drawListItem(y, SVC_ACTIONS[i], i==c, nullptr, col);
        y += LINE_H;
    }
    drawFooter("WS=nav  D/Enter=run  A=back", "");
}

// ─────────────────────────────────────────────────────────────────────────────
// Actions
// ─────────────────────────────────────────────────────────────────────────────
void drawActions() {
    M5Cardputer.Display.fillScreen(COL_BG);
    char hdr[28]; snprintf(hdr, sizeof(hdr), "ACTIONS  %d", actCount);
    drawHeader(hdr);

    if (actCount == 0) {
        M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
        M5Cardputer.Display.setTextSize(1);
        const char* em = fetchErrMsg(actFetchStatus);
        M5Cardputer.Display.setCursor(8, CONTENT_Y+14);
        M5Cardputer.Display.print(em ? em : "No actions available");
        drawFooter("r=retry  A=back", ""); return;
    }

    int y = CONTENT_Y;
    int end = min(scr()+VIS, actCount);
    for (int i = scr(); i < end; i++) {
        const ActionItem& a = actions[i];
        bool sel = (i == cur());
        char tag[7]=""; uint16_t tc=COL_DIM;
        if (!strcmp(a.kind,"gitea"))   { strlcpy(tag, statusTag(a.status), 7); tc=statusColor(a.status); }
        else if (!strcmp(a.kind,"command")) strlcpy(tag," CMD",7);
        else                               strlcpy(tag," SYS",7);
        char label[56];
        snprintf(label, sizeof(label), "%s%s", isFavorite("action", a.id) ? "* " : "", a.label);
        drawListItem(y, label, sel, tag, sel?COL_SEL_FG:tc);
        y += LINE_H;
    }
    char pos[14]=""; if(actCount>VIS) snprintf(pos,sizeof(pos),"%d-%d/%d",scr()+1,end,actCount);
    drawFooter("WS=nav  D=run  f=pin  A=back", pos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Favorites
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void drawFavorites() {
    M5Cardputer.Display.fillScreen(COL_BG);
    char hdr[28];
    snprintf(hdr, sizeof(hdr), "FAVORITES  %d", favoriteCount);
    drawHeader(hdr);

    if (favoriteCount == 0) {
        M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, CONTENT_Y + 14);
        M5Cardputer.Display.print("Press f on Services or Actions to pin");
        M5Cardputer.Display.setCursor(8, CONTENT_Y + 28);
        M5Cardputer.Display.print("A=back");
        drawFooter("f=pin  Enter=open  A=back", "");
        return;
    }

    int y = CONTENT_Y;
    int end = min(scr() + VIS, favoriteCount);
    for (int i = scr(); i < end; i++) {
        const FavoriteItem& f = favorites[i];
        bool sel = (i == cur());
        char label[56];
        char tag[8] = "";
        uint16_t tc = COL_DIM;
        if (!strcmp(f.kind, "service")) {
            const ServiceItem* svc = findServiceById(f.id);
            if (svc) {
                strlcpy(label, svc->name, sizeof(label));
                strlcpy(tag, statusTag(svc->status), sizeof(tag));
                tc = statusColor(svc->status);
            } else {
                strlcpy(label, f.label, sizeof(label));
                strlcpy(tag, "SVC", sizeof(tag));
            }
        } else {
            const ActionItem* act = findActionById(f.id);
            if (act) {
                strlcpy(label, act->label, sizeof(label));
                if (!strcmp(act->kind, "gitea")) {
                    strlcpy(tag, statusTag(act->status), sizeof(tag));
                    tc = statusColor(act->status);
                } else if (!strcmp(act->kind, "command")) {
                    strlcpy(tag, "CMD", sizeof(tag));
                } else {
                    strlcpy(tag, "SYS", sizeof(tag));
                }
            } else {
                strlcpy(label, f.label, sizeof(label));
                strlcpy(tag, "ACT", sizeof(tag));
            }
        }
        drawListItem(y, label, sel, tag, sel ? COL_SEL_FG : tc);
        y += LINE_H;
    }

    char pos[14] = "";
    if (favoriteCount > VIS) snprintf(pos, sizeof(pos), "%d-%d/%d", scr()+1, end, favoriteCount);
    drawFooter("Enter=open  f=unpin  A=back", pos);
}

void drawGitea() {
    M5Cardputer.Display.fillScreen(COL_BG);
    char hdr[30];
    snprintf(hdr, sizeof(hdr), "GITEA  %s", gitea.username[0] ? gitea.username : "n/a");
    drawHeader(hdr, gitea.fresh ? (gitea.serverVer[0] ? gitea.serverVer : "heatmap") : "offline");

    int y = CONTENT_Y;
    char val[72];

    if (gitea.fullName[0]) {
        drawKV(y, "NAME", gitea.fullName, COL_TEXT); y += LINE_H;
    }
    snprintf(val, sizeof(val), "%d repos  %d stars  %d followers", gitea.repos, gitea.stars, gitea.followers);
    drawKV(y, "STAT", val, COL_AMBER); y += LINE_H;
    snprintf(val, sizeof(val), "%d total  %d active  peak %d", gitea.total, gitea.activeDays, gitea.peak);
    drawKV(y, "DAY ", val, COL_OK); y += LINE_H;

    drawRule(y + 1);
    y += 6;

    int gridX = 8;
    int gridY = y + 2;
    int cell  = 6;
    int gap   = 1;
    int cols  = 12;
    int rows  = 7;
    int idx   = 0;
    for (int c = 0; c < cols; c++) {
        for (int r = 0; r < rows; r++) {
            int x = gridX + c * (cell + gap);
            int yy = gridY + r * (cell + gap);
            uint8_t level = (idx < 84) ? gitea.cells[idx].level : 0;
            M5Cardputer.Display.fillRect(x, yy, cell, cell, giteaHeatColor(level));
            idx++;
        }
    }

    M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(8, gridY + rows * (cell + gap) + 2);
    M5Cardputer.Display.print("r=refresh  Enter=refresh  A=back");
    drawFooter("r=refresh  Enter=refresh  A=back", "");
}

// Activity Log
// ─────────────────────────────────────────────────────────────────────────────
void drawLog() {
    M5Cardputer.Display.fillScreen(COL_BG);
    char hdr[28]; snprintf(hdr, sizeof(hdr), "ACTIVITY  %d", logCount);
    drawHeader(hdr);

    if (logCount == 0) {
        const char* em = fetchErrMsg(logFetchStatus);
        M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, CONTENT_Y+14);
        M5Cardputer.Display.print(em ? em : "No activity yet");
        drawFooter("r=retry  A=back", ""); return;
    }

    int y = CONTENT_Y;
    int end = min(scr()+VIS, logCount);
    for (int i = scr(); i < end; i++) {
        uint16_t col = eventColor(logItems[i].type);
        uint16_t bg  = (((i-scr())%2)==0) ? COL_BG2 : COL_BG;
        M5Cardputer.Display.fillRect(0, y, SCREEN_W, LINE_H, bg);
        M5Cardputer.Display.fillRect(0, y, 3, LINE_H, col);
        M5Cardputer.Display.setTextColor(col, bg);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(7, y+3);
        char buf[38]; strncpy(buf, logItems[i].message, 37); buf[37]='\0';
        M5Cardputer.Display.print(buf);
        y += LINE_H;
    }
    char pos[16]; snprintf(pos, sizeof(pos), "%d-%d/%d", scr()+1, end, logCount);
    drawFooter("WS=scroll  A=back  r=refresh", pos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Alerts
// ─────────────────────────────────────────────────────────────────────────────
void drawAlerts() {
    M5Cardputer.Display.fillScreen(COL_BG);
    char hdr[28];
    if (unreadCount > 0)
        snprintf(hdr, sizeof(hdr), "ALERTS  !%d unread", unreadCount);
    else
        snprintf(hdr, sizeof(hdr), "ALERTS  %d", alertCount);
    drawHeader(hdr);

    if (alertCount == 0) {
        M5Cardputer.Display.setTextColor(COL_OK, COL_BG);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, CONTENT_Y+18);
        M5Cardputer.Display.print("All clear — no alerts");
        M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
        M5Cardputer.Display.setCursor(8, CONTENT_Y+32);
        M5Cardputer.Display.print("r=refresh  A=back");
        drawFooter("r=refresh  A=back", ""); return;
    }

    int y = CONTENT_Y;
    int end = min(scr()+VIS, alertCount);
    for (int i = scr(); i < end; i++) {
        uint16_t lc  = alertLevelColor(alerts[i].level);
        uint16_t bg  = alerts[i].read ? COL_BG : COL_BG2;
        bool sel     = (i == cur());
        if (sel) bg  = COL_SEL_BG;
        M5Cardputer.Display.fillRect(0, y, SCREEN_W, LINE_H, bg);
        M5Cardputer.Display.fillRect(0, y, 3, LINE_H, lc);
        if (!alerts[i].read) {
            M5Cardputer.Display.fillRect(4, y+5, 3, 3, COL_AMBER);
        }
        M5Cardputer.Display.setTextColor(sel?COL_SEL_FG:COL_TEXT, bg);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(10, y+3);
        char buf[34]; strncpy(buf, alerts[i].message, 33); buf[33]='\0';
        M5Cardputer.Display.print(buf);
        // level tag
        M5Cardputer.Display.setTextColor(sel?COL_AMBER:lc, bg);
        M5Cardputer.Display.setCursor(SCREEN_W-25, y+3);
        M5Cardputer.Display.print(
            !strcmp(alerts[i].level,"error") ? " ERR" :
            !strcmp(alerts[i].level,"warning") ? "WARN" : "INFO");
        y += LINE_H;
    }
    char pos[14]=""; if(alertCount>VIS) snprintf(pos,sizeof(pos),"%d/%d",scr()+1,alertCount);
    drawFooter("Enter=clear all  A=back  r=ref", pos);
}

// ─────────────────────────────────────────────────────────────────────────────
// IR Remote — 3-row × 4-col button grid
// ─────────────────────────────────────────────────────────────────────────────
void drawIRRemote() {
    M5Cardputer.Display.fillScreen(COL_BG);
    int profIdx = rconfig.ir_device % IR_PROFILE_COUNT;
    const IRProfile& prof = IR_PROFILES[profIdx];
    drawHeader("IR REMOTE", prof.name);

    // Navigation hint: A/D to switch device
    M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(4, CONTENT_Y + 1);
    M5Cardputer.Display.printf("< %s >", prof.name);

    // Button grid — 4 cols, 3 rows
    int btnW = 52, btnH = 20, padX = 8, padY = 4;
    int gridX = (SCREEN_W - (IR_GRID_COLS * btnW + (IR_GRID_COLS-1) * padX)) / 2;
    int gridY = CONTENT_Y + 14;

    for (int row = 0; row < IR_GRID_ROWS; row++) {
        for (int col = 0; col < IR_GRID_COLS; col++) {
            int idx = row * IR_GRID_COLS + col;
            if (idx >= prof.count) continue;
            int bx = gridX + col * (btnW + padX);
            int by = gridY + row * (btnH + padY);
            bool sel = (row == irRow && col == irCol);
            uint16_t bg  = sel ? COL_AMBER : COL_BG2;
            uint16_t fg  = sel ? COL_BG    : COL_TEXT;
            uint16_t brd = sel ? COL_AMBER : COL_DIM;
            M5Cardputer.Display.fillRect(bx, by, btnW, btnH, bg);
            M5Cardputer.Display.drawRect(bx, by, btnW, btnH, brd);
            M5Cardputer.Display.setTextColor(fg, bg);
            M5Cardputer.Display.setTextSize(1);
            int lw = (int)strlen(prof.keys[idx].label) * 6;
            M5Cardputer.Display.setCursor(bx + (btnW-lw)/2, by + 6);
            M5Cardputer.Display.print(prof.keys[idx].label);
        }
    }
    drawFooter("WASD/arrows=nav  Enter=send  A=back", "");
}

// ─────────────────────────────────────────────────────────────────────────────
// Network Scanner — WiFi AP scan + LAN info
// ─────────────────────────────────────────────────────────────────────────────
void drawNetScan() {
    M5Cardputer.Display.fillScreen(COL_BG);
    char hdr[28]; snprintf(hdr, sizeof(hdr), "NETWORK  %d AP", netAPCount);
    drawHeader(hdr);

    int y = CONTENT_Y;

    if (netScanning) {
        M5Cardputer.Display.setTextColor(COL_WARN, COL_BG);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, y + 16);
        M5Cardputer.Display.print("Scanning WiFi networks...");
        drawFooter("please wait...", "");
        return;
    }

    // Own connection info
    if (wifiOk()) {
        char ipLine[32]; snprintf(ipLine, sizeof(ipLine), "%s", WiFi.localIP().toString().c_str());
        int rssi = WiFi.RSSI();
        char rssiLine[24]; snprintf(rssiLine, sizeof(rssiLine), "%s  %ddBm",
                                    rssi > -65 ? "Good" : rssi > -80 ? "Fair" : "Weak", rssi);
        drawKV(y, "IP  ", ipLine,   COL_OK);   y += LINE_H;
        drawKV(y, "RSSI", rssiLine, rssi > -65 ? COL_OK : rssi > -80 ? COL_WARN : COL_ERR);
        y += LINE_H;
    } else {
        drawKV(y, "WiFi", "not connected", COL_ERR); y += LINE_H * 2;
    }

    if (offlineMode) {
        drawKV(y, "Mode", "offline until connect", COL_WARN); y += LINE_H;
    }

    drawRule(y + 1); y += 6;

    if (netConnectMode && netConnectIdx >= 0 && netConnectIdx < netAPCount) {
        // Password entry overlay
        const NetAP& ap = netAPs[netConnectIdx];
        char ssidLine[26]; snprintf(ssidLine, sizeof(ssidLine), "%.24s", ap.ssid);
        drawKV(y, "SSID", ssidLine, COL_AMBER); y += LINE_H;
        // Password field with cursor
        char disp[40]; strlcpy(disp, netConnectBuf, sizeof(disp));
        int bl = strlen(disp); if (bl < 38) { disp[bl] = '_'; disp[bl+1] = '\0'; }
        drawKV(y, "PASS", disp, COL_TEXT);
        drawFooter("type password  Enter=connect  h=retry host  Backspace=back", "");
    } else if (netAPCount == 0) {
        M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, y + 4);
        M5Cardputer.Display.print("Enter=scan  r=scan again");
        drawFooter("Enter=scan  h=retry host  A=back", "");
    } else {
        int selRow = cur();
        int end    = min(scr() + VIS - 2, netAPCount);
        for (int i = scr(); i < end && y < SCREEN_H - FOOTER_H - LINE_H; i++) {
            const NetAP& ap  = netAPs[i];
            bool connected   = (wifiOk() && strcmp(ap.ssid, WiFi.SSID().c_str()) == 0);
            bool sel         = (i == selRow);
            uint16_t bg      = sel ? COL_SEL_BG : (i % 2 == 0 ? COL_BG2 : COL_BG);
            uint16_t fc      = sel ? COL_SEL_FG : (connected ? COL_OK : COL_TEXT);
            uint16_t sc      = ap.rssi > -65 ? COL_OK : ap.rssi > -80 ? COL_WARN : COL_ERR;
            M5Cardputer.Display.fillRect(0, y, SCREEN_W, LINE_H, bg);
            M5Cardputer.Display.fillRect(0, y, 3, LINE_H, sc);
            M5Cardputer.Display.setTextColor(fc, bg);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setCursor(7, y + 3);
            char ssidBuf[22]; strncpy(ssidBuf, ap.ssid, 21); ssidBuf[21] = '\0';
            M5Cardputer.Display.print(ssidBuf);
            // Lock icon and signal info on right
            char info[18];
            snprintf(info, sizeof(info), "%s ch%d %ddBm",
                     ap.open ? " " : "L", ap.channel, ap.rssi);
            M5Cardputer.Display.setTextColor(sel ? COL_SEL_FG : COL_DIM, bg);
            M5Cardputer.Display.setCursor(SCREEN_W - (int)strlen(info) * 6 - 4, y + 3);
            M5Cardputer.Display.print(info);
            y += LINE_H;
        }
        drawFooter("WS=nav  Enter=connect  h=retry host  A=back", "");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Notes — 4-slot notepad, stored in NVS
// ─────────────────────────────────────────────────────────────────────────────
void drawNotes() {
    M5Cardputer.Display.fillScreen(COL_BG);
    drawHeader("NOTES");

    if (!notesLoaded) { loadNotes(notes, MAX_NOTES); notesLoaded = true; }

    int y = CONTENT_Y;
    for (int i = 0; i < MAX_NOTES; i++) {
        bool sel = (i == cur() && !noteEditMode);
        uint16_t bg = sel ? COL_SEL_BG : (i%2==0?COL_BG2:COL_BG);
        uint16_t fg = sel ? COL_SEL_FG : COL_TEXT;
        M5Cardputer.Display.fillRect(0, y, SCREEN_W, LINE_H, bg);
        if (sel) M5Cardputer.Display.fillRect(0, y, 3, LINE_H, COL_AMBER);

        // Slot number
        M5Cardputer.Display.setTextColor(sel?COL_AMBER:COL_DIM, bg);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(5, y+3);
        char num[4]; snprintf(num, sizeof(num), "%d:", i+1);
        M5Cardputer.Display.print(num);

        // Note content or placeholder
        const char* text = notes[i];
        if (noteEditMode && i == noteEditIdx) {
            char buf[36]; strlcpy(buf, noteEditBuf, 34); int _bl=strlen(buf); buf[_bl]='_'; buf[_bl+1]='\0';
            M5Cardputer.Display.setTextColor(COL_AMBER, bg);
            M5Cardputer.Display.setCursor(20, y+3);
            M5Cardputer.Display.print(buf);
        } else if (text[0]) {
            char trunc[30]; strncpy(trunc, text, 29); trunc[29]='\0';
            M5Cardputer.Display.setTextColor(fg, bg);
            M5Cardputer.Display.setCursor(20, y+3);
            M5Cardputer.Display.print(trunc);
        } else {
            M5Cardputer.Display.setTextColor(COL_DIM, bg);
            M5Cardputer.Display.setCursor(20, y+3);
            M5Cardputer.Display.print("(empty)");
        }
        y += LINE_H;
    }

    y += 4; drawRule(y); y += 6;

    // Show full selected note when not editing
    if (!noteEditMode && cur() < MAX_NOTES && notes[cur()][0]) {
        M5Cardputer.Display.setTextColor(COL_TEXT, COL_BG);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(4, y);
        // Word-wrap first 2 lines
        char buf[NOTE_LEN]; strlcpy(buf, notes[cur()], NOTE_LEN);
        int len = strlen(buf);
        if (len > 37) { char l1[38]; strncpy(l1,buf,37); l1[37]='\0'; M5Cardputer.Display.print(l1); }
        else { M5Cardputer.Display.print(buf); }
    }

    if (noteEditMode) {
        drawFooter("type text  Enter=save  Backspace=erase", "");
    } else {
        drawFooter("WS=nav  Enter=edit  Del=clear  A=back", "");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────
void drawSettings() {
    M5Cardputer.Display.fillScreen(COL_BG);
    drawHeader("SETTINGS", "NVS");

    int y = CONTENT_Y;
    int end = min(scr()+VIS, SF_COUNT);
    for (int i = scr(); i < end; i++) {
        bool sel     = (i == cur());
        uint16_t bg  = sel ? COL_SEL_BG : COL_BG;
        uint16_t fg  = sel ? COL_SEL_FG : COL_TEXT;

        M5Cardputer.Display.fillRect(0, y, SCREEN_W, LINE_H, bg);
        if (sel) M5Cardputer.Display.fillRect(0, y, 3, LINE_H, COL_AMBER);

        M5Cardputer.Display.setTextColor(sel?COL_AMBER:COL_DIM, bg);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(5, y+3);
        M5Cardputer.Display.print(sel?">":" ");

        if (sfIsAction(i)) {
            M5Cardputer.Display.setTextColor(i==SF_SAVE?COL_OK:COL_ERR, bg);
            M5Cardputer.Display.setCursor(14, y+3);
            M5Cardputer.Display.print(sfLabel(i));
        } else {
            M5Cardputer.Display.setTextColor(COL_DIM, bg);
            M5Cardputer.Display.setCursor(14, y+3);
            M5Cardputer.Display.print(sfLabel(i));

            char val[64]=""; sfGet(i, val, sizeof(val));
            char disp[20];
            if (sfIsSecret(i) && val[0]) {
                int vl=strlen(val), show=min(vl,3), fill=min(vl-show,8), out=0;
                for (int k=0;k<show&&out<18;k++) disp[out++]=val[k];
                for (int k=0;k<fill&&out<18;k++) disp[out++]='*';
                disp[out]='\0';
            } else {
                strlcpy(disp, val, sizeof(disp)); disp[17]='\0';
            }
            int tw = (int)strlen(disp)*6;
            M5Cardputer.Display.setTextColor(fg, bg);
            M5Cardputer.Display.setCursor(SCREEN_W-tw-4, y+3);
            M5Cardputer.Display.print(disp);
        }
        y += LINE_H;
    }

    if (settingsEditMode) drawSettingsEditBar();
    else drawFooter("WS=nav  Enter=edit  h=retry host  A=back", "");
}

void drawSettingsEditBar() {
    int ey = SCREEN_H - 28;
    M5Cardputer.Display.fillRect(0, ey, SCREEN_W, 28, COL_HEADER);
    M5Cardputer.Display.drawRect(0, ey, SCREEN_W, 28, COL_AMBER);
    M5Cardputer.Display.setTextColor(COL_DIM, COL_HEADER);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(5, ey+3);
    M5Cardputer.Display.print(sfLabel(settingsEditField));
    char disp[40]; strlcpy(disp, settingsEditBuf, 38); strncat(disp, "_", sizeof(disp)-strlen(disp)-1);
    M5Cardputer.Display.setTextColor(COL_TEXT, COL_HEADER);
    M5Cardputer.Display.setCursor(5, ey+15);
    M5Cardputer.Display.print(disp);
}

// ─────────────────────────────────────────────────────────────────────────────
// Web Browser screen — fetch via bridge w3m, display scrollable text
// ─────────────────────────────────────────────────────────────────────────────

void drawBrowserUrlBar() {
    int ey = HEADER_H + 1;
    M5Cardputer.Display.fillRect(0, ey, SCREEN_W, 24, COL_HEADER);
    M5Cardputer.Display.drawRect(0, ey, SCREEN_W, 24, thFG());
    M5Cardputer.Display.setTextColor(COL_DIM, COL_HEADER);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(5, ey + 3);
    M5Cardputer.Display.print("Search or URL:");
    char disp[40]; strlcpy(disp, browserUrlBuf, 36); strncat(disp, "_", sizeof(disp) - strlen(disp) - 1);
    M5Cardputer.Display.setTextColor(COL_TEXT, COL_HEADER);
    M5Cardputer.Display.setCursor(5, ey + 14);
    M5Cardputer.Display.print(disp);
}

void browserScrollTo(int pos) {
    if (browserLineCount <= BR_VIS) pos = 0;
    int maxScroll = max(0, browserLineCount - BR_VIS);
    int next = constrain(pos, 0, maxScroll);
    if (next != browserScroll) {
        browserScroll = next;
        Audio::click();
        drawBrowser();
    }
}

void browserScrollBy(int delta) {
    browserScrollTo(browserScroll + delta);
}

void browserLinkMove(int delta) {
    if (browserLinkCount <= 0) return;
    int next = constrain(browserLinkCursor + delta, 0, browserLinkCount - 1);
    if (next == browserLinkCursor) return;
    browserLinkCursor = next;
    if (browserLinkCursor < browserLinkScroll) browserLinkScroll = browserLinkCursor;
    if (browserLinkCursor >= browserLinkScroll + BR_VIS) browserLinkScroll = browserLinkCursor - BR_VIS + 1;
    Audio::click();
    drawBrowser();
}

void openBrowserLink() {
    if (browserLinkCount <= 0 || browserLinkCursor < 0 || browserLinkCursor >= browserLinkCount) return;
    strlcpy(browserUrlBuf, browserLinkUrls[browserLinkCursor], sizeof(browserUrlBuf));
    browserUrlMode = true;
    browserBookmarkMode = false;
    browserLinkMode = false;
    fetchBrowserPage();
}

// ─────────────────────────────────────────────────────────────────────────────
// Terminal screen
// ─────────────────────────────────────────────────────────────────────────────
void drawTerminal() {
    M5Cardputer.Display.fillScreen(COL_BG);
    drawHeader("TERMINAL", bridgeOnline ? nullptr : "OFFLINE");

    if (termBusy) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
        M5Cardputer.Display.setCursor(8, CONTENT_Y + 28);
        M5Cardputer.Display.print("Running...");
        drawFooter("", "");
        return;
    }

    int y = CONTENT_Y + 4;
    M5Cardputer.Display.setTextSize(1);
    int end = min(termScroll + TERM_VIS, termLineCount);
    for (int i = termScroll; i < end; i++) {
        M5Cardputer.Display.fillRect(0, y, SCREEN_W, LINE_H, COL_BG);
        M5Cardputer.Display.setTextColor(COL_TEXT, COL_BG);
        M5Cardputer.Display.setCursor(2, y + 3);
        M5Cardputer.Display.print(termLines[i]);
        y += LINE_H;
    }
    if (y < CONTENT_Y + TERM_VIS * LINE_H + 4) {
        M5Cardputer.Display.fillRect(0, y, SCREEN_W, CONTENT_Y + TERM_VIS * LINE_H + 4 - y, COL_BG);
    }

    // Command bar
    int barY = CONTENT_Y + 4 + TERM_VIS * LINE_H;
    if (termCmdMode) {
        char bar[42]; snprintf(bar, sizeof(bar), "> %s_", termCmdBuf);
        M5Cardputer.Display.fillRect(0, barY, SCREEN_W, LINE_H, COL_SEL_BG);
        M5Cardputer.Display.setTextColor(COL_SEL_FG, COL_SEL_BG);
        M5Cardputer.Display.setCursor(2, barY + 3);
        M5Cardputer.Display.print(bar);
        char pos[12]; snprintf(pos, sizeof(pos), "%d/%d", termLineCount, TERM_MAX_LINES);
        drawFooter("Enter=run Del=backspace", pos);
    } else {
        M5Cardputer.Display.fillRect(0, barY, SCREEN_W, LINE_H, COL_BG);
        char pos[12]; snprintf(pos, sizeof(pos), "%d lines", termLineCount);
        drawFooter("WS=scroll Enter=cmd Del=clear", pos);
    }
}

void fetchTerminalExec() {
    if (!wifiOk()) {
        if (termLineCount < TERM_MAX_LINES)
            strlcpy(termLines[termLineCount++], "[WiFi offline]", TERM_LINE_LEN + 1);
        termCmdMode = false;
        termScroll = max(0, termLineCount - TERM_VIS);
        drawTerminal();
        return;
    }
    termBusy = true;
    drawTerminal();

    String body = "{\"cmd\":\"";
    for (const char* p = termCmdBuf; *p; ++p) {
        if (*p == '"' || *p == '\\') body += '\\';
        body += *p;
    }
    body += "\"}";

    JsonDocument doc;
    int code = bridgePostBody("/api/v1/terminal/exec", body, doc);
    termBusy = false;

    // Echo the command
    char echo[TERM_LINE_LEN + 1];
    snprintf(echo, sizeof(echo), "$ %s", termCmdBuf);
    if (termLineCount < TERM_MAX_LINES) strlcpy(termLines[termLineCount++], echo, TERM_LINE_LEN + 1);

    if (code != 200) {
        char err[TERM_LINE_LEN + 1];
        if (code < 0) strlcpy(err, "[no response]", sizeof(err));
        else snprintf(err, sizeof(err), "[HTTP %d]", code);
        if (termLineCount < TERM_MAX_LINES) strlcpy(termLines[termLineCount++], err, TERM_LINE_LEN + 1);
    } else {
        bool ok = doc["ok"] | false;
        if (!ok) {
            const char* msg = doc["error"] | "error";
            if (termLineCount < TERM_MAX_LINES) {
                char el[TERM_LINE_LEN + 1];
                strlcpy(el, msg, sizeof(el));
                strlcpy(termLines[termLineCount++], el, TERM_LINE_LEN + 1);
            }
        } else {
            JsonArray arr = doc["lines"].as<JsonArray>();
            for (JsonVariant v : arr) {
                if (termLineCount >= TERM_MAX_LINES) break;
                strlcpy(termLines[termLineCount++], v.as<const char*>(), TERM_LINE_LEN + 1);
            }
            bridgeOnline = true;
            offlineMode = false;
        }
    }

    termCmdBuf[0] = '\0';
    termCmdMode = false;
    termScroll = max(0, termLineCount - TERM_VIS);
    drawTerminal();
}

void drawBrowser() {
    M5Cardputer.Display.fillScreen(COL_BG);
    drawHeader("WEB BROWSER", bridgeOnline ? nullptr : "OFFLINE");

    if (browserLoading) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
        M5Cardputer.Display.setCursor(8, CONTENT_Y + 28);
        M5Cardputer.Display.print("Fetching...");
        M5Cardputer.Display.setCursor(8, CONTENT_Y + 44);
        char trunc[36]; strlcpy(trunc, browserFetchedUrl, 36);
        M5Cardputer.Display.setTextColor(COL_AMBER, COL_BG);
        M5Cardputer.Display.print(trunc);
        return;
    }

    if (browserBookmarkMode) {
        M5Cardputer.Display.setTextSize(1);
        int y = CONTENT_Y + 4;
        for (int i = 0; i < BROWSER_BM_COUNT; i++) {
            bool sel = (i == cur());
            uint16_t bg = sel ? COL_SEL_BG : COL_BG;
            uint16_t fg = sel ? COL_SEL_FG : (i == BROWSER_BM_COUNT - 1 ? COL_DIM : COL_TEXT);
            M5Cardputer.Display.fillRect(0, y, SCREEN_W, LINE_H, bg);
            M5Cardputer.Display.setTextColor(fg, bg);
            M5Cardputer.Display.setCursor(5, y + 3);
            M5Cardputer.Display.print(sel ? "> " : "  ");
            M5Cardputer.Display.print(BROWSER_BM_LABELS[i]);
            y += LINE_H;
        }
        drawFooter("WS=nav Enter=open A=back", "");
        return;
    }

    if (browserUrlMode) {
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(COL_DIM, COL_BG);
        drawBrowserUrlBar();
        M5Cardputer.Display.setCursor(8, CONTENT_Y + 32);
        M5Cardputer.Display.print("Enter=go  b=bookmarks");
        M5Cardputer.Display.setCursor(8, CONTENT_Y + 48);
        M5Cardputer.Display.print("Words search DuckDuckGo.");
        if (browserError[0]) {
            M5Cardputer.Display.setTextColor(COL_ERR, COL_BG);
            M5Cardputer.Display.setCursor(4, CONTENT_Y + 66);
            char el[38]; strlcpy(el, browserError, 38);
            M5Cardputer.Display.print(el);
        }
        return;
    }

    if (browserLinkMode) {
        int end = min(browserLinkScroll + BR_VIS, browserLinkCount);
        int y = CONTENT_Y + 4;
        M5Cardputer.Display.setTextSize(1);
        for (int i = browserLinkScroll; i < end; i++) {
            bool sel = (i == browserLinkCursor);
            uint16_t bg = sel ? COL_SEL_BG : COL_BG;
            uint16_t fg = sel ? COL_SEL_FG : COL_TEXT;
            M5Cardputer.Display.fillRect(0, y, SCREEN_W, LINE_H, bg);
            M5Cardputer.Display.setTextColor(sel ? COL_AMBER : COL_DIM, bg);
            M5Cardputer.Display.setCursor(2, y + 3);
            M5Cardputer.Display.print(sel ? ">" : " ");
            M5Cardputer.Display.setTextColor(fg, bg);
            M5Cardputer.Display.setCursor(10, y + 3);
            char title[36];
            strlcpy(title, browserLinkTitles[i], sizeof(title));
            M5Cardputer.Display.print(title);
            y += LINE_H;
        }
        char pos[20];
        snprintf(pos, sizeof(pos), "%d/%d", min(browserLinkCursor + 1, browserLinkCount), browserLinkCount);
        drawFooter("WS=nav Enter=go Del=back o=text", pos);
        return;
    }

    drawBrowserUrlBar();
    int end = min(browserScroll + BR_VIS, browserLineCount);
    int y = CONTENT_Y + 26;
    M5Cardputer.Display.setTextSize(1);
    for (int i = browserScroll; i < end; i++) {
        M5Cardputer.Display.fillRect(0, y, SCREEN_W, LINE_H, COL_BG);
        M5Cardputer.Display.setTextColor(COL_TEXT, COL_BG);
        M5Cardputer.Display.setCursor(2, y + 3);
        M5Cardputer.Display.print(browserLines[i]);
        y += LINE_H;
    }
    if (y < SCREEN_H - 12) {
        M5Cardputer.Display.fillRect(0, y, SCREEN_W, SCREEN_H - 12 - y, COL_BG);
    }

    char pg[20];
    snprintf(pg, sizeof(pg), "%d/%d", min(browserScroll + BR_VIS, browserLineCount), browserLineCount);
    if (browserLineCount > BR_VIS) {
        int trackX = SCREEN_W - 4;
        int trackY = CONTENT_Y + 26;
        int trackH = SCREEN_H - FOOTER_H - trackY - 1;
        int maxScroll = max(1, browserLineCount - BR_VIS);
        int thumbH = max(8, (trackH * BR_VIS) / max(1, browserLineCount));
        int thumbY = trackY + ((trackH - thumbH) * browserScroll) / maxScroll;
        M5Cardputer.Display.fillRect(trackX, trackY, 2, trackH, COL_DIM);
        M5Cardputer.Display.fillRect(trackX, thumbY, 2, thumbH, COL_AMBER);
    }
    drawFooter(browserLinkCount > 0 ? "WS=scroll Enter=links /=url" : "WS=scroll b=bkmks /=url", pg);
}
void fetchBrowserPage() {
    if (!wifiOk()) {
        strlcpy(browserError, "WiFi offline", sizeof(browserError));
        drawBrowser();
        return;
    }
    // Push current page URL to history before navigating (skip when going back)
    if (!browserNavBack && browserLineCount > 0 && browserFetchedUrl[0] &&
        browserHistoryDepth < BROWSER_HIST_MAX) {
        strlcpy(browserUrlHistory[browserHistoryDepth++], browserFetchedUrl, sizeof(browserUrlHistory[0]));
    }
    browserNavBack = false;
    char input[80];
    strlcpy(input, browserUrlBuf, sizeof(input));
    bool isSearch = strchr(input, ' ') != nullptr;
    if (!isSearch && strncmp(input, "http://", 7) != 0 && strncmp(input, "https://", 8) != 0 && strchr(input, '.') == nullptr) {
        isSearch = true;
    }
    char url[80];
    if (isSearch) {
        strlcpy(url, input, sizeof(url));
    } else {
        strlcpy(url, input, sizeof(url));
        if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
            char tmp[80]; snprintf(tmp, sizeof(tmp), "https://%s", url);
            strlcpy(url, tmp, sizeof(url));
        }
    }
    strlcpy(browserFetchedUrl, isSearch ? input : url, sizeof(browserFetchedUrl));
    browserLoading   = true;
    browserLineCount = 0;
    browserLinkCount = 0;
    browserLinkCursor = 0;
    browserLinkScroll = 0;
    browserLinkMode = false;
    browserScroll    = 0;
    browserError[0]  = '\0';
    drawBrowser();

    String body = isSearch ? "{\"query\":\"" : "{\"url\":\"";
    for (const char* p = url; *p; ++p) {
        if (*p == '"' || *p == '\\') body += '\\';
        body += *p;
    }
    body += "\"}";

    JsonDocument doc;
    int code = bridgePostBody("/api/v1/web/fetch", body, doc);
    browserLoading = false;

    if (code != 200) {
        if (code == 503)
            strlcpy(browserError, "w3m not installed on Pi", sizeof(browserError));
        else if (code < 0)
            strlcpy(browserError, "No control response", sizeof(browserError));
        else {
            snprintf(browserError, sizeof(browserError), "HTTP %d", code);
        }
        browserUrlMode = true;
        drawBrowser();
        return;
    }

    bool ok = doc["ok"] | false;
    if (!ok) {
        const char* err = doc["error"] | "fetch failed";
        strlcpy(browserError, err, sizeof(browserError));
        browserUrlMode = true;
        drawBrowser();
        return;
    }

    JsonArray arr = doc["lines"].as<JsonArray>();
    browserLineCount = 0;
    for (JsonVariant v : arr) {
        if (browserLineCount >= BR_MAX_LINES) break;
        strlcpy(browserLines[browserLineCount++], v.as<const char*>(), BR_LINE_LEN + 1);
    }

    JsonArray links = doc["links"].as<JsonArray>();
    browserLinkCount = 0;
    for (JsonVariant item : links) {
        if (browserLinkCount >= BR_MAX_LINKS) break;
        const char* title = item["title"] | "";
        const char* linkUrl = item["url"] | "";
        if (!linkUrl[0]) continue;
        strlcpy(browserLinkTitles[browserLinkCount], title[0] ? title : linkUrl, BR_LINK_TITLE_LEN + 1);
        strlcpy(browserLinkUrls[browserLinkCount], linkUrl, BR_LINK_URL_LEN + 1);
        browserLinkCount++;
    }
    browserLinkCursor = 0;
    browserLinkScroll = 0;

    if (browserLineCount == 0) {
        strlcpy(browserError, "Empty page", sizeof(browserError));
        browserUrlMode = true;
    } else {
        browserUrlMode = false;
        browserLinkMode = browserLinkCount > 0;
    }
    bridgeOnline = true;
    offlineMode = false;
    Audio::ok();
    drawBrowser();
}

// ─────────────────────────────────────────────────────────────────────────────
// Device info
// ─────────────────────────────────────────────────────────────────────────────
void drawInfo() {
    M5Cardputer.Display.fillScreen(COL_BG);
    drawHeader("DEVICE", wifiOk()?WiFi.SSID().c_str():"offline");

    int y = CONTENT_Y;
    char val[52];

    strlcpy(val, wifiOk()?WiFi.localIP().toString().c_str():"not connected", sizeof(val));
    drawKV(y, "IP  ", val, wifiOk()?COL_OK:COL_ERR); y+=LINE_H;

    if (wifiOk()) {
        int rssi = WiFi.RSSI();
        snprintf(val, sizeof(val), "%ddBm  %s", rssi, rssi>-65?"good":rssi>-80?"fair":"weak");
        drawKV(y, "RSSI", val, rssi>-65?COL_OK:rssi>-80?COL_WARN:COL_ERR); y+=LINE_H;
    }

    {
        int bat=batteryLevelPercent();
        formatBatteryStatus(val, sizeof(val));
        drawKV(y, "BAT ", val, bat>50?COL_OK:bat>20?COL_WARN:bat>=0?COL_ERR:COL_DIM);
        y+=LINE_H;
    }

    {
        unsigned long s=millis()/1000;
        snprintf(val, sizeof(val), "%luh %02lum %02lus", s/3600, (s%3600)/60, s%60);
        drawKV(y, "UP  ", val, COL_DIM); y+=LINE_H;
    }

    {   // Firmware version + OTA hint
        snprintf(val, sizeof(val), "%s%s", FW_VERSION, otaInfo.available?" [UPDATE!]":"");
        drawKV(y, "FW  ", val, otaInfo.available?COL_WARN:COL_DIM); y+=LINE_H;
    }

    drawRule(y+1); y+=6;

    // Control Center URL
    {
        char ccUrl[90];
        const char* p=strstr(g_bridge_host,":8001");
        if (p) { int bl=(int)(p-g_bridge_host); snprintf(ccUrl,sizeof(ccUrl),"%.*s:8000",bl,g_bridge_host); }
        else    strlcpy(ccUrl, g_bridge_host, sizeof(ccUrl));
        drawKV(y, "CC  ", ccUrl, COL_AMBER); y+=LINE_H;
    }
    drawKV(y, "BRG ", g_bridge_host, COL_DIM);
    y += LINE_H;

    {
        uint16_t stCol = offlineMode ? COL_ERR : bridgeOnline ? COL_OK : COL_WARN;
        const char* st  = offlineMode ? "OFFLINE" : bridgeOnline ? "ONLINE" : "UNKNOWN";
        drawKV(y, "STS ", st, stCol); y += LINE_H;
    }

    {
        char webUrl[32];
        if (wifiOk()) {
            snprintf(webUrl, sizeof(webUrl), "http://%s/", WiFi.localIP().toString().c_str());
        } else {
            strlcpy(webUrl, "n/a", sizeof(webUrl));
        }
        drawKV(y, "WEB ", webUrl, wifiOk() ? COL_OK : COL_DIM); y += LINE_H;
    }

    const char* otaHint = otaInfo.available ? "u=OTA update" : "u=check OTA";
    drawFooter("A=back  h=retry  web=update", otaHint);
}

// ─────────────────────────────────────────────────────────────────────────────
// USB Macro screen — schedule a HID keyboard string to fire at a set time
// ─────────────────────────────────────────────────────────────────────────────
void drawUSBMacro() {
    M5Cardputer.Display.fillScreen(thBG());
    drawHeader("USB MACRO", ntpSynced ? "NTP OK" : "NO TIME");

    int y = CONTENT_Y + 2;
    // Current local time
    if (ntpSynced) {
        struct tm ti; getLocalTime(&ti);
        char now[20]; snprintf(now, sizeof(now), "%02d:%02d:%02d UTC+%d",
                               ti.tm_hour, ti.tm_min, ti.tm_sec, rconfig.tz_offset);
        drawKV(y, "NOW ", now, COL_DIM); y += LINE_H;
    } else {
        drawKV(y, "NOW ", "no NTP sync", COL_WARN); y += LINE_H;
    }
    drawRule(y+1); y += 6;

    // Fields
    struct { const char* label; char val[48]; uint16_t col; } rows[5];
    char hhmm[8]; snprintf(hhmm, sizeof(hhmm), "%02d:%02d",
                            rconfig.macro_hour, rconfig.macro_min);
    strlcpy(rows[0].val, hhmm, sizeof(rows[0].val));            rows[0].label="TIME"; rows[0].col=thFG();
    strlcpy(rows[1].val, rconfig.macro_text, 32);               rows[1].label="TEXT"; rows[1].col=COL_TEXT;
    snprintf(rows[2].val, sizeof(rows[2].val), "UTC+%d", rconfig.tz_offset);
                                                                rows[2].label="TZ  "; rows[2].col=COL_DIM;
    strlcpy(rows[3].val, rconfig.macro_enabled?"ARMED":"DISARMED", sizeof(rows[3].val));
                                                                rows[3].label="STAT"; rows[3].col=rconfig.macro_enabled?COL_OK:COL_DIM;
    strlcpy(rows[4].val, macroFired?"FIRED":"READY",            sizeof(rows[4].val)); rows[4].label="LAST"; rows[4].col=macroFired?COL_WARN:COL_DIM;

    for (int i = 0; i < 5; i++) {
        bool sel = (!macroEditMode && i == cur());
        uint16_t bg = sel ? thFG() : (i%2==0 ? 0x0821 : thBG());
        uint16_t fg = sel ? thBG() : rows[i].col;
        M5Cardputer.Display.fillRect(0, y, SCREEN_W, LINE_H, bg);
        if (sel) {
            M5Cardputer.Display.setTextColor(thBG(), bg);
            M5Cardputer.Display.setTextSize(1);
            M5Cardputer.Display.setCursor(5, y+3);
            M5Cardputer.Display.print(">");
        }
        M5Cardputer.Display.setTextColor(sel?thBG():COL_DIM, bg);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(14, y+3);
        M5Cardputer.Display.print(rows[i].label);
        M5Cardputer.Display.setTextColor(fg, bg);

        char disp[34] = "";
        strlcpy(disp, (macroEditMode && i == macroEditField) ? macroEditBuf : rows[i].val, 33);
        if (macroEditMode && i == macroEditField) {
            int bl = strlen(disp); if(bl<33){disp[bl]='_';disp[bl+1]='\0';}
        }
        M5Cardputer.Display.setCursor(SCREEN_W-(int)strlen(disp)*6-4, y+3);
        M5Cardputer.Display.print(disp);
        y += LINE_H;
    }

    if (macroEditMode)
        drawFooter(macroEditField == 0 ? "type HH:MM  Enter=save  Esc=exit" : "type  Enter=save  Esc=exit", "");
    else
        drawFooter("WS=nav  Enter=edit  h=retry host  A=back", macroFired?"FIRED":"");
}

// ─────────────────────────────────────────────────────────────────────────────
// USB Macro onEnter handling — called from main onEnter() switch
// ─────────────────────────────────────────────────────────────────────────────
// (handled inline in onEnter Screen::USBMacro case)

// ─────────────────────────────────────────────────────────────────────────────
// USB Macro scheduler — called every loop iteration
// ─────────────────────────────────────────────────────────────────────────────
void checkUsbMacro() {
    if (!rconfig.macro_enabled || !ntpSynced || macroFired) return;
    if (!rconfig.macro_text[0]) return;
    struct tm ti;
    if (!getLocalTime(&ti, 0)) return;
    if (ti.tm_hour == (int)rconfig.macro_hour &&
        ti.tm_min  == (int)rconfig.macro_min  &&
        ti.tm_sec  <  5) {
        // Fire!
        macroFired = true;
        delay(200);
        KbdHID.print(rconfig.macro_text);
        KbdHID.write(KEY_RETURN);
        Audio::ok();
        if (current == Screen::USBMacro) drawUSBMacro();
    }
}
