# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

B.L.A.S.T. (Button Logic & Arcade Simulation Terminal) is a configurable arcade controller with two main components:
- **Firmware** (`firmware/`): C++/Arduino running on RP2040/RP2350 (Raspberry Pi Pico), Arduino-Pico core 6.1.1
- **App** (`app/`): Rust/Slint desktop configuration tool (Linux, Windows, macOS)
- **PCB** (`pcb/`): KiCAD hardware design files

`app/CLAUDE.md` and `firmware/CLAUDE.md` hold the per-component file maps, dependencies and pin conventions.

## Build Commands

### App (Rust), run from `app/`
```bash
cargo build                  # debug build
cargo build --release        # release build (LTO, opt-level "z", panic=abort, stripped)
cargo test                   # run tests (keymap, types, slip, ui::tests)
cargo test keymap::tests::<name>   # run a single test
cargo clippy                 # lint (warnings are CI failures)
cargo fmt --check            # CI formatting check
```
On Linux the build needs `libudev-dev` (serialport), `libfontconfig-dev` (Slint's font loading links fontconfig/freetype) and `pkg-config`. Slint 1.18 needs Rust 1.92+.

### Firmware (Arduino CLI), run from the repo root
```bash
# Local board config lives in .vscode/arduino.json (rpipicow, usbstack=picosdk, output to _build/)
arduino-cli compile --fqbn rp2040:rp2040:rpipicow --output-dir _build firmware/firmware.ino
# CI compiles with --fqbn rp2040:rp2040:generic

# Lint the same way CI does
cppcheck --enable=all --error-exitcode=1 --suppress=missingIncludeSystem --suppress=unusedFunction firmware/

# Flash: run from firmware/ (the default UF2 path is ../_build/firmware.ino.uf2).
# Uses a 1200-baud touch to enter the bootloader, then picotool load and picotool reboot.
cd firmware && ./flash.sh [-p /dev/ttyACMx] [path/to/firmware.uf2]   # auto-detects port by VID/PID f144:0001
```

## Architecture

### Two serial protocols on one USB CDC port
The firmware's `Serial` (USB CDC, 115200 baud) runs in one of two modes, held in the global `serialProtocolMode` (`firmware.ino`). `loop()` sends incoming bytes to one of two parsers depending on the mode:

1. **BLAST text protocol** (`PROTOCOL_BLAST`, the default at boot): `firmware/blast_protocol.cpp`, specified in `firmware/BLAST_PROTOCOL.md`. It reads newline-terminated ASCII lines such as `B{cmd}x{player}x{value}[x{extra}]`. This is the host/game-facing interface for LED control:
   - `B1`/`B2`: host startup/shutdown
   - `B3`–`B6`: start/coin/action A/action B LEDs per player (player `0` = both)
   - `B7`: pause/save/load LEDs as a group
   - `B+`: switch to SLIP mode

   The "flash" LED effect is a timed on/off sequence driven by `updateBlastFlash()`, not one of the base LED modes.
2. **SLIP binary protocol** (`PROTOCOL_SLIP`): the config app's protocol.
   - App side: `app/src/protocol.rs` + `app/src/slip.rs`
   - Firmware side: `firmware/serializer.cpp/h`
   - Frame format: `[SLIP_END] [CMD] [LEN_L] [LEN_H] [PAYLOAD] [CRC_L] [CRC_H] [SLIP_END]`, CRC-16-CCITT
   - The app first sends `B+\n` to leave BLAST mode. `CMD_SWITCH_BLAST` (0x0A) switches back.

The OLED shows the active mode as "B" or "S" (controlled by `SHOW_PROTOCOL_MODE_INDICATOR`).

**Keep both sides in sync:**
- Command and error IDs are defined twice: `#define CMD_*`/`ERR_*` in `serializer.h` and `pub const CMD_*` in `protocol.rs`.
- The USB product name and VID/PID set in `setup()` (`firmware.ino`) are used by the app to preselect the controller (`BLAST_PRODUCT_NAME`/`BLAST_USB_ID` in `app/src/ui.rs`) and by `firmware/flash.sh` to find the port.
- Profile/settings structs are C structs in the firmware and `#[repr(C, packed)]` in `app/src/types.rs`. They are sent as raw bytes, so any field change has to be mirrored exactly on both sides.
- `SET_PROFILE` only updates RAM. `SAVE_PROFILE` persists to EEPROM (flash-emulated, `storage.cpp`, up to `MAX_PROFILES` slots of `sizeof(ButtonMapping)`).
- Changing the `ButtonMapping` layout also changes the EEPROM layout. Bump `STORAGE_VERSION` in `storage.h` and extend `migrateStorage()` so profiles on existing devices survive. The size test in `app/src/types.rs` guards the app side.

### Firmware hardware interfaces
- **MCP23017** (I2C 0x20): 16 button inputs, interrupt-driven with a polling fallback (`debounce_mcp.cpp`). Buttons are active-low.
- **TLC59711** (bit-banged SPI, CLK GP6 / DOUT GP7): 12-channel LED driver.
  - `tlcSetLed()` applies gamma correction and marks a dirty buffer; `tlcUpdate()` pushes it to the chip.
  - `ledSetMode()` implements the OFF/ON/BREATHING/BLINKING modes on top.
- **SSD1306** (I2C 0x3C): 128x64 OLED with a menu state machine (`menu.cpp`). Call `requestRedraw()` after any state change the display should show.
- **USB HID** keyboard emulation. `setup()` renames the USB device (product "B.L.A.S.T.", custom VID/PID).

### Index conventions (easy to get wrong)
- MCP pin constants are 1-indexed; debouncer channels are 0-indexed (`channel = MCP_PIN - 1`).
- TLC LED pin constants (`TLC_PIN_*` in `firmware.ino`) are also 1-indexed; `ledSetMode()` takes `pin - 1`.
- `blast_protocol.cpp` keeps its own copy of the TLC pin map (`TLC_*`), which must match the `TLC_PIN_*` constants in `firmware.ino`.

### App GUI
Slint UI: the layout is declared in `app/ui/app.slint` (compiled by `build.rs`), the state and logic live in `app/src/ui.rs` (`AppState` + `Controller`). Long-running serial operations run on background threads while a busy overlay is shown. Theme is persisted via confy. Slint is used under its royalty-free license, which requires the `AboutSlint` widget (Help → About) and the "Made with Slint" badge in README.md; don't remove either. See `app/CLAUDE.md`.

## Development Workflow

Uses **GitFlow**: features branch from `develop`; releases merge to `main` and then back to `develop`. PRs to `main` and `develop` need 1 review and passing CI. See `GITFLOW.md` and `.github/WORKFLOWS.md`.

Branch naming: `feature/*`, `bugfix/*`, `release/*`, `hotfix/*`

## CI/CD

- **app-build.yml**: build + test (release) on Linux/Windows/macOS, clippy, fmt check. Triggered by changes under `app/`.
- **firmware-build.yml**: Arduino CLI compile (produces the UF2 artifact) + cppcheck. Triggered by changes under `firmware/` or `pcb/`.
- **release.yml**: manual dispatch; creates a release branch with version bumps.
- **tag-release.yml**: tags automatically when a release merges to `main` and creates the GitHub Release.

## Rust Code Standards

- No `unwrap()` without justification; use `Result<T, E>` with anyhow/thiserror.
- `unsafe` byte transmutes of packed structs in `protocol.rs` carry `// SAFETY:` comments; keep that pattern.

## Firmware Debug

Enable flags in `firmware/_debug.h`:
- `DEBUG`: debug output on Serial2 (GP8/GP9, 115200 baud). Debug prints are wrapped in `#ifdef DEBUG`.
- `DEBUG_SLIP`: protocol tracing.
- `DEBUG_LOOPTIME`: loop cycle stats.
