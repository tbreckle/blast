#!/bin/bash -e

# Usage: ./flash.sh [-p PORT] [UF2_FILE]

# USB VID/PID set by the firmware (see USB.setVIDPID in firmware.ino).
BLAST_VID="f144"
BLAST_PID="0001"

PORT=""
while getopts "p:h" opt; do
    case "$opt" in
        p) PORT="$OPTARG" ;;
        h)
            echo "Usage: $0 [-p PORT] [UF2_FILE]"
            exit 0
            ;;
        *)
            echo "Usage: $0 [-p PORT] [UF2_FILE]"
            exit 1
            ;;
    esac
done
shift $((OPTIND - 1))

FLASH_FILE="${1:-../_build/firmware.ino.uf2}"

# Find the serial port of a USB device by VID/PID via sysfs.
find_port_by_vid_pid() {
    local tty usb_dev
    for tty in /sys/class/tty/ttyACM*; do
        [ -e "$tty/device" ] || continue
        # $tty/device is the USB interface; its parent is the USB device.
        usb_dev="$(readlink -f "$tty/device")/.."
        if [ "$(cat "$usb_dev/idVendor" 2>/dev/null)" = "$BLAST_VID" ] &&
            [ "$(cat "$usb_dev/idProduct" 2>/dev/null)" = "$BLAST_PID" ]; then
            echo "/dev/$(basename "$tty")"
            return
        fi
    done
}

if [ -n "$PORT" ]; then
    echo "Using user-specified port $PORT"
else
    PORT=$(find_port_by_vid_pid)
    if [ -n "$PORT" ]; then
        echo "Found B.L.A.S.T. ($BLAST_VID:$BLAST_PID) on $PORT"
    else
        # Fall back to the first ttyACM device.
        echo "No device with VID/PID $BLAST_VID:$BLAST_PID found, falling back to first /dev/ttyACM*"
        PORT=$(ls /dev/ttyACM* 2>/dev/null | head -n 1 || true)
        if [ -z "$PORT" ]; then
            echo "Pico not found in /dev/"
            exit 1
        fi
        echo "Found Pico on $PORT"
    fi
fi

if [ ! -e "$PORT" ]; then
    echo "Port $PORT does not exist"
    exit 1
fi

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
