# B.L.A.S.T. Configuration Tool

Rust/Slint desktop app for configuring the B.L.A.S.T. arcade controller over serial.

## Build

```bash
cargo build           # debug build (build.rs compiles ui/app.slint)
cargo build --release # release build (optimized for size, LTO, stripped)
cargo test            # unit tests (keymap, types, slip, ui::tests for AppState logic)
```

Slint 1.18 needs Rust 1.92+. On Linux the build needs `libudev-dev` (serialport), `libfontconfig-dev` and `pkg-config`: Slint's font loading (`yeslogic-fontconfig-sys`) links fontconfig/freetype in the app build (only the `slint-build` build-dependency uses dlopen). X11/Wayland and xkbcommon are loaded at runtime.

## Architecture

- `ui/app.slint` — the whole UI: menu bar, toolbar, profile list, status bar, and in-window dialogs (busy overlay, confirmations, profile editor, About with `AboutSlint`). It holds no state of its own: Rust pushes properties and models, the UI reports actions via callbacks.
- `ui.rs` — `AppState` (plain Rust state and logic, unit-tested) and `Controller` (wires Slint callbacks, pushes state to the UI in `refresh()`).
  - Serial operations run on a background thread via `start_operation()`; a `slint::Timer` polls the result channel and applies it on the UI thread.
  - After the window closes, `run()` calls `Controller::shutdown()`, which waits for a running operation and switches the firmware back to BLAST mode.
- `keymap.rs` — maps captured Slint `KeyEvent.text` to firmware `KeyCombo`s (letters, digits, Space, Enter, F1–F24; ESC clears; modifier-only presses are ignored).
- `protocol.rs` — serial protocol: commands, responses, firmware communication
- `types.rs` — `#[repr(C, packed)]` structs matching the firmware (`ButtonMapping` is 79 bytes)
- `slip.rs` — SLIP encoder/decoder for framing serial data

## Conventions

- After changing `AppState`, call `refresh()` (or use `Controller::update()`, which does both).
- Models (`VecModel`) are updated through `sync_model()`, which only replaces changed rows so widgets keep focus and state.
- Row checkboxes use two-way bindings (`checked <=> binding.ctrl`) so model updates from Rust stay visible after the user clicked them.

## Licensing

Slint is used under the Slint Royalty-free Desktop, Mobile, and Web Applications License 2.0. It requires attribution: keep the `AboutSlint` widget in the About dialog (reachable from the top-level Help menu) and the "Made with Slint" badge in the root README.md.

## Key dependencies

- `slint` 1.18 (+ `slint-build`) — UI toolkit (winit backend, femtovg + software renderer)
- `serialport` 4.5 — serial port communication
- `confy` 0.5 — persistent app configuration (theme)
- `serde` 1.0 — serialization for configs
