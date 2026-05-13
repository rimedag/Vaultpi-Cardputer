<img width="1080" height="1440" alt="cardputer-zero-diy-v0-kisizx9bldzg1" src="https://github.com/user-attachments/assets/8460d64b-fbb0-4963-af8d-94075090d0ac" />
<img width="1080" height="1440" alt="cardputer-zero-diy-v0-pr6mod1aldzg1" src="https://github.com/user-attachments/assets/da692928-080c-4bfc-a740-37767c055e3e" />
# VaultPi Cardputer

A portable control panel for a Raspberry Pi home server, running on the **M5Stack Cardputer** (ESP32-S3). Monitor system health, manage services and actions, browse the web, fire IR remotes, and more — from a pocketable device with a full QWERTY keyboard.

---

## Hardware

- [M5Stack Cardputer](https://docs.m5stack.com/en/core/Cardputer) (ESP32-S3, 240×135 display, QWERTY keyboard)
- Raspberry Pi running [VaultPi Control Center](https://github.com/rimedag/Vaultpi-Control-Center)
- USB-C cable or LiPo battery

---

## Architecture

```
Cardputer firmware
  │  Wi-Fi / HTTP  X-Cardputer-Password
  ▼
VaultPi Control Center  :8001
```

The Cardputer connects directly to the VaultPi Control Center over Wi-Fi. All you need is the control center IP address and the Cardputer password configured in its settings.

---

## Features
![Uploading cardputer-zero-diy-v0-kisizx9bldzg1.webp…]()

### Screens

| Screen | What you can do |
|---|---|
| **Dashboard** | CPU, RAM, disk, temp, load — sparklines over the last 40 polls |
| **Services** | List all services; start / stop / restart any of them |
| **Actions** | Run one-click actions (backup, sync, shutdown, custom scripts) |
| **Favorites** | Pin frequently used services or actions for quick access |
| **Alerts** | View and clear Pi-side notifications |
| **Activity** | Recent activity log from the control center |
| **Gitea** | Profile info, repo count, stars, followers, 12-week commit heatmap |
| **IR Remote** | D-pad + dedicated buttons for Samsung, LG, Sony, Philips, AC profiles |
| **Network** | Scan nearby Wi-Fi, connect to a new network on the fly |
| **Web Browser** | Text browser; DuckDuckGo search; back button; bookmarks; link navigation |
| **Terminal** | Run commands on the Pi directly from the Cardputer |
| **Notes** | Four persistent notes stored in NVS |
| **USB Macro** | Schedule a typed HID macro at a set time each day |
| **Device** | Firmware version, battery, uptime, IP, OTA update check |
| **Settings** | All runtime config — see [Settings reference](#settings-reference) |

### Other highlights

- **Six colour themes**: P1NK · CYB3R · AMB3R · GH0ST · BL00D · L1ME
- **Battery icon** in the top bar with charge indicator
- **Wi-Fi signal** icon with RSSI strength
- **Power-aware display timeout**: separate dim/off timers for plugged-in vs. battery
- **GO button** (back of device): configurable as a lock button
- **OTA updates** over Wi-Fi
- **PIN lock** with 4-digit code, shown at boot and optionally on GO button wake
- **Idle animation** (rain scene) on the main menu when enabled

---
<img width="1080" height="1440" alt="cardputer-zero-diy-v0-ox3gt61bldzg1" src="https://github.com/user-attachments/assets/f8c7ce91-32b5-4986-bed5-c03e45751187" />

## Quick Start

### 1 — Configure the firmware

Edit `firmware/src/config.h`:

```cpp
// Wi-Fi — fill in up to three networks; first to connect wins
static const char* WIFI_SSIDS[]  = { "YourSSID1", "YourSSID2", "" };
static const char* WIFI_PASSES[] = { "YourPass1", "YourPass2", "" };

// Point this at your VaultPi Control Center
#define BRIDGE_HOST "http://192.168.1.x:8001"

// Cardputer password — must match the value set in the control center settings
#define BRIDGE_PSK  "your-cardputer-password"
```

### 2 — Flash the firmware

```bash
cd firmware
pio run --target upload
```

Or build only:

```bash
pio run
```

PlatformIO handles library dependencies automatically on first build.

---

## Keyboard Shortcuts

### Global (any screen, outside edit mode)

| Key | Action |
|---|---|
| `w` / `;` | Up |
| `s` / `.` | Down |
| `a` / `,` | Back |
| `d` / `/` | Select |
| `Enter` | Select |
| `Del` / `Backspace` | Back |
| `ESC` | Back to main menu |
| `r` | Refresh current screen |
| `b` | Run backup action |
| `g` | Run sync-android action |
| `x` | Safe shutdown |
| `l` | Open Activity Log |
| `t` | Open Notes |
| `n` | Open Network scan |
| `f` | Open Favorites / pin current service or action |
| `u` | OTA check (Device screen) |

### Main Menu

| Key | Action |
|---|---|
| `1` – `9` | Jump to menu item 1–9 |
| `0` | Jump to menu item 10 |
| Arrow keys | Navigate |

---

## Settings Reference

Open **Settings** from the main menu. Navigate with `w`/`s`, press `Enter` to edit a field or cycle an option. Select **[Save & Apply]** to persist.

| Field | Type | Default | Description |
|---|---|---|---|
| Nickname | text | `CARDPUTER` | Device name shown in the control center |
| WiFi1–3 SSID / Pass | text | — | Up to three Wi-Fi networks tried in order |
| Control Host | text | `http://192.168.1.x:8001` | VaultPi Control Center URL |
| Password | text | `your-cardputer-password` | Cardputer password (set in control center) |
| Poll (sec) | number | `15` | Background data refresh interval |
| Dim (plugged) | number | `0` (Never) | Seconds idle before dimming on USB power; `0` = never |
| Off (plugged) | number | `0` (Never) | Seconds idle before screen-off on USB power; `0` = never |
| Dim (battery) | number | `15` | Seconds idle before dimming on battery |
| Off (battery) | number | `30` | Seconds idle before screen-off on battery |
| GO Button | cycle | `Default` | `Default` = nothing; `Lock` = press to sleep/wake screen |
| PIN Lock | toggle | `OFF` | Enable 4-digit PIN on boot |
| PIN Code | text | `0000` | 4-digit PIN |
| Speaker Vol | number | `80` | Buzzer volume (0–255) |
| IR Device | cycle | `Samsung` | Active IR profile: Samsung · LG · Sony · Philips · AC |
| Theme | cycle | `P1NK` | Colour theme: P1NK · CYB3R · AMB3R · GH0ST · BL00D · L1ME |
| Idle Anim | toggle | `ON` | Rain idle scene animation on the main menu |

---

## Project Layout

```
firmware/
├── platformio.ini
└── src/
    ├── main.cpp          — screens, navigation, input, main loop
    ├── config.h          — compile-time defaults (edit before flashing)
    ├── settings_store.h  — RuntimeConfig struct, NVS load/save
    ├── display.h         — drawing helpers, colour palette, themes
    ├── network.h         — authenticated HTTP helpers
    ├── audio.h           — buzzer tones
    ├── ir_codes.h        — IR protocol tables
    ├── ota.h             — OTA update logic
    └── pig.h             — idle scene animation
```

---

## Dependencies

Managed by PlatformIO:

- [M5Cardputer](https://github.com/m5stack/M5Cardputer) — board HAL, display, keyboard, power
- [ArduinoJson](https://arduinojson.org/) ^7.1
- [IRremote](https://github.com/Arduino-IRremote/Arduino-IRremote) ^4.4

---

## License

MIT — see [LICENSE](LICENSE).
