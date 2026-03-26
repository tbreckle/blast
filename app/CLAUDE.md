# B.L.A.S.T. Configuration Tool

Rust/egui desktop app for configuring the B.L.A.S.T. arcade controller over serial.

## Build

```bash
cargo build           # debug build
cargo build --release # release build (optimized for size, LTO, stripped)
```

## Architecture

- `main.rs` — entry point, eframe/egui window setup
- `ui.rs` — full GUI implementation (button mapping, profile management, LED config, theme)
- `protocol.rs` — serial protocol: commands, responses, firmware communication
- `types.rs` — core data structures (ButtonMapping, LedConfig, profiles)
- `slip.rs` — SLIP (Serial Line Internet Protocol) encoder/decoder for framing serial data

## Key dependencies

- `eframe`/`egui` 0.29 — immediate-mode GUI
- `serialport` 4.5 — serial port communication
- `confy` 0.5 — persistent app configuration
- `serde` 1.0 — serialization for configs and protocol

## Communication

- Serial over USB CDC at 115200 baud
- SLIP-encoded protocol for reliable framing
- Supports: profile CRUD, button mapping, LED configuration, firmware version query
