#pragma once
#include <Arduino.h>

#include "_debug.h"

// Protocol modes for Serial (USB CDC).
enum SerialProtocolMode {
    PROTOCOL_BLAST,
    PROTOCOL_SLIP
};

// LED operation modes (used by both BLAST protocol and firmware LED system).
enum LedMode {
    LED_OFF = 0,        // LED is off.
    LED_ON = 1,         // LED is constantly on at set brightness.
    LED_BREATHING = 2,  // LED fades in/out smoothly.
    LED_BLINKING = 3    // LED blinks on/off.
};

// Current protocol mode (defined in firmware.ino).
extern SerialProtocolMode serialProtocolMode;

// Display protocol mode indicator ("B"/"S") in top-right corner of the OLED bar.
// Comment out to disable.
#define SHOW_PROTOCOL_MODE_INDICATOR

// BLAST line buffer size.
#define BLAST_LINE_BUFFER_SIZE 64

// BLAST LED value constants.
#define BLAST_LED_OFF 0
#define BLAST_LED_ON 1
#define BLAST_LED_BLINK 2
#define BLAST_LED_FLASH 3
#define BLAST_LED_BREATH 4

// Process incoming BLAST protocol data (non-blocking, call from main loop).
void processBlastProtocol();

// Update flash LED states (call from main loop).
void updateBlastFlash();
