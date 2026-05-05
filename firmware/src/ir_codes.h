#pragma once
// ---------------------------------------------------------------------------
// IR Remote — code database and sender
// Requires IRremote library (z3t0/IRremote ^4.4)
// M5Cardputer Adv IR TX pin: GPIO 44
// ---------------------------------------------------------------------------
#include <IRremote.hpp>

#define IR_TX_PIN 44

// Protocol IDs (matches IRremote send method)
#define IR_NEC     0
#define IR_SAMSUNG 1
#define IR_SONY    2
#define IR_RC5     3
#define IR_LG      4   // LG uses NEC variant with full 32-bit command

struct IRKey {
    const char label[5];   // up to 4 chars + null
    uint8_t  proto;
    uint16_t address;
    uint16_t command;
    uint8_t  bits;         // Sony: 12/15/20; others: 0 (ignored)
};

struct IRProfile {
    const char* name;
    const IRKey* keys;
    int count;
};

// ─── Samsung TV ──────────────────────────────────────────────────────────────
static const IRKey IR_SAMSUNG_TV[] = {
    {" PWR", IR_SAMSUNG, 0x0707, 0x02, 0},
    {"VOL+", IR_SAMSUNG, 0x0707, 0x07, 0},
    {"VOL-", IR_SAMSUNG, 0x0707, 0x0B, 0},
    {"MUTE", IR_SAMSUNG, 0x0707, 0x0F, 0},
    {" CH+", IR_SAMSUNG, 0x0707, 0x12, 0},
    {" CH-", IR_SAMSUNG, 0x0707, 0x10, 0},
    {" SRC", IR_SAMSUNG, 0x0707, 0x01, 0},
    {"MENU", IR_SAMSUNG, 0x0707, 0x1A, 0},
    {"  OK", IR_SAMSUNG, 0x0707, 0x68, 0},
    {"BACK", IR_SAMSUNG, 0x0707, 0x58, 0},
    {"HOME", IR_SAMSUNG, 0x0707, 0x79, 0},
    {"INFO", IR_SAMSUNG, 0x0707, 0x1F, 0},
};

// ─── LG TV ───────────────────────────────────────────────────────────────────
static const IRKey IR_LG_TV[] = {
    {" PWR", IR_NEC, 0x20DF, 0x10EF, 0},
    {"VOL+", IR_NEC, 0x20DF, 0x40BF, 0},
    {"VOL-", IR_NEC, 0x20DF, 0xC03F, 0},
    {"MUTE", IR_NEC, 0x20DF, 0x906F, 0},
    {" CH+", IR_NEC, 0x20DF, 0x00FF, 0},
    {" CH-", IR_NEC, 0x20DF, 0x807F, 0},
    {" INP", IR_NEC, 0x20DF, 0xD02F, 0},
    {"MENU", IR_NEC, 0x20DF, 0xC23D, 0},
    {"  OK", IR_NEC, 0x20DF, 0x5EA1, 0},
    {"BACK", IR_NEC, 0x20DF, 0x28D7, 0},
    {"HOME", IR_NEC, 0x20DF, 0xAE51, 0},
    {"INFO", IR_NEC, 0x20DF, 0x55AA, 0},
};

// ─── Sony TV ─────────────────────────────────────────────────────────────────
static const IRKey IR_SONY_TV[] = {
    {" PWR", IR_SONY, 0x01, 0x15, 12},
    {"VOL+", IR_SONY, 0x01, 0x12, 12},
    {"VOL-", IR_SONY, 0x01, 0x13, 12},
    {"MUTE", IR_SONY, 0x01, 0x14, 12},
    {" CH+", IR_SONY, 0x01, 0x10, 12},
    {" CH-", IR_SONY, 0x01, 0x11, 12},
    {" INP", IR_SONY, 0x01, 0x2D, 12},
    {"MENU", IR_SONY, 0x01, 0x35, 12},
    {"  OK", IR_SONY, 0x01, 0x65, 12},
    {"BACK", IR_SONY, 0x01, 0x6B, 12},
    {"HOME", IR_SONY, 0x01, 0x60, 12},
    {"INFO", IR_SONY, 0x01, 0x5D, 12},
};

// ─── Generic AC (NEC — Midea/Comfee/many white-label units) ──────────────────
static const IRKey IR_AC_GENERIC[] = {
    {" PWR", IR_NEC, 0xB26D, 0x02FD, 0},
    {"COOL", IR_NEC, 0xB26D, 0x0CF3, 0},
    {"HEAT", IR_NEC, 0xB26D, 0x0EF1, 0},
    {" FAN", IR_NEC, 0xB26D, 0x0AF5, 0},
    {" T+ ", IR_NEC, 0xB26D, 0x22DD, 0},
    {" T- ", IR_NEC, 0xB26D, 0x24DB, 0},
    {" AUT", IR_NEC, 0xB26D, 0x30CF, 0},
    {" SLP", IR_NEC, 0xB26D, 0x34CB, 0},
    {" F+ ", IR_NEC, 0xB26D, 0x18E7, 0},
    {" F- ", IR_NEC, 0xB26D, 0x1AE5, 0},
    {"TIMR", IR_NEC, 0xB26D, 0x2CD3, 0},
    {"SWNG", IR_NEC, 0xB26D, 0x40BF, 0},
};

// ─── Philips TV ──────────────────────────────────────────────────────────────
static const IRKey IR_PHILIPS_TV[] = {
    {" PWR", IR_RC5,  0x00,  0x0C, 0},
    {"VOL+", IR_RC5,  0x00,  0x10, 0},
    {"VOL-", IR_RC5,  0x00,  0x11, 0},
    {"MUTE", IR_RC5,  0x00,  0x0D, 0},
    {" CH+", IR_RC5,  0x00,  0x20, 0},
    {" CH-", IR_RC5,  0x00,  0x21, 0},
    {" SRC", IR_RC5,  0x00,  0x38, 0},
    {"MENU", IR_RC5,  0x00,  0x12, 0},
    {"  OK", IR_RC5,  0x00,  0x57, 0},
    {"BACK", IR_RC5,  0x00,  0x22, 0},
    {"HOME", IR_RC5,  0x00,  0x24, 0},
    {"INFO", IR_RC5,  0x00,  0x3F, 0},
};

// ─── Profile table ────────────────────────────────────────────────────────────
static const IRProfile IR_PROFILES[] = {
    {"Samsung TV",  IR_SAMSUNG_TV, 12},
    {"LG TV",       IR_LG_TV,      12},
    {"Sony TV",     IR_SONY_TV,    12},
    {"Philips TV",  IR_PHILIPS_TV, 12},
    {"AC Generic",  IR_AC_GENERIC, 12},
};
static const int IR_PROFILE_COUNT = 5;
static const int IR_KEYS_PER_PROFILE = 12;  // 3 rows × 4 cols

// Grid layout: 3 rows × 4 cols
static const int IR_GRID_ROWS = 3;
static const int IR_GRID_COLS = 4;

// ─── Init ─────────────────────────────────────────────────────────────────────
inline void irInit() {
    IrSender.begin(IR_TX_PIN);
}

// ─── Send ─────────────────────────────────────────────────────────────────────
inline bool irSend(const IRKey& k) {
    switch (k.proto) {
        case IR_NEC:
            IrSender.sendNEC((uint16_t)k.address, (uint16_t)k.command, 0);
            return true;
        case IR_SAMSUNG:
            IrSender.sendSamsung((uint16_t)k.address, (uint16_t)k.command, 0);
            return true;
        case IR_SONY:
            IrSender.sendSony((uint16_t)k.address, (uint16_t)k.command, 2,
                              k.bits ? k.bits : 12);
            return true;
        case IR_RC5:
            IrSender.sendRC5((uint8_t)k.address, (uint8_t)k.command, 0, false);
            return true;
        default:
            return false;
    }
}
