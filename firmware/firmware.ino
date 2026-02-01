#include <Adafruit_MCP23X17.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Keyboard.h>
#include <splash.h>

#include "_debug.h"
#include "debounce_mcp.h"
#include "keycodes.h"
#include "menu.h"
#include "serializer.h"
#include "storage.h"

// Display configuration.
const uint8_t SCREEN_WIDTH{128};
const uint8_t SCREEN_HEIGHT{64};
const int8_t OLED_RESET{-1};
const uint8_t OLED_ADDRESS{0x3C};
const uint8_t MAX_VISIBLE_PROFILES{4};
// Firmware version
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 0
#define FIRMWARE_VERSION_PATCH 0

// Global variables.
MenuState currentMenuState{STATE_SPLASH};
uint8_t selectedProfileIndex{0};
uint8_t selectedProfileMenuItem{0};
uint32_t splashStartTime{0};
uint32_t keyPressTime{0};
bool keyPressed{false};
uint8_t currentProfileIndex{0};
ButtonMapping currentProfile;
bool redrawDisplay{true};
uint32_t lockTimeIntA{0L};
uint32_t lockTimeIntB{0L};
uint16_t tlcPwmBuffer[12]{0};
uint16_t tlcPwmDirtyBuffer[12]{0};
bool tlcDirty{false};

// LED operation modes.
enum LedMode {
    LED_OFF = 0,        // LED is off.
    LED_ON = 1,         // LED is constantly on at set brightness.
    LED_BREATHING = 2,  // LED fades in/out smoothly.
    LED_BLINKING = 3    // LED blinks on/off.
};

// Per-channel LED configuration.
struct LedChannelConfig {
        LedMode mode;          // Current operation mode.
        uint16_t brightness;   // Target brightness (0-4095) for ON mode.
        uint16_t period;       // Period in milliseconds for breathing/blinking.
        uint32_t phaseOffset;  // Phase offset in ms (for staggered effects).
};

// Array to hold configuration for all 12 channels.
LedChannelConfig ledChannels[12];

// Firmware settings instance
FirmwareSettings globalSettings = {
    FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH,
    50  // keyPressDurationMs (default 50ms)
};

// Button pin definitions.
const uint8_t MCP_PIN_START_P1{9};
const uint8_t MCP_PIN_START_P2{10};
const uint8_t MCP_PIN_COIN_P1{11};
const uint8_t MCP_PIN_COIN_P2{12};
const uint8_t MCP_PIN_ACTION_P1_1{13};
const uint8_t MCP_PIN_ACTION_P1_2{14};
const uint8_t MCP_PIN_ACTION_P2_1{15};
const uint8_t MCP_PIN_ACTION_P2_2{16};
const uint8_t MCP_PIN_PAUSE{1};
const uint8_t MCP_PIN_SAVE{2};
const uint8_t MCP_PIN_LOAD{3};
const uint8_t MCP_PIN_CURSOR_UP{7};
const uint8_t MCP_PIN_CURSOR_DOWN{6};
const uint8_t MCP_PIN_CURSOR_LEFT{4};
const uint8_t MCP_PIN_CURSOR_RIGHT{5};
const uint8_t MCP_PIN_ENTER{8};

// MCP23017 interrupt pin definitions.
const uint8_t PIN_MCP_INT_A{2};
const uint8_t PIN_MCP_INT_B{1};

// TLC59711 pin definitions.
const uint8_t PIN_TLC_CLK{6};
const uint8_t PIN_TLC_DOUT{7};

// RGB status LED pin definitions.
const uint8_t PIN_RGB_LED_R{16};
const uint8_t PIN_RGB_LED_G{18};
const uint8_t PIN_RGB_LED_B{20};

const uint8_t PIN_RESET_MCP{21};

// Devices.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_MCP23X17 mcp;
DebounceMCP debounceMcp(&mcp, PIN_MCP_INT_A, PIN_MCP_INT_B, 20, 1000, true);

void loadProfileAfterStart() {
    bool found = false;
    for (uint8_t i = 0; i < MAX_PROFILES; i++) {
        if (isProfileSlotUsed(i)) {
            loadProfile(i, &currentProfile);
            found = true;
#ifdef DEBUG
            Serial2.print("Profile ");
            Serial2.print(i);
            Serial2.print(" is used: ");
            Serial2.println(currentProfile.name);
#endif
#ifdef DEBUG
        } else {
            Serial2.print("Profile ");
            Serial2.print(i);
            Serial2.println(" is empty.");
#endif
        }
    }

    if (!found) {
        // No profiles found, load a default profile.
        strncpy(currentProfile.name, "Default", sizeof(currentProfile.name));
        currentProfile.startP1 = {MOD_NONE, '1'};
        currentProfile.startP2 = {MOD_NONE, '2'};
        currentProfile.coinP1 = {MOD_NONE, '4'};
        currentProfile.coinP2 = {MOD_NONE, '5'};
        currentProfile.actionP1_1 = {MOD_NONE, ' '};
        currentProfile.actionP1_2 = {MOD_NONE, ' '};
        currentProfile.actionP2_1 = {MOD_NONE, ' '};
        currentProfile.actionP2_2 = {MOD_NONE, ' '};
        currentProfile.serviceP1 = {MOD_NONE, '6'};
        currentProfile.serviceP2 = {MOD_NONE, '7'};
        currentProfile.testP1 = {MOD_NONE, '8'};
        currentProfile.testP2 = {MOD_NONE, '9'};
        currentProfile.pause = {MOD_ALT, 'P'};
        currentProfile.save = {MOD_F, 5};
        currentProfile.load = {MOD_F, 7};
        currentProfile.exit = {MOD_ESC, 0};
        saveProfile(0, &currentProfile);
        currentProfileIndex = 0;
#ifdef DEBUG
        Serial2.println("No profiles found. Loaded default profile.");
#endif
    }
}

// Apply gamma correction to 16-bit value (gamma = 2.8).
uint16_t gammaCorrect16(uint16_t value) {
    // Normalize to 0.0-1.0 range, apply gamma, scale back to 0-65535.
    float normalized = value / 65535.0;
    float corrected = pow(normalized, 2.8);
    return (uint16_t)(corrected * 65535.0 + 0.5);
}

// Apply gamma correction to 12-bit value (gamma = 2.8).
uint16_t gammaCorrect12(uint16_t value) {
    // Normalize to 0.0-1.0 range, apply gamma, scale back to 0-4095.
    float normalized = value / 4095.0;
    float corrected = pow(normalized, 2.8);
    return (uint16_t)(corrected * 4095.0 + 0.5);
}

void setRGBLed(uint16_t r, uint16_t g, uint16_t b) {
    // Apply 16-bit gamma correction for smooth low-level transitions.
    analogWrite(PIN_RGB_LED_R, gammaCorrect16(r));
    analogWrite(PIN_RGB_LED_G, gammaCorrect16(g));
    analogWrite(PIN_RGB_LED_B, gammaCorrect16(b));
}

void tlcSetLed(uint8_t channel, uint16_t value) {
    if (channel >= 12) {
        return;
    }
    // Apply gamma correction to 12-bit value.
    if (tlcPwmDirtyBuffer[channel] != value) {
        tlcPwmDirtyBuffer[channel] = value;
        uint16_t corrected = gammaCorrect12(value);
        tlcPwmBuffer[channel] = corrected;
        tlcDirty = true;
    }
}

void tlcUpdate() {
    if (!tlcDirty) {
        return;
    }
    tlcDirty = false;

    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));

    // Magic word for write
    uint32_t command = 0x25;

    command <<= 5;
    // OUTTMG = 1, EXTGCK = 0, TMGRST = 1, DSPRPT = 1, BLANK = 0 -> 0x16
    command |= 0x16;

    // Set global brightness values (BCr, BCg, BCb) fixed to max (127).
    // Red.
    command <<= 7;
    command |= 127;

    // Green.
    command <<= 7;
    command |= 127;

    // Blue.
    command <<= 7;
    command |= 127;

    // Send command bytes.
    SPI.transfer(command >> 24);
    SPI.transfer(command >> 16);
    SPI.transfer(command >> 8);
    SPI.transfer(command);

    // 12 channels per TLC59711
    for (int8_t c = 11; c >= 0; c--) {
        // 16 bits per channel, send MSB first
        SPI.transfer(tlcPwmBuffer[c] >> 8);
        SPI.transfer(tlcPwmBuffer[c]);
    }

    SPI.endTransaction();
}

// Initialize a channel's LED mode.
void ledSetMode(uint8_t channel, LedMode mode, uint16_t brightness = 4095, uint16_t period = 1000) {
    if (channel >= 12) return;

    ledChannels[channel].mode = mode;
    ledChannels[channel].brightness = brightness;
    ledChannels[channel].period = period;
    // Can be set separately for staggered effects.
    ledChannels[channel].phaseOffset = 0;
}

// Update all LEDs based on their modes (call in loop()).
void ledUpdate() {
    uint32_t now = millis();

    for (uint8_t i = 0; i < 12; i++) {
        uint16_t value = 0;
        uint32_t time = now + ledChannels[i].phaseOffset;

        switch (ledChannels[i].mode) {
            case LED_OFF:
                value = 0;
                break;

            case LED_ON:
                value = ledChannels[i].brightness;
                break;

            case LED_BREATHING:
                // Smooth sine wave breathing.
                {
                    float phase = (time % ledChannels[i].period) / (float)ledChannels[i].period;
                    float sine = sin(phase * 2.0 * PI);
                    value = (uint16_t)((sine * 0.5 + 0.5) * ledChannels[i].brightness);
                }
                break;

            case LED_BLINKING:
                // 50% duty cycle square wave.
                {
                    uint32_t halfPeriod = ledChannels[i].period / 2;
                    value = ((time % ledChannels[i].period) < halfPeriod) ? ledChannels[i].brightness : 0;
                }
                break;
            default:
                value = 0;
        }

        tlcSetLed(i, value);
    }

    tlcUpdate();
}

// Scan I2C bus for devices.
void scanI2C() {
#ifdef DEBUG
    Serial2.println("\nScanning I2C bus...");
#endif
    byte count = 0;

    for (byte address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        byte error = Wire.endTransmission();

        if (error == 0) {
#ifdef DEBUG
            Serial2.print("I2C device found at address 0x");
            if (address < 16) {
                Serial2.print("0");
            }
            Serial2.print(address, HEX);
            Serial2.println();
#endif
            count++;
        }
    }

#ifdef DEBUG
    if (count == 0) {
        Serial2.println("No I2C devices found.");
    } else {
        Serial2.print("Found ");
        Serial2.print(count);
        Serial2.println(" I2C device(s).");
    }
    Serial2.println();
#endif
}

void setup() {
    // Serial (aka Serial1) is USB CDC and used for main communication.
    Serial.begin(115200);
    // Serial2 is HW UART 1 and used for debug output.
    // Initialize Serial2 on GP8 (TX) and GP9 (RX) for debug output.
    Serial2.setTX(8);
    Serial2.setRX(9);
    Serial2.begin(115200);

#ifdef DEBUG
    delay(2500);
    Serial2.println("B.L.A.S.T. initializing.");
#endif

    // Initialize RGB status LED pins.
    pinMode(PIN_RGB_LED_R, OUTPUT);
    pinMode(PIN_RGB_LED_G, OUTPUT);
    pinMode(PIN_RGB_LED_B, OUTPUT);

#ifdef DEBUG
    Serial2.println("Initialize analog outputs.");
#endif
    analogWriteFreq(5000);
    analogWriteRange(65535);
    analogWriteResolution(16);

    // Start with green LED to indicate initialization.
    setRGBLed(0, 65535, 0);

#ifdef DEBUG
    Serial2.println("Initialize EEPROM.");
#endif
    EEPROM.begin(4096);
    // Initialize storage on first run.
    // Uncomment if required.
    // intializeStorage();

#ifdef DEBUG
    Serial2.println("Initialize Keyboard.");
#endif
    Keyboard.begin();

    pinMode(PIN_RESET_MCP, OUTPUT);
    digitalWrite(PIN_RESET_MCP, LOW);
    delay(10);
    digitalWrite(PIN_RESET_MCP, HIGH);

#ifdef DEBUG
    Serial2.println("Initialize I2C.");
#endif
    Wire.setSDA(4);
    Wire.setSCL(5);
    Wire.begin();
    Wire.setClock(400000);

    // Run I2C scanner.
    // scanI2C();

#ifdef DEBUG
    Serial2.println("Initialize display.");
#endif
    // Initialize display.
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
#ifdef DEBUG
        Serial2.println("SSD1306 allocation failed.");
        Serial2.flush();
#endif
        // Red LED for error.
        setRGBLed(255, 0, 0);
        while (1);
    }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 10);
    display.setTextSize(1);
    display.println("        .");
    display.println("        |");
    display.println("  -=[ B.L.A.S.T. ]=-");
    display.println("        |");
    display.println("     (INSERT COIN)");

    display.display();

    splashStartTime = millis();
    currentMenuState = STATE_SPLASH;

#ifdef DEBUG
    Serial2.println("Initialize MCP23017.");
#endif
    if (!mcp.begin_I2C(0x20, &Wire)) {
#ifdef DEBUG
        Serial2.println("MCP23017 initialization failed!");
        Serial2.flush();
#endif
        // Red LED for error.
        setRGBLed(255, 0, 0);
        while (1);
    }

    // Initialize TLC59711.
#ifdef DEBUG
    Serial2.println("Initialize TLC59711.");
    Serial2.flush();
#endif
    SPI.setMOSI(PIN_TLC_DOUT);
#ifdef DEBUG
    Serial2.println("Initialize TLC59711.2");
    Serial2.flush();
#endif
    SPI.setSCK(PIN_TLC_CLK);
#ifdef DEBUG
    Serial2.println("Initialize TLC59711.3");
    Serial2.flush();
#endif
    SPI.begin();
#ifdef DEBUG
    Serial2.println("Initialize TLC59711.4");
    Serial2.flush();
#endif
    memset(tlcPwmBuffer, 0, sizeof(tlcPwmBuffer));
    memset(tlcPwmDirtyBuffer, 0, sizeof(tlcPwmDirtyBuffer));
#ifdef DEBUG
    Serial2.println("Initialize TLC59711.5");
    Serial2.flush();
#endif

    // Initialize all LED channels to off.
    for (uint8_t i = 0; i < 12; i++) {
        // ledSetMode(i, LED_OFF);
        ledSetMode(i, LED_BREATHING, 4095, 2000);
    }
    // ledSetMode(0, LED_BREATHING, 4095, 2000);
    ledUpdate();

#ifdef DEBUG
    Serial2.println("Initialize buttons.");
#endif
    pinMode(PIN_MCP_INT_A, INPUT);
    pinMode(PIN_MCP_INT_B, INPUT);

    for (uint8_t i = 0; i < 16; i++) {
        // Setup all MCP pins as input with pull-up and interrupt on change.
        mcp.pinMode(i, INPUT_PULLUP);
        mcp.setupInterruptPin(i, CHANGE);
    }
    // Setup MCP interrupt configuration: not mirrored, active low.
    mcp.setupInterrupts(false, false, LOW);
    mcp.clearInterrupts();

#ifdef DEBUG
    Serial2.println("Load profiles.");
#endif
    loadProfileAfterStart();

#ifdef DEBUG
    Serial2.println("Initialization complete.");
#endif
    // Switch to blue LED to indicate ready state.
    setRGBLed(0, 0, 65535);
}

void loop() {
#ifdef DEBUG_LOOPTIME
    static uint32_t lastPrintTime = 0;
    static uint32_t minLoopTime = UINT32_MAX;
    static uint32_t maxLoopTime = 0;
    static uint32_t totalLoopTime = 0;
    static uint32_t loopCount = 0;
    uint32_t loopStartTime = micros();
#endif

    if (digitalRead(PIN_MCP_INT_A) == LOW && (millis() - lockTimeIntA >= 5)) {
        mcp.clearInterrupts();
        debounceMcp.interruptA();
        lockTimeIntA = millis();
    }
    if (digitalRead(PIN_MCP_INT_B) == LOW && (millis() - lockTimeIntB >= 5)) {
        mcp.clearInterrupts();
        debounceMcp.interruptB();
        lockTimeIntB = millis();
    }

    // Update all buttons.
    debounceMcp.update();

    // Handle menu navigation.
    handleMenuNavigation(
        debounceMcp.channelPressed(MCP_PIN_CURSOR_UP - 1), debounceMcp.channelPressed(MCP_PIN_CURSOR_RIGHT - 1),
        debounceMcp.channelPressed(MCP_PIN_CURSOR_DOWN - 1), debounceMcp.channelPressed(MCP_PIN_CURSOR_LEFT - 1),
        debounceMcp.channelPressed(MCP_PIN_ENTER - 1));

    // Update display.
    updateDisplay();

    // Handle key release after non-blocking delay.
    if (keyPressed && (millis() - keyPressTime >= 10)) {
        Keyboard.releaseAll();
        keyPressed = false;
    }

    // Check for button press events (only in profile mode, not during menu).
    if (currentMenuState == STATE_PROFILE && !keyPressed) {
        if (debounceMcp.channelPressed(MCP_PIN_START_P1 - 1)) {
            handleButtonPress(&currentProfile.startP1);
        }
        if (debounceMcp.channelPressed(MCP_PIN_START_P2 - 1)) {
            handleButtonPress(&currentProfile.startP2);
        }
        if (debounceMcp.channelPressed(MCP_PIN_COIN_P1 - 1)) {
            handleButtonPress(&currentProfile.coinP1);
        }
        if (debounceMcp.channelPressed(MCP_PIN_COIN_P2 - 1)) {
            handleButtonPress(&currentProfile.coinP2);
        }
        if (debounceMcp.channelPressed(MCP_PIN_ACTION_P1_1 - 1)) {
            handleButtonPress(&currentProfile.actionP1_1);
        }
        if (debounceMcp.channelPressed(MCP_PIN_ACTION_P1_2 - 1)) {
            handleButtonPress(&currentProfile.actionP1_2);
        }
        if (debounceMcp.channelPressed(MCP_PIN_ACTION_P2_1 - 1)) {
            handleButtonPress(&currentProfile.actionP2_1);
        }
        if (debounceMcp.channelPressed(MCP_PIN_ACTION_P2_2 - 1)) {
            handleButtonPress(&currentProfile.actionP2_2);
        }
        if (debounceMcp.channelPressed(MCP_PIN_PAUSE - 1)) {
            handleButtonPress(&currentProfile.pause);
        }
        if (debounceMcp.channelPressed(MCP_PIN_LOAD - 1)) {
            handleButtonPress(&currentProfile.load);
        }
        if (debounceMcp.channelPressed(MCP_PIN_SAVE - 1)) {
            handleButtonPress(&currentProfile.save);
        }
    }

    processSerialCommand();

    // Update LED effects.
    ledUpdate();

#ifdef DEBUG_LOOPTIME
    // Measure loop cycle time.
    uint32_t loopEndTime = micros();
    uint32_t loopTime = loopEndTime - loopStartTime;

    // Track statistics.
    if (loopTime < minLoopTime) minLoopTime = loopTime;
    if (loopTime > maxLoopTime) maxLoopTime = loopTime;
    totalLoopTime += loopTime;
    loopCount++;

    // Print statistics every second.
    if (millis() - lastPrintTime >= 1000) {
        uint32_t avgLoopTime = totalLoopTime / loopCount;
        Serial2.print("[LOOP] min: ");
        Serial2.print(minLoopTime);
        Serial2.print("us, max: ");
        Serial2.print(maxLoopTime);
        Serial2.print("us, avg: ");
        Serial2.print(avgLoopTime);
        Serial2.print("us, count: ");
        Serial2.println(loopCount);

        // Reset statistics.
        lastPrintTime = millis();
        minLoopTime = UINT32_MAX;
        maxLoopTime = 0;
        totalLoopTime = 0;
        loopCount = 0;
    }
#endif
}

void handleButtonPress(KeyCombo* key) {
#ifdef DEBUG
    Serial2.print("Button pressed - modifiers: ");
    Serial2.print(key->modifiers);
    Serial2.print(", key: ");
    Serial2.println(key->key);
#endif

    if (key->modifiers & MOD_ESC) {
#ifdef DEBUG
        Serial2.println("Pressing ESC key.");
#endif
        Keyboard.press(KEY_ESC);
    } else {
        // Press modifier keys if needed.
        if (key->modifiers & MOD_CTRL) {
            Keyboard.press(KEY_LEFT_CTRL);
        }
        if (key->modifiers & MOD_ALT) {
            Keyboard.press(KEY_LEFT_ALT);
        }
        if (key->modifiers & MOD_SHIFT) {
            Keyboard.press(KEY_LEFT_SHIFT);
        }

        if (key->modifiers & MOD_F) {
            // Map F1-F24.
            uint8_t fKey = key->key;
            if (fKey >= 1 && fKey <= 24) {
#ifdef DEBUG
                Serial2.print("Pressing F key: F");
                Serial2.println(fKey);
#endif
                Keyboard.press(KEY_F1 + (fKey - 1));
            }
        } else {
            // Regular key.
            Keyboard.press(key->key);
        }
    }

    // Start non-blocking timer for key release.
    keyPressTime = globalSettings.keyPressDurationMs;
    keyPressed = true;
}
