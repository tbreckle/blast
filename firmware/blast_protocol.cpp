#include "blast_protocol.h"

#include "menu.h"
#include "storage.h"

// LED control function (defined in firmware.ino).
extern void ledSetMode(uint8_t channel, LedMode mode, uint16_t brightness, uint16_t period,
                        uint32_t phaseOffset);

// BLAST protocol line buffer.
static char lineBuffer[BLAST_LINE_BUFFER_SIZE];
static uint8_t linePos = 0;

// Flash state tracking per TLC channel (0-indexed, 12 channels).
struct FlashState {
    bool active;
    uint8_t remaining;      // Remaining toggles (count * 2).
    uint32_t lastToggle;    // Timestamp of last toggle.
    uint16_t halfPeriod;    // Milliseconds between each toggle.
    bool on;                // Current on/off state.
};
static FlashState flashStates[12] = {};

// Default values when EXTRA parameter is omitted.
static const uint16_t DEFAULT_BLINK_DURATION_MS = 500;
static const uint8_t DEFAULT_FLASH_COUNT = 3;
static const uint16_t DEFAULT_BREATH_DURATION_MS = 1000;
static const uint16_t FLASH_HALF_PERIOD_MS = 150;

// Upper limits for EXTRA values.
// Blink/breath period is 2 * duration and must fit into uint16_t.
static const uint16_t MAX_DURATION_MS = 32767;
// Flash toggles (count * 2) must fit into FlashState::remaining (uint8_t).
static const uint8_t MAX_FLASH_COUNT = 127;

// TLC LED pin values (1-indexed, matching firmware.ino constants).
// Channel passed to ledSetMode is pin - 1.
static const uint8_t TLC_START_P1 = 1;
static const uint8_t TLC_START_P2 = 11;
static const uint8_t TLC_COIN_P1 = 2;
static const uint8_t TLC_COIN_P2 = 8;
static const uint8_t TLC_ACTION_P1_A = 12;
static const uint8_t TLC_ACTION_P1_B = 10;
static const uint8_t TLC_ACTION_P2_A = 9;
static const uint8_t TLC_ACTION_P2_B = 7;
static const uint8_t TLC_PAUSE = 4;
static const uint8_t TLC_SAVE = 5;
static const uint8_t TLC_LOAD = 6;

// Map BLAST LED command (3-6) + player (1-2) to TLC channel (0-indexed).
// Returns -1 on invalid input.
static int8_t getButtonChannel(uint8_t command, uint8_t player) {
    switch (command) {
        case 3:  // Start button
            return (player == 1) ? (TLC_START_P1 - 1) : (TLC_START_P2 - 1);
        case 4:  // Coin button
            return (player == 1) ? (TLC_COIN_P1 - 1) : (TLC_COIN_P2 - 1);
        case 5:  // Action A button
            return (player == 1) ? (TLC_ACTION_P1_A - 1) : (TLC_ACTION_P2_A - 1);
        case 6:  // Action B button
            return (player == 1) ? (TLC_ACTION_P1_B - 1) : (TLC_ACTION_P2_B - 1);
        default:
            return -1;
    }
}

// Return the blink/breath duration for EXTRA: default if omitted or 0, capped at MAX_DURATION_MS.
static uint16_t getDuration(uint16_t extra, bool hasExtra, uint16_t defaultDuration) {
    if (!hasExtra || extra == 0) {
        return defaultDuration;
    }
    return (extra > MAX_DURATION_MS) ? MAX_DURATION_MS : extra;
}

// Apply an LED command to a single TLC channel.
static void applyLedCommand(uint8_t channel, uint8_t value, uint16_t extra, bool hasExtra) {
    // Cancel any active flash on this channel.
    if (channel < 12) {
        flashStates[channel].active = false;
    }

    switch (value) {
        case BLAST_LED_OFF:
            ledSetMode(channel, LED_OFF, 0, 0, 0);
            break;

        case BLAST_LED_ON:
            ledSetMode(channel, LED_ON, 4095, 0, 0);
            break;

        case BLAST_LED_BLINK: {
            uint16_t duration = getDuration(extra, hasExtra, DEFAULT_BLINK_DURATION_MS);
            ledSetMode(channel, LED_BLINKING, 4095, duration * 2, 0);
            break;
        }

        case BLAST_LED_FLASH: {
            uint8_t count = DEFAULT_FLASH_COUNT;
            if (hasExtra) {
                count = (extra > MAX_FLASH_COUNT) ? MAX_FLASH_COUNT : (uint8_t)extra;
            }
            if (count == 0) {
                ledSetMode(channel, LED_OFF, 0, 0, 0);
                break;
            }
            // Start flash sequence: alternate on/off for count cycles then stop.
            flashStates[channel].active = true;
            flashStates[channel].remaining = count * 2;
            flashStates[channel].halfPeriod = FLASH_HALF_PERIOD_MS;
            flashStates[channel].lastToggle = millis();
            flashStates[channel].on = true;
            ledSetMode(channel, LED_ON, 4095, 0, 0);
            break;
        }

        case BLAST_LED_BREATH: {
            uint16_t duration = getDuration(extra, hasExtra, DEFAULT_BREATH_DURATION_MS);
            ledSetMode(channel, LED_BREATHING, 4095, duration * 2, 0);
            break;
        }

        default:
#ifdef DEBUG
            Serial2.print("[BLAST] Unknown LED value: ");
            Serial2.println(value);
#endif
            break;
    }
}

// Turn off all TLC channels and cancel any active flash sequences.
static void turnOffAllLeds() {
    for (uint8_t i = 0; i < 12; i++) {
        flashStates[i].active = false;
        ledSetMode(i, LED_OFF, 0, 0, 0);
    }
}

// Parse numeric fields separated by 'x' starting from startPos.
// Fills fields array (values saturate at 65535), returns number of fields parsed.
static uint8_t parseFields(const char* line, uint8_t startPos, uint16_t* fields, uint8_t maxFields) {
    uint8_t count = 0;
    uint8_t pos = startPos;

    while (line[pos] != '\0' && count < maxFields) {
        if (line[pos] == 'x' || line[pos] == 'X') {
            pos++;
            unsigned long number = strtoul(&line[pos], nullptr, 10);
            fields[count++] = (number > 0xFFFF) ? 0xFFFF : (uint16_t)number;
            // Skip past the number digits.
            while (line[pos] >= '0' && line[pos] <= '9') pos++;
        } else {
            pos++;
        }
    }
    return count;
}

// Handle a single per-player LED command (B3-B6).
static void handlePlayerLedCommand(uint8_t commandNum, const char* line) {
    // Format: B{cmd}x{PLAYER}x{VALUE}[x{EXTRA}]
    uint16_t fields[3] = {0};
    uint8_t fieldCount = parseFields(line, 2, fields, 3);

    if (fieldCount < 2) {
#ifdef DEBUG
        Serial2.println("[BLAST] Invalid LED command: missing fields.");
#endif
        return;
    }

    if (fields[0] > 2 || fields[1] > BLAST_LED_BREATH) {
#ifdef DEBUG
        Serial2.println("[BLAST] Invalid player or LED value.");
#endif
        return;
    }

    uint8_t player = static_cast<uint8_t>(fields[0]);
    uint8_t value = static_cast<uint8_t>(fields[1]);
    uint16_t extra = (fieldCount >= 3) ? fields[2] : 0;
    bool hasExtra = (fieldCount >= 3);

    if (player == 0) {
        // Player 0: apply to both players.
        for (uint8_t p = 1; p <= 2; p++) {
            int8_t channel = getButtonChannel(commandNum, p);
            if (channel >= 0) {
                applyLedCommand((uint8_t)channel, value, extra, hasExtra);
            }
        }
#ifdef DEBUG
        Serial2.print("[BLAST] B");
        Serial2.print(commandNum);
        Serial2.print(" P0 (both) val=");
        Serial2.println(value);
#endif
    } else {
        int8_t channel = getButtonChannel(commandNum, player);
        if (channel >= 0) {
            applyLedCommand((uint8_t)channel, value, extra, hasExtra);
#ifdef DEBUG
            Serial2.print("[BLAST] B");
            Serial2.print(commandNum);
            Serial2.print(" P");
            Serial2.print(player);
            Serial2.print(" → ch");
            Serial2.print(channel);
            Serial2.print(" val=");
            Serial2.println(value);
#endif
        }
    }
}

// Handle B7: load, pause, save button LEDs (1 command for 3 buttons).
static void handleGroupLedCommand(const char* line) {
    // Format: B7x{VALUE}[x{EXTRA}]
    uint16_t fields[2] = {0};
    uint8_t fieldCount = parseFields(line, 2, fields, 2);

    if (fieldCount < 1) {
#ifdef DEBUG
        Serial2.println("[BLAST] Invalid B7 command: missing value.");
#endif
        return;
    }

    uint8_t value = (uint8_t)fields[0];
    uint16_t extra = (fieldCount >= 2) ? fields[1] : 0;
    bool hasExtra = (fieldCount >= 2);

    // Apply to all three buttons: pause, save, load.
    applyLedCommand(TLC_PAUSE - 1, value, extra, hasExtra);
    applyLedCommand(TLC_SAVE - 1, value, extra, hasExtra);
    applyLedCommand(TLC_LOAD - 1, value, extra, hasExtra);

#ifdef DEBUG
    Serial2.print("[BLAST] B7 val=");
    Serial2.println(value);
#endif
}

// Handle BL: switch to the profile whose gameName matches, or leave the game if empty.
static void handleGameNameCommand(const char* line) {
    // Format: BLx{GAMENAME}, BLx = leave game
    if (line[2] != 'x' && line[2] != 'X') {
#ifdef DEBUG
        Serial2.println("[BLAST] Invalid BL command: missing game name.");
#endif
        return;
    }

    const char* gameName = &line[3];

    // Empty game name: leave the game and return to the main menu.
    if (gameName[0] == '\0') {
        if (currentMenuState == STATE_PROFILE || currentMenuState == STATE_SERVICEMENU) {
#ifdef DEBUG
            Serial2.println("[BLAST] Game ended, returning to main menu.");
#endif
            returnToMainMenu();
        }
        return;
    }

    int16_t profileIndex = findProfileByGameName(gameName);
    if (profileIndex < 0) {
#ifdef DEBUG
        Serial2.print("[BLAST] No profile for game: ");
        Serial2.println(gameName);
#endif
        return;
    }

    // Keep the current state (and LEDs) if the profile is already active.
    if (currentMenuState == STATE_PROFILE && currentProfileIndex == profileIndex) {
        return;
    }

#ifdef DEBUG
    Serial2.print("[BLAST] Game ");
    Serial2.print(gameName);
    Serial2.print(" → profile ");
    Serial2.println(profileIndex);
#endif
    activateProfile((uint8_t)profileIndex);
}

// Process a complete BLAST protocol line.
static void processBlastLine(const char* line) {
    if (line[0] != 'B') return;

    switch (line[1]) {
        case '1':  // B1 - Startup
#ifdef DEBUG
            Serial2.println("[BLAST] Host connected.");
#endif
            turnOffAllLeds();
            break;

        case '2':  // B2 - Shutdown
#ifdef DEBUG
            Serial2.println("[BLAST] Host disconnected.");
#endif
            turnOffAllLeds();
            break;

        case '3':  // B3 - Start button LED
        case '4':  // B4 - Coin button LED
        case '5':  // B5 - Action A button LED
        case '6':  // B6 - Action B button LED
            handlePlayerLedCommand(line[1] - '0', line);
            break;

        case '7':  // B7 - Load, Pause, Save button LEDs
            handleGroupLedCommand(line);
            break;

        case 'L':  // BL - Switch profile by game name
            handleGameNameCommand(line);
            break;

        case '+':  // B+ - Switch to SLIP mode
#ifdef DEBUG
            Serial2.println("[BLAST] Switching to SLIP protocol.");
#endif
            serialProtocolMode = PROTOCOL_SLIP;
            requestRedraw();
            break;

        default:
#ifdef DEBUG
            Serial2.print("[BLAST] Unknown command: B");
            Serial2.println(line[1]);
#endif
            break;
    }
}

void processBlastProtocol() {
    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == '\n') {
            lineBuffer[linePos] = '\0';
            if (linePos > 0) {
                processBlastLine(lineBuffer);
            }
            linePos = 0;
        } else if (c != '\r') {
            if (linePos < BLAST_LINE_BUFFER_SIZE - 1) {
                lineBuffer[linePos++] = c;
            }
        }
    }
}

void updateBlastFlash() {
    uint32_t now = millis();

    for (uint8_t i = 0; i < 12; i++) {
        if (!flashStates[i].active) continue;

        if (now - flashStates[i].lastToggle >= flashStates[i].halfPeriod) {
            flashStates[i].lastToggle = now;
            flashStates[i].remaining--;

            if (flashStates[i].remaining == 0) {
                // Flash sequence complete.
                ledSetMode(i, LED_OFF, 0, 0, 0);
                flashStates[i].active = false;
            } else {
                // Toggle LED.
                flashStates[i].on = !flashStates[i].on;
                if (flashStates[i].on) {
                    ledSetMode(i, LED_ON, 4095, 0, 0);
                } else {
                    ledSetMode(i, LED_OFF, 0, 0, 0);
                }
            }
        }
    }
}
