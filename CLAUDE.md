# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

B.L.A.S.T. (Button Logic & Arcade Simulation Terminal) is a configurable arcade controller with two main components:
- **Firmware** (`firmware/`) — C++/Arduino running on RP2040/RP2350 (Raspberry Pi Pico)
- **App** (`app/`) — Rust/egui desktop configuration tool (Linux, Windows, macOS)
- **PCB** (`pcb/`) — KiCAD hardware design files

The firmware and app communicate over USB CDC serial (115200 baud) using a SLIP-framed binary protocol with CRC-16-CCITT checksums.

## Build Commands

### App (Rust)
```bash
cd app
cargo build              # debug build
cargo build --release    # release build (LTO, size-optimized, stripped)
cargo test               # run tests
cargo clippy             # lint (warnings are CI failures)
cargo fmt --check        # check formatting
cargo fmt                # auto-format
```

### Firmware (Arduino)
```bash
# Build with Arduino CLI (board: rp2040:rp2040)
# Output goes to _build/
# Flash via firmware/flash.sh (uses picotool with bootloader reset)
```

## Architecture

### Communication Protocol
Both sides implement the same SLIP-based serial protocol:
- **App side:** `app/src/protocol.rs` (commands/responses) + `app/src/slip.rs` (framing)
- **Firmware side:** `firmware/serializer.cpp/h` (commands/responses + SLIP framing)
- Frame format: `[SLIP_END] [CMD] [LEN_L] [LEN_H] [PAYLOAD] [CRC_L] [CRC_H] [SLIP_END]`
- C structs use `#[repr(C, packed)]` on the Rust side for binary compatibility

### Firmware Hardware Interfaces
- **MCP23017** (I2C 0x20): 16-channel GPIO expander for buttons, interrupt-driven + polling fallback
- **SSD1306** (I2C 0x3C): 128x64 OLED display with menu state machine
- **USB HID**: Keyboard emulation for button-to-key mapping
- **PWM LEDs**: 4 modes (OFF, ON, BREATHING, BLINKING)

### App GUI
Immediate-mode GUI (egui/eframe). Long-running serial operations run on background threads with an overlay spinner. Theme and settings persisted via confy.

### Key Convention
MCP pin constants are 1-indexed (`MCP_PIN_START_P1 = 9`), but the debouncer uses 0-indexed channels (`channel = MCP_PIN - 1`). Buttons are active-low (pull-up, grounded when pressed).

## Development Workflow

Uses **GitFlow**: features branch from `develop`, releases merge to `main` then back to `develop`. PRs required for `main` and `develop` (1 review, CI must pass). See `GITFLOW.md` for details.

Branch naming: `feature/*`, `bugfix/*`, `release/*`, `hotfix/*`

## CI/CD

- **app-build.yml**: Builds on Linux/Windows/macOS, runs clippy + fmt check
- **firmware-build.yml**: Arduino CLI compile + cppcheck linting
- **release.yml**: Manual dispatch, creates release branch with version bumps
- **tag-release.yml**: Auto-tags on release merge to main, creates GitHub Release

## Rust Code Standards

- No `unwrap()` without justification — use `Result<T, E>` with anyhow/thiserror
- Clippy warnings are build failures in CI
- Release profile: LTO enabled, `opt-level = "z"`, panic=abort, symbols stripped

## Firmware Debug

Enable flags in `firmware/_debug.h`: `DEBUG` (serial output on GP8/GP9), `DEBUG_SLIP` (protocol tracing), `DEBUG_LOOPTIME` (loop cycle stats). Debug serial on Serial2 at 115200 baud.
