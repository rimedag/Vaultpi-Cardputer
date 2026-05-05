#pragma once
#include <M5Cardputer.h>

// ---------------------------------------------------------------------------
// Audio helpers — speaker beep patterns for UI feedback
// All functions are blocking for the tone duration only.
// ---------------------------------------------------------------------------
namespace Audio {

    inline void init(uint8_t vol = 80) {
        M5Cardputer.Speaker.setVolume(vol);
        M5Cardputer.Speaker.setAllChannelVolume(vol);
    }

    inline void _tone(uint16_t freq, uint32_t ms) {
        M5Cardputer.Speaker.tone(freq, ms);
        delay(ms + 15);
    }

    // Single soft click — navigation feedback
    inline void click() {
        M5Cardputer.Speaker.tone(1800, 18);
        delay(30);
    }

    // Two-tone ascending — action succeeded / PIN correct
    inline void ok() {
        _tone(1100, 70);
        _tone(1700, 100);
    }

    // Three short low beeps — action failed / PIN wrong
    inline void err() {
        for (int i = 0; i < 3; i++) { _tone(380, 70); delay(35); }
    }

    // Double mid beep — warning / new alert received
    inline void warn() {
        _tone(750, 130);
        delay(70);
        _tone(750, 130);
    }

    // Rising three-tone — boot complete
    inline void boot() {
        _tone(600, 55);
        _tone(900, 55);
        _tone(1400, 90);
    }

    // Single short high — IR code sent
    inline void irSent() {
        M5Cardputer.Speaker.tone(2000, 25);
        delay(40);
    }

    // Critical alert — service down etc.
    inline void critical() {
        for (int i = 0; i < 2; i++) {
            _tone(500, 200);
            delay(80);
        }
    }

} // namespace Audio
