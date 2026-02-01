#pragma once
#include <stdint.h>
#include <string.h>

typedef struct {
        uint8_t modifiers : 5;  // Bits for ALT, CTRL, SHIFT, F keys and ESC.
        uint8_t key : 8;        // ASCII or keycode.
} KeyCombo;

#define MOD_NONE 0x0
#define MOD_CTRL 0x1
#define MOD_ALT 0x2
#define MOD_SHIFT 0x4
#define MOD_F 0x8     // Function key modifier (e.g. F1-F12).
#define MOD_ESC 0x10  // Escape key.

// This struct consumes 49 bytes in total (16 for name + 1 padding + 32 for key combos).
typedef struct {
        char name[17];  // Max 16 chars + null terminator.
        KeyCombo startP1;
        KeyCombo startP2;
        KeyCombo coinP1;
        KeyCombo coinP2;
        KeyCombo actionP1_1;
        KeyCombo actionP1_2;
        KeyCombo actionP2_1;
        KeyCombo actionP2_2;
        KeyCombo serviceP1;
        KeyCombo serviceP2;
        KeyCombo testP1;
        KeyCombo testP2;
        KeyCombo pause;
        KeyCombo save;
        KeyCombo load;
        KeyCombo exit;
} ButtonMapping;
