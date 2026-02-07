#!/bin/bash -e

FLASH_FILE="${1:-../_build/firmware.ino.uf2}"

# Try to find Pico device.
PORT=$(ls /dev/ttyACM* | head -n 1)

if [ -z "$PORT" ]; then
    echo "Pico not found in /dev/"
    exit 1
fi

echo "Found Pico on $PORT"

# Trigger reset to enter bootloader mode.
# This is done by setting the serial port to 1200 baud which the Pico firmware interprets as a reset command.
echo "Triggering reset on $PORT"
stty -F "$PORT" 1200
sleep 1

echo "Flashing $FLASH_FILE..."
picotool load "$FLASH_FILE"
echo "Flash complete."

# Reset the device to exit bootloader and run the new firmware.
sleep 1
echo "Resetting device..."
picotool reboot
