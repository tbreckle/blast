# B.L.A.S.T. Firmware

Arduino/C++ firmware for RP2040/RP2350-based arcade controller.

## Build

- Board package: Earle Philhower's Arduino-Pico core (`rp2040:rp2040`), version 6.1.1
- Local board config: `../.vscode/arduino.json` (`rpipicow`, `usbstack=picosdk`, output to `../_build/`)
- Build from repo root: `arduino-cli compile --fqbn rp2040:rp2040:rpipicow --output-dir _build firmware/firmware.ino`
- Lint (as CI): `cppcheck --enable=all --error-exitcode=1 --suppress=missingIncludeSystem --suppress=unusedFunction firmware/`
- Flash from this directory: `./flash.sh [-p PORT] [uf2]`. The port is found by USB VID/PID `f144:0001`, falling back to the first `/dev/ttyACM*`. It then does a 1200-baud touch to enter the bootloader, then `picotool load` + `picotool reboot`.

## Architecture

- `firmware.ino`: main entry point with pin definitions, setup/loop, button handling, the LED system (`ledSetMode`, `tlcSetLed`, `tlcUpdate`), and USB device naming
- `blast_protocol.cpp/h`: line-based BLAST text protocol for host LED control (default serial mode); also defines `LedMode` and `SerialProtocolMode`
- `serializer.cpp/h`: SLIP binary protocol used by the config app; defines `CMD_*`/`ERR_*` IDs and the `FirmwareSettings` struct
- `debounce_mcp.cpp/h`: MCP23017 debouncing (interrupt-driven + periodic poll)
- `menu.cpp/h`: SSD1306 OLED menu system (`requestRedraw()` to refresh)
- `storage.cpp/h`: profile persistence in flash-emulated EEPROM (`MAX_PROFILES` slots of `sizeof(ButtonMapping)`, 79 bytes). The layout version is stored in the last EEPROM byte (`STORAGE_VERSION`), and `migrateStorage()` converts old layouts at boot. Any change to the `ButtonMapping` layout needs a version bump plus a migration step.
- `keycodes.h`: key code and modifier definitions (MOD_NONE, MOD_CTRL, MOD_ALT, MOD_SHIFT, MOD_F, MOD_ESC)
- `_debug.h`: debug flag toggles
- `version.h`: `FIRMWARE_VERSION_MAJOR/MINOR/PATCH` (sent via `CMD_GET_VERSION`) and `FIRMWARE_VERSION_STRING` (splash screen). The committed file says `0.0.0`. CI overwrites it with `scripts/version.sh firmware-header <version>`. Don't commit a generated version.

## Serial protocol modes

USB CDC `Serial` (115200 baud) runs one parser at a time, selected by the global `serialProtocolMode`:

- **`PROTOCOL_BLAST`** (the default at boot) → `processBlastProtocol()`. The full spec is in `BLAST_PROTOCOL.md`; update it when changing the protocol. Commands are newline-terminated ASCII lines of the form `B{cmd}x{field}x{field}...`:
  - `B1`/`B2`: host connect/disconnect; both turn off all LEDs and cancel active flash sequences
  - `B3`–`B6`: LEDs for start/coin/action A/action B, format `B{n}x{player}x{value}[x{extra}]`. Player `0` means both players.
  - `B7x{value}[x{extra}]`: pause, save and load LEDs as a group
  - `BLx{gamename}`: switch to the first profile whose `ButtonMapping::gameName` equals the name exactly (via `findProfileByGameName()` + `activateProfile()`); ignored if nothing matches. An empty name (`BLx`) calls `returnToMainMenu()` when a profile or the service menu is active.
  - `B+`: switch to SLIP mode
  - LED values are 0=off, 1=on, 2=blink, 3=flash, 4=breath. `extra` is the blink/breath duration in ms, or the number of flashes.
  - "Flash" is a timed on/off sequence run by `updateBlastFlash()` in the main loop.
- **`PROTOCOL_SLIP`** → `processSerialCommand()`. The config app sends `B+\n` first. `CMD_SWITCH_BLAST` (0x0A) switches back.

When changing SLIP commands or `ButtonMapping`/`FirmwareSettings`, mirror the change in `app/src/protocol.rs` / `app/src/types.rs`. Structs are sent as raw packed bytes.

## Hardware

- MCP23017 I2C GPIO expander (address 0x20) for 16 button inputs with pull-ups
  - Port A (channels 0-7): pause, save, load, cursor directions, enter
  - Port B (channels 8-15): start, coin, action buttons (P1/P2)
- TLC59711 12-channel LED driver (CLK GP6, DOUT GP7)
  - `tlcSetLed()` applies gamma correction and writes to a dirty buffer, which `tlcUpdate()` flushes
  - `ledSetMode()` provides OFF / ON / BREATHING / BLINKING with brightness (0-4095), period and phase offset
- SSD1306 OLED display (I2C, address 0x3C, 128x64). It shows a "B"/"S" protocol mode indicator when `SHOW_PROTOCOL_MODE_INDICATOR` is defined.
- USB HID keyboard emulation. The USB device is renamed in `setup()` (product "B.L.A.S.T.", manufacturer "tbreckle", VID/PID 0xF144/0x0001).
- RGB status LED on direct GPIO pins (`PIN_RGB_LED_*`): green during boot and in SLIP mode, red on init failure, blue when running (dimmed in profile mode). Colors are set via `updateStatusLed()`.

## Debug

- `#define DEBUG` in `_debug.h` enables serial debug output on Serial2 (GP8=TX, GP9=RX, 115200 baud). Wrap debug prints in `#ifdef DEBUG`.
- Other toggles: `DEBUG_SLIP` (protocol tracing), `DEBUG_LOOPTIME` (loop cycle stats)

## Key conventions

- MCP pin constants (`MCP_PIN_*`) are 1-indexed; debouncer channels are 0-indexed (`channel = MCP_PIN - 1`).
- TLC pin constants (`TLC_PIN_*`) are 1-indexed; `ledSetMode()` / `tlcSetLed()` take `pin - 1`.
- `blast_protocol.cpp` duplicates the TLC pin map as `TLC_*` constants. Keep it in sync with `TLC_PIN_*` in `firmware.ino`.
- Buttons are active-low (pull-up + button grounds the pin).
- Key presses are held for `globalSettings.keyPressDurationMs` (default 50 ms) and then released.
