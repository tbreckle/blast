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
    // Allocate memory for state tracking arrays.
    // Use fixed-size arrays; just clear them.

    // Initialize arrays.
    memset(previousState, 0, sizeof(previousState));
    memset(pressedState, 0, sizeof(pressedState));
    memset(lastDebounceTime, 0, sizeof(lastDebounceTime));

    // Initialize pending interrupt flags.
    pendingIntA = false;
    pendingIntB = false;
}

DebounceMCP::~DebounceMCP() {}

void DebounceMCP::update() {
    unsigned long currentTime = millis();

    // Reset pressed state at the beginning of each update cycle.
    memset(pressedState, 0, sizeof(pressedState));

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

    // Perform full poll periodically to catch any missed interrupts.
    if (currentTime - lastFullPoll >= pollInterval) {
        lastFullPoll = currentTime;

        // Poll both ports.
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
    return pressedState[channel] != 0;
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

    // Read appropriate GPIO register.
    if (port == 0) {
        portData = mcp->readGPIOA();
        startChannel = 0;
    } else {
        portData = mcp->readGPIOB();
        startChannel = 8;
    }

    unsigned long currentTime = millis();

    // Process each channel in this port.
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t channel = startChannel + i;
        // Channel is guaranteed within 0..15.

        // Extract bit for this channel.
        uint8_t newState = (portData >> i) & 0x01;

        // Check if enough time has passed since last debounce for this channel.
        if (currentTime - lastDebounceTime[channel] >= debounceDelay) {
            processChannel(channel, newState);
            lastDebounceTime[channel] = currentTime;
        }
    }
}

void DebounceMCP::processChannel(uint8_t channel, uint8_t newState) {
    // Check if state changed from previous state.
    if (newState != previousState[channel]) {
        previousState[channel] = newState;

        // Determine active level based on configuration.
        const bool isActive = activeLow ? (newState == 0) : (newState == 1);
        if (isActive) {
            pressedState[channel] = 1;
        }
    }
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
