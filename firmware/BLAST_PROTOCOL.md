# BLAST Serial Protocol

The BLAST protocol is a simple line-based ASCII protocol that lets host software control the button LEDs of the B.L.A.S.T. controller over USB serial.

Implementation: [`blast_protocol.cpp`](blast_protocol.cpp) / [`blast_protocol.h`](blast_protocol.h)

## Connection

| Setting | Value |
|---------|-------|
| Interface | USB CDC serial (`/dev/ttyACM*` on Linux, `COMx` on Windows) |
| USB VID/PID | `0xF144` / `0x0001` |
| USB product name | `B.L.A.S.T.` |
| Baud rate | 115200 (ignored by USB CDC, but set it anyway) |

The firmware starts in BLAST mode after every boot. The OLED shows the active mode as `B` (BLAST) or `S` (SLIP) in the top-right corner.

The protocol only goes from host to device. The firmware **never sends a response or acknowledgement**, and it silently ignores invalid commands. With `DEBUG` enabled in `_debug.h`, it logs parsed commands and errors on the debug UART (Serial2, GP8/GP9).

## Command format

```
B<cmd>[x<field>[x<field>...]]\n
```

- Every command starts with an uppercase `B` followed by a single command character.
- Fields are decimal integers, each preceded by `x` or `X`. The only exception is `BL`, which takes a text field.
- Commands end with `\n`. `\r` is ignored, so `\r\n` works too.
- Lines not starting with `B` are ignored, as are empty lines.
- The maximum line length is 63 characters; anything beyond that is dropped.

## Commands

| Command | Format | Description |
|---------|--------|-------------|
| `B1` | `B1` | Host startup. Turns off all LEDs. |
| `B2` | `B2` | Host shutdown. Turns off all LEDs. |
| `B3` | `B3x<player>x<value>[x<extra>]` | Start button LED |
| `B4` | `B4x<player>x<value>[x<extra>]` | Coin button LED |
| `B5` | `B5x<player>x<value>[x<extra>]` | Action A button LED |
| `B6` | `B6x<player>x<value>[x<extra>]` | Action B button LED |
| `B7` | `B7x<value>[x<extra>]` | Pause, save and load button LEDs (all three together) |
| `BL` | `BLx<gamename>` | Switch to the profile with this game name (see [Profile switching by game name](#profile-switching-by-game-name)) |
| `B+` | `B+` | Switch to SLIP mode (see [Switching to SLIP mode](#switching-to-slip-mode)) |

"Turns off all LEDs" means all 12 LED channels, including any flash sequence still running.

### Player

| Player | Target |
|--------|--------|
| `0` | Both players |
| `1` | Player 1 |
| `2` | Player 2 |

Commands with a player above `2` are ignored.

### LED values

| Value | Effect | `extra` (optional) | Default when `extra` is omitted |
|-------|--------|--------------------|---------------------------------|
| `0` | Off | ignored | – |
| `1` | On (full brightness) | ignored | – |
| `2` | Blink, 50% duty cycle | on-time (and off-time) in ms, 1–32767 | 500 ms |
| `3` | Flash a number of times, then turn off | number of flashes, 0–127 | 3 |
| `4` | Breathe (sine fade) | half the breathing cycle in ms, 1–32767 | 1000 ms |

Details:

- **Blink** and **breath** repeat until the LED gets another command. The full cycle is `2 × extra` ms. An `extra` of `0` uses the default, and values above 32767 are capped at 32767.
- **Flash** turns the LED on for 150 ms and off for 150 ms, `extra` times, and leaves it off afterwards. A count of `0` just turns the LED off. Counts above 127 are capped at 127.
- Numeric fields above 65535 are treated as 65535.
- Any new command for an LED replaces whatever that LED was doing before, including a flash still in progress.

## Profile switching by game name

`BLx<gamename>` switches the controller to the profile whose **game name** equals `<gamename>`. You set a profile's game name in the configuration app's profile editor. It is meant to be the game name MAMEHooker reports, so the controller can switch its key mapping when a game starts.

- Everything after `BLx` up to the end of the line is the game name. It is compared byte for byte: case-sensitive, with no trimming and no prefix matching.
- Game names can be at most 29 characters. Longer names never match.
- Profiles are searched in slot order and the first match wins. The config app prevents two profiles from sharing a game name.
- If a profile matches, the controller loads it and switches to the profile screen, just like selecting it in the menu. The LEDs are set for the new profile immediately, so LED commands sent after `BL` (even in the same write) are not overwritten.
- If the matching profile is already active on the profile screen, nothing happens and the LEDs are left alone.
- If no profile matches, the command is ignored.

```
BLxtcrisis      Switch to the profile with game name "tcrisis"
```

## Button-to-LED mapping

These are the TLC59711 channels in the firmware (1-indexed `TLC_PIN_*` constants in `firmware.ino`):

| Button | Player 1 | Player 2 |
|--------|----------|----------|
| Start (`B3`) | 1 | 11 |
| Coin (`B4`) | 2 | 8 |
| Action A (`B5`) | 10 | 7 |
| Action B (`B6`) | 12 | 9 |

| Button (`B7`) | Channel |
|---------------|---------|
| Pause | 4 |
| Save | 5 |
| Load | 6 |

Channel 3 is not mapped to any BLAST command.

## Examples

```
B1              Host started: all LEDs off
BLxsf2          Switch to the profile for game "sf2" (if there is one)
B3x1x1          Player 1 start LED on
B4x0x2          Both coin LEDs blink (500 ms on / 500 ms off)
B4x0x2x200      Both coin LEDs blink fast (200 ms on / 200 ms off)
B5x2x3x5        Player 2 action A LED flashes 5 times
B6x1x4x1500     Player 1 action B LED breathes with a 3 s cycle
B7x1            Pause, save and load LEDs on
B7x0            Pause, save and load LEDs off
B2              Host shutting down: all LEDs off
```

Sending commands from a Linux shell:

```bash
stty -F /dev/ttyACM0 115200 raw -echo
printf 'B1\nB3x0x2x250\n' > /dev/ttyACM0
```

## Switching to SLIP mode

The configuration app uses a separate binary SLIP protocol (`serializer.cpp`, `app/src/protocol.rs`) on the same serial port. Only one protocol is active at a time:

1. The host sends `B+\n`. The firmware switches to SLIP mode and treats everything after that as SLIP frames. No response is sent.
2. The host sends the SLIP command `CMD_SWITCH_BLAST` (`0x0A`). The firmware answers with `CMD_RESPONSE_OK` and switches back to BLAST mode.

The config app does both on its own when connecting and disconnecting. While the device is in SLIP mode it ignores BLAST commands, so a crashed or killed config app can leave it in SLIP mode until the next reboot.

## Interaction with the menu

The firmware also sets LEDs when the menu state changes. For example, selecting a profile lights the LEDs of all mapped buttons, and the service menu makes all LEDs blink. These changes overwrite any LED states set over BLAST. BLAST commands received after a menu change overwrite the menu's LED states in turn.
