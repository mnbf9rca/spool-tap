# Spool Tap

Firmware for an M5Stack CoreS3 SE with a Grove U216 NFC reader on Port A.
This first task boots the reader and proves the USB CDC test transport.

## Build and flash

Connect the CoreS3 SE and U216 to Port A.
Run `scripts/pio.sh device list` to find the USB serial port.
Run `scripts/pio.sh test -e cores3_test -f test_boot` to verify Unity output over USB CDC.
Run `scripts/pio.sh run -e cores3` to build the firmware.
Run `scripts/pio.sh run -e cores3 -t upload --upload-port /dev/cu.usbmodem2101` to flash the connected device.
Replace the sample port with the path shown by `device list` when it differs.

`scripts/pio.sh` pins PlatformIO 6.2.0 and uses `/private/tmp/spool-tap-pio-home`, the toolchain from the device spike.
It also uses the spike's uv cache under `/private/tmp`.
macOS may clear `/private/tmp` on reboot, which requires downloading about 2.4 GB of toolchain packages again.
Set `PLATFORMIO_CORE_DIR`, `UV_CACHE_DIR`, or `UV_TOOL_DIR` before invoking the script to choose durable locations.

The `cores3` environment uses 16 MB dual-slot partitions and DIO flash mode.
QIO caused a watchdog boot loop during the spike.
