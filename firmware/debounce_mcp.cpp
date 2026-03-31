#include "debounce_mcp.h"

#include <Arduino.h>
#include <string.h>

DebounceMCP::DebounceMCP(Adafruit_MCP23X17* mcpPtr, uint8_t intA, uint8_t intB, unsigned long debounce,
                         unsigned long poll, bool activeLow_)
    : mcp(mcpPtr),
      intPinA(intA),
      intPinB(intB),
      debounceDelay(debounce),
      pollInterval(poll),
      lastFullPoll(0),
      activeLow(activeLow_) {
    // Initialize to idle state (pull-up = 1 for active-low).
    uint8_t idleState = activeLow ? 1 : 0;
    memset(rawState, idleState, sizeof(rawState));
    memset(debouncedState, idleState, sizeof(debouncedState));
    memset(pressedState, 0, sizeof(pressedState));
    memset(lastDebounceTime, 0, sizeof(lastDebounceTime));

    pendingIntA = false;
    pendingIntB = false;
}

DebounceMCP::~DebounceMCP() {}

void DebounceMCP::update() {
    unsigned long currentTime = millis();

    // Handle pending interrupts signaled by external handlers.
    if (pendingIntA) {
#ifdef DEBUG
        Serial2.println("DebounceMCP: Handling interrupt A.");
#endif
        pendingIntA = false;
        readPort(0);
    }

    if (pendingIntB) {
#ifdef DEBUG
        Serial2.println("DebounceMCP: Handling interrupt B.");
#endif
        pendingIntB = false;
        readPort(1);
    }

    // Check if any channel's raw state has been stable long enough to accept.
    for (uint8_t ch = 0; ch < MCP_23017_CHANNELS; ch++) {
        if (rawState[ch] != debouncedState[ch]) {
            if (currentTime - lastDebounceTime[ch] >= debounceDelay) {
                debouncedState[ch] = rawState[ch];
                const bool isActive = activeLow ? (rawState[ch] == 0) : (rawState[ch] == 1);
                if (isActive) {
                    pressedState[ch] = 1;
                }
            }
        }
    }

    // Perform full poll periodically to catch any missed interrupts.
    if (currentTime - lastFullPoll >= pollInterval) {
        lastFullPoll = currentTime;
        readPort(0);
        readPort(1);
    }
}

bool DebounceMCP::pressed() {
    for (uint8_t i = 0; i < 16; i++) {
        if (pressedState[i]) {
            return true;
        }
    }
    return false;
}

bool DebounceMCP::channelPressed(uint8_t channel) {
    if (channel >= 16) {
        return false;
    }
    // Consume the press event on read so it persists across update()
    // cycles until actually handled.
    bool wasPressed = pressedState[channel] != 0;
    pressedState[channel] = 0;
    return wasPressed;
}

bool DebounceMCP::channelHeld(uint8_t channel) {
    if (channel >= MCP_23017_CHANNELS) return false;
    return activeLow ? (debouncedState[channel] == 0) : (debouncedState[channel] == 1);
}

void DebounceMCP::setDebounceDelay(unsigned long delay) {
    debounceDelay = delay;
}

void DebounceMCP::setPollInterval(unsigned long interval) {
    pollInterval = interval;
}

void DebounceMCP::readPort(uint8_t port) {
    uint8_t portData = 0;
    uint8_t startChannel = 0;

    if (port == 0) {
        portData = mcp->readGPIOA();
        startChannel = 0;
    } else {
        portData = mcp->readGPIOB();
        startChannel = 8;
    }

    unsigned long currentTime = millis();

    for (uint8_t i = 0; i < 8; i++) {
        uint8_t channel = startChannel + i;
        uint8_t newState = (portData >> i) & 0x01;

        // If raw state changed, restart the debounce timer.
        if (newState != rawState[channel]) {
            rawState[channel] = newState;
            lastDebounceTime[channel] = currentTime;
        }
    }
}

void DebounceMCP::processChannel(uint8_t channel, uint8_t newState) {
    // No longer used — debounce logic is in update() and readPort().
}

void DebounceMCP::setActiveLow(bool enabled) {
    activeLow = enabled;
}

void DebounceMCP::interruptA() {
    pendingIntA = true;
}

void DebounceMCP::interruptB() {
    pendingIntB = true;
}
