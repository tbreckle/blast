#ifndef DEBOUNCE_MCP_H
#define DEBOUNCE_MCP_H

#include <Adafruit_MCP23X17.h>
#include <stdint.h>

#include "_debug.h"

const uint8_t MCP_23017_CHANNELS = 16;  // Fixed channel count.

/**
 * Debouncer for MCP23017 GPIO expander.
 * Handles debouncing with interrupt detection and periodic polling.
 */
class DebounceMCP {
    public:
        /**
         * Constructor (fixed 16 channels).
         * @param mcpPtr Pointer to Adafruit_MCP23X17 instance.
         * @param intA Arduino pin connected to INT A.
         * @param intB Arduino pin connected to INT B.
         * @param debounce Debounce delay in milliseconds (default 20).
         * @param poll Full poll interval in milliseconds (default 1000).
         * @param activeLow Invert detection for pull-up inputs (default false).
         */
        DebounceMCP(Adafruit_MCP23X17* mcpPtr, uint8_t intA, uint8_t intB, unsigned long debounce = 20,
                    unsigned long poll = 1000, bool activeLow = false);

        /**
         * Destructor - frees allocated memory.
         */
        ~DebounceMCP();

        /**
         * Update debouncer state.
         * Call this regularly in your main loop.
         * Checks for interrupts and performs debouncing.
         */
        void update();

        /**
         * Check if any key was pressed (debounced).
         * @return true if any channel has been pressed since last reset.
         */
        bool pressed();

        /**
         * Check if a specific channel was pressed.
         * @param channel Channel number (0-15 for 16-channel expander).
         * @return true if this channel was pressed since last reset.
         */
        bool channelPressed(uint8_t channel);

        /**
         * Check if a specific channel is currently held (debounced state).
         * @param channel Channel number (0-15 for 16-channel expander).
         * @return true if the channel is currently in the active/pressed state.
         */
        bool channelHeld(uint8_t channel);

        /**
         * Set debounce delay.
         * @param delay Delay in milliseconds.
         */
        void setDebounceDelay(unsigned long delay);

        /**
         * Set poll interval.
         * @param interval Interval in milliseconds.
         */
        void setPollInterval(unsigned long interval);

        /**
         * Set active-low mode (invert detection).
         * When enabled, a channel is considered pressed when its GPIO bit is 0.
         */
        void setActiveLow(bool enabled);

        /**
         * Notify debouncer that INT A interrupt occurred.
         * Should be called from external ISR/handler.
         */
        void interruptA();

        /**
         * Notify debouncer that INT B interrupt occurred.
         * Should be called from external ISR/handler.
         */
        void interruptB();

    private:
        Adafruit_MCP23X17* mcp;
        uint8_t intPinA;  // Arduino pin for INT A
        uint8_t intPinB;  // Arduino pin for INT B

        uint8_t rawState[MCP_23017_CHANNELS];                  // Last raw GPIO reading per channel
        uint8_t debouncedState[MCP_23017_CHANNELS];          // Accepted stable state per channel
        uint8_t pressedState[MCP_23017_CHANNELS];            // Pending press events per channel
        unsigned long lastDebounceTime[MCP_23017_CHANNELS];  // When raw state last changed

        unsigned long debounceDelay;  // Debounce delay in ms (default 20)
        unsigned long pollInterval;   // Full poll interval in ms (default 1000)
        unsigned long lastFullPoll;   // Timestamp of last full poll
        bool activeLow;               // Invert detection for pull-up inputs

        // Pending interrupt flags set by external handlers (volatile for ISR safety)
        volatile bool pendingIntA;
        volatile bool pendingIntB;

        /**
         * Read GPIO register and handle debouncing
         * @param port 0 for PORT A, 1 for PORT B
         */
        void readPort(uint8_t port);

        /**
         * Process channel state change
         * @param channel Channel number
         * @param newState New state of the channel
         */
        void processChannel(uint8_t channel, uint8_t newState);

        // No direct pin reading; interrupts are handled externally.
};

#endif  // DEBOUNCE_MCP_H
