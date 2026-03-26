# B.L.A.S.T. Firmware

Arduino/C++ firmware for RP2040/RP2350-based arcade controller.

## Build

- Built with Arduino IDE or arduino-cli
- Board: RP2040/RP2350 (Raspberry Pi Pico)
- Build output goes to `../_build/`
- Flash via `flash.sh` (triggers bootloader reset, uses `picotool`)

## Architecture

- `firmware.ino` — main entry point, pin definitions, setup/loop, button handling, LED config
- `debounce_mcp.cpp/h` — MCP23017 GPIO expander debouncing (interrupt-driven + periodic poll)
- `menu.cpp/h` — SSD1306 OLED display menu system
- `serializer.cpp/h` — SLIP-based serial protocol for device-to-app communication
- `storage.cpp/h` — profile storage in flash
- `keycodes.h` — key code and modifier definitions (MOD_NONE, MOD_CTRL, MOD_ALT, MOD_SHIFT, MOD_F, MOD_ESC)
- `_debug.h` — debug flag toggles (DEBUG, DEBUG_SLIP, DEBUG_LOOPTIME)

## Hardware

- MCP23017 I2C GPIO expander (address 0x20) for 16 button inputs with pull-ups
  - Port A (channels 0-7): pause, save, load, cursor directions, enter
  - Port B (channels 8-15): start, coin, action buttons (P1/P2)
- SSD1306 OLED display (I2C, address 0x3C, 128x64)
- USB HID keyboard emulation
- LED outputs with PWM (4 modes: OFF, ON, BREATHING, BLINKING)

## Debug

- `#define DEBUG` in `_debug.h` enables serial debug output on Serial2 (GP8=TX, GP9=RX)
- Debug serial runs at 115200 baud
- Additional toggles: DEBUG_SLIP (protocol tracing), DEBUG_LOOPTIME (loop cycle stats)

## Key conventions

- MCP pin constants are 1-indexed (`MCP_PIN_START_P1 = 9`), debouncer channels are 0-indexed (`channel = MCP_PIN - 1`)
- Button active-low (pull-up + button grounds the pin)
- Key presses are held for `globalSettings.keyPressDurationMs` then released
