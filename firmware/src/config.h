#pragma once
// ---------------------------------------------------------------------------
// VaultPi Cardputer — compile-time defaults
// All values can be overridden at runtime via the Settings screen (NVS).
// ---------------------------------------------------------------------------

// Wi-Fi networks — tried in order, first to connect wins
// Fill in your own SSIDs/passwords here before flashing.
#define WIFI_NET_COUNT 3
static const char* WIFI_SSIDS[]  = { "YourSSID1", "YourSSID2", "" };
static const char* WIFI_PASSES[] = { "YourPass1", "YourPass2", "" };

// Control Center connection
// Set to the IP/hostname of your Raspberry Pi control center API (port 8001).
#define BRIDGE_HOST "http://192.168.1.x:8001"
#define BRIDGE_PSK  "password"

// Device identity
#define DEFAULT_DEVICE_NICKNAME "CARDPUTER"

// Timing defaults (override in Settings screen)
#define DEFAULT_POLL_SEC    15   // background refresh interval (seconds)
#define DEFAULT_DIM_SEC_POWER   0    // never dim when plugged in (0=never)
#define DEFAULT_OFF_SEC_POWER   0    // never turn off when plugged in (0=never)
#define DEFAULT_DIM_SEC_BAT    15    // dim after N idle seconds on battery
#define DEFAULT_OFF_SEC_BAT    30    // screen off after N idle seconds on battery (0=never)
#define DEFAULT_GO_BTN_MODE     0    // 0=Default (nothing), 1=Lock
#define WIFI_RETRY_MS     8000   // per-network WiFi attempt timeout
#define HTTP_TIMEOUT_MS   6000   // HTTP request timeout for data fetches
#define FAST_HTTP_TIMEOUT_MS  300 // quick failure for control-center state mirroring
#define WIFI_RECONNECT_MS 60000  // attempt WiFi reconnect every N ms when offline

// Display brightness
#define DIM_BRIGHTNESS     10    // barely visible (0-255)
#define FULL_BRIGHTNESS   200

// Security defaults
#define DEFAULT_PIN_ENABLED  false
#define DEFAULT_PIN_CODE     "0000"

// Audio defaults
#define DEFAULT_SPK_VOLUME   80   // 0-255

// IR remote default profile (0=Samsung, 1=LG, 2=Sony, 3=Philips, 4=AC)
#define DEFAULT_IR_DEVICE    0

// Sparkline history depth (one point per poll cycle)
#define SPARK_MAX  40

// Notepad
#define MAX_NOTES   12
#define NOTE_LEN   80

// Theme (0=P1NK 1=CYB3R 2=AMB3R 3=GH0ST 4=BL00D 5=L1ME)
#define DEFAULT_THEME_IDX    0

// Idle scene animation (rain + mountains + birds on main screen)
#define DEFAULT_IDLE_ANIM    true

// USB Macro scheduler defaults
#define DEFAULT_MACRO_HOUR   3
#define DEFAULT_MACRO_MIN    5
#define DEFAULT_MACRO_ENABLED false
#define DEFAULT_TZ_OFFSET    1    // UTC+1 (CET)
