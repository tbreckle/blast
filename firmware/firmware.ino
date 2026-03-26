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
MenuState oldMenuState{STATE_NONE};

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

const uint8_t TLC_PIN_START_P1{1};
const uint8_t TLC_PIN_START_P2{11};
const uint8_t TLC_PIN_COIN_P1{2};
const uint8_t TLC_PIN_COIN_P2{8};
const uint8_t TLC_PIN_ACTION_P1_1{10};
const uint8_t TLC_PIN_ACTION_P1_2{12};
const uint8_t TLC_PIN_ACTION_P2_1{7};
const uint8_t TLC_PIN_ACTION_P2_2{9};
const uint8_t TLC_PIN_PAUSE{4};
const uint8_t TLC_PIN_SAVE{5};
const uint8_t TLC_PIN_LOAD{6};

// MCP23017 interrupt pin definitions.
const uint8_t PIN_MCP_INT_A{2};
const uint8_t PIN_MCP_INT_B{1};

// TLC59711 pin definitions.
const uint8_t PIN_TLC_CLK{6};
const uint8_t PIN_TLC_DOUT{7};

// RGB status LED pin definitions.
const uint8_t PIN_RGB_LED_R{20};
const uint8_t PIN_RGB_LED_G{18};
const uint8_t PIN_RGB_LED_B{16};

const uint8_t PIN_RESET_MCP{21};

// Map TLC LED pins to corresponding KeyCombo pointers in currentProfile.
struct LedProfileMap {
        uint8_t tlcPin;
        KeyCombo* keyCombo;
};

LedProfileMap ledProfileMap[] = {{TLC_PIN_START_P1, &currentProfile.startP1},
                                 {TLC_PIN_START_P2, &currentProfile.startP2},
                                 {TLC_PIN_COIN_P1, &currentProfile.coinP1},
                                 {TLC_PIN_COIN_P2, &currentProfile.coinP2},
                                 {TLC_PIN_ACTION_P1_1, &currentProfile.actionP1_1},
                                 {TLC_PIN_ACTION_P1_2, &currentProfile.actionP1_2},
                                 {TLC_PIN_ACTION_P2_1, &currentProfile.actionP2_1},
                                 {TLC_PIN_ACTION_P2_2, &currentProfile.actionP2_2},
                                 {TLC_PIN_PAUSE, &currentProfile.pause},
                                 {TLC_PIN_LOAD, &currentProfile.load},
                                 {TLC_PIN_SAVE, &currentProfile.save}};

// Devices.
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_MCP23X17 mcp;
DebounceMCP debounceMcp(&mcp, PIN_MCP_INT_A, PIN_MCP_INT_B, 20, 1000, true);

void loadProfileAfterStart() {
    /**
     * Load the first used profile from storage after startup.
     *
     * If no profiles are found, a default profile is created and saved.
     *
     */
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

uint16_t gammaCorrect16(uint16_t value) {
    /**
     * Apply gamma correction to a 16-bit LED brightness value.
     *
     * @param value The original 16-bit brightness value (0-65535).
     * @return The gamma-corrected 16-bit brightness value (0-65535).
     *
     * Gamma correction is applied to ensure that the perceived brightness
     * of the LED changes linearly with the input value. This is important
     * for achieving smooth brightness transitions and consistent color representation.
     * The gamma value used is 2.8, which is commonly used for LED applications.
     *
     */
    // Normalize to 0.0-1.0 range, apply gamma, scale back to 0-65535.
    float normalized = value / 65535.0;
    float corrected = pow(normalized, 2.8);
    return (uint16_t)(corrected * 65535.0 + 0.5);
}

uint16_t gammaCorrect12(uint16_t value) {
    /**
     * Apply gamma correction to a 12-bit LED brightness value.
     *
     * @param value The original 12-bit brightness value (0-4095).
     * @return The gamma-corrected 12-bit brightness value (0-4095).
     *
     * Gamma correction is applied to ensure that the perceived brightness
     * of the LED changes linearly with the input value. This is important
     * for achieving smooth brightness transitions and consistent color representation.
     * The gamma value used is 2.8, which is commonly used for LED applications.
     *
     */
    // Normalize to 0.0-1.0 range, apply gamma, scale back to 0-4095.
    float normalized = value / 4095.0;
    float corrected = pow(normalized, 2.8);
    return (uint16_t)(corrected * 4095.0 + 0.5);
}

void setRGBLed(uint16_t r, uint16_t g, uint16_t b) {
    /**
     * Set the RGB status LED color with gamma correction.
     *
     * @param r Red component (0-65535).
     * @param g Green component (0-65535).
     * @param b Blue component (0-65535).
     *
     */
    // Apply 16-bit gamma correction for smooth low-level transitions.
    analogWrite(PIN_RGB_LED_R, 65535 - gammaCorrect16(r));
    analogWrite(PIN_RGB_LED_G, 65535 - gammaCorrect16(g));
    analogWrite(PIN_RGB_LED_B, 65535 - gammaCorrect16(b));
}

void setRGBLedFailed() {
    /**
     * Indicate a failure state using the RGB status LED.
     */
    setRGBLed(65535, 0, 0);  // Set LED to red.
}

void setRGBLedSuccess(bool dim = false) {
    /**
     * Indicate a success state using the RGB status LED.
     *
     * @param dim If true, set to a dimmer blue color.
     *
     */
    if (dim) {
        setRGBLed(0, 0, 16384);
    } else {
        setRGBLed(0, 0, 65535);
    }
}

void tlcSetLed(uint8_t channel, uint16_t value) {
    /**
     * Set the PWM value for a specific TLC59711 LED channel with gamma correction.
     *
     * @param channel The LED channel number (0-11).
     * @param value The desired PWM value (0-4095).
     *
     * This function updates the PWM buffer for the specified channel only if the new value
     * differs from the current value, marking the buffer as dirty for later update.
     * Gamma correction is applied to ensure consistent brightness perception.
     *
     */
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
    /**
     * Update the TLC59711 LED driver with the current PWM buffer values.
     *
     * This function sends the PWM values to the TLC59711 only if there have been changes
     * since the last update, as indicated by the `tlcDirty` flag.
     * If no changes are detected, the function returns immediately to avoid unnecessary SPI communication.
     * If changes are detected, it constructs the command sequence and transmitss the data via SPI.
     *
     */
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

void ledSetMode(uint8_t channel, LedMode mode, uint16_t brightness = 4095, uint16_t period = 1000,
                uint32_t phaseOffset = 0) {
    /**
     * Set the mode for a specific LED channel.
     *
     * @param channel The LED channel number (0-11).
     * @param mode The desired LED mode (LED_OFF, LED_ON, LED_BREATHING, LED_BLINKING).
     * @param brightness The brightness level (0-4095) for all modes.
     * @param period The period in milliseconds for cyclic modes.
     * @param phaseOffset The phase offset in milliseconds for staggered effects.
     *
     * @note If the channel number is invalid (>=12), the function returns without action.
     *
     */
    if (channel >= 12) {
        return;
    }

    ledChannels[channel].mode = mode;
    ledChannels[channel].brightness = brightness;
    ledChannels[channel].period = period;
    ledChannels[channel].phaseOffset = phaseOffset;
}

void ledUpdate() {
    /**
     * Update all LED channels based on their configured modes.
     *
     * This function calculates the appropriate brightness for each LED
     * channel based on its mode (OFF, ON, BREATHING, BLINKING) and updates
     * the TLC59711 accordingly.
     *
     */
    for (uint8_t i = 0; i < 12; i++) {
        uint16_t value = 0;
        uint32_t time = millis() + ledChannels[i].phaseOffset;

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

void scanI2C() {
    /**
     * Scan the I2C bus for connected devices and print their addresses to Serial2.
     *
     */
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

void handleMenuStateTransitions() {
    /**
     * Handle transitions between different menu states (and therefore general states).
     *
     */
    if (currentMenuState != oldMenuState) {
        // Menu state changed.
        oldMenuState = currentMenuState;
        if (currentMenuState == STATE_SELECT) {
            // Splash/select mode - breathing effect.
            for (uint8_t i = 0; i < 12; i++) {
                ledSetMode(i, LED_BREATHING, 4095, 2000);
            }
            setRGBLedSuccess();
        } else if (currentMenuState == STATE_PROFILE) {
            // Selected profile - solid on.
            for (uint8_t i = 0; i < 12; i++) {
                ledSetMode(i, LED_OFF);
            }

            for (const auto& map : ledProfileMap) {
                if (map.keyCombo->key != 0) {
                    ledSetMode(map.tlcPin - 1, LED_ON, 4095);
                }
            }
            setRGBLedSuccess(true);
        } else if (currentMenuState == STATE_SPLASH) {
            // Splash screen - fast blinking effect.
            for (uint8_t i = 0; i < 12; i++) {
                ledSetMode(i, LED_BLINKING, 4095, 200);
            }
        } else if (currentMenuState == STATE_SERVICEMENU) {
            // Service menu - slow blinking effect.
            for (uint8_t i = 0; i < 12; i++) {
                ledSetMode(i, LED_BLINKING, 4095, 1000);
            }
            setRGBLedSuccess();
        } else {
            // Unknown state - turn off LEDs.
#ifdef DEBUG
            Serial2.print("Unknown menu state: ");
            Serial2.println(currentMenuState);
#endif
            for (uint8_t i = 0; i < 12; i++) {
                ledSetMode(i, LED_OFF);
            }
        }
    }
}

void handleButtonPress(KeyCombo* key) {
    /**
     * Handle a single button press by sending the corresponding key combo.
     *
     * @param key Pointer to the KeyCombo structure representing the button press.
     *
     * @note This function presses the keys and starts a timer for releasing them.
     *       The actual release is handled in the main loop based on keyPressTime.
     *
     */
#ifdef DEBUG
    Serial2.print("Button pressed - modifiers:");
    if (key->modifiers == MOD_NONE) Serial2.print(" NONE");
    if (key->modifiers & MOD_CTRL) Serial2.print(" CTRL");
    if (key->modifiers & MOD_ALT) Serial2.print(" ALT");
    if (key->modifiers & MOD_SHIFT) Serial2.print(" SHIFT");
    if (key->modifiers & MOD_F) Serial2.print(" F-KEY");
    if (key->modifiers & MOD_ESC) Serial2.print(" ESC");
    Serial2.print(", key: ");
    if (key->modifiers & MOD_F) {
        Serial2.print("F");
        Serial2.println(key->key);
    } else if (key->modifiers & MOD_ESC) {
        Serial2.println("ESC");
    } else if (key->key >= 32 && key->key <= 126) {
        Serial2.println((char)key->key);
    } else {
        Serial2.println(key->key);
    }
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
    keyPressTime = millis();
    keyPressed = true;
}

void handleButtons() {
    /**
     * Handle button presses from MCP23017 via DebounceMCP.
     *
     * Checks each button channel and triggers the corresponding key press.
     *
     */
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

void setup() {
    /**
     * Controller initialization.
     *
     */
    // Serial (aka Serial1) is USB CDC and used for main communication with host.
    Serial.begin(115200);
    // Serial2 is HW UART 1 and used for debug output.
    // Initialize Serial2 on GP8 (TX) and GP9 (RX) for debug output.
    Serial2.setTX(8);
    Serial2.setRX(9);
    Serial2.begin(115200);

#ifdef DEBUG
    // delay(2500);
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
        setRGBLedFailed();
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
        setRGBLedFailed();
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
        ledSetMode(i, LED_OFF);
    }
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
    setRGBLedSuccess();
}

void loop() {
    /**
     * Main loop.
     *
     */
#ifdef DEBUG_LOOPTIME
    static uint32_t lastPrintTime = 0;
    static uint32_t minLoopTime = UINT32_MAX;
    static uint32_t maxLoopTime = 0;
    static uint32_t totalLoopTime = 0;
    static uint32_t loopCount = 0;
    uint32_t loopStartTime = micros();
#endif

    handleMenuStateTransitions();

    if (digitalRead(PIN_MCP_INT_A) == LOW && (millis() - lockTimeIntA >= 5)) {
        debounceMcp.interruptA();
        lockTimeIntA = millis();
    }
    if (digitalRead(PIN_MCP_INT_B) == LOW && (millis() - lockTimeIntB >= 5)) {
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
    if (keyPressed && (millis() - keyPressTime >= globalSettings.keyPressDurationMs)) {
        Keyboard.releaseAll();
        keyPressed = false;
    }

    // Check for button press events (only in profile mode, not during menu).
    if ((currentMenuState == STATE_PROFILE || currentMenuState == STATE_SERVICEMENU) && !keyPressed) {
        handleButtons();
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
