# Spool Tap

Firmware for an M5Stack CoreS3 SE with a Grove U216 NFC reader on Port A.
It scans tagged filament spools and confirms load or unload changes through FilaBridge.

The printed CoreS3 wedge and NFC dock are described in [cad/README.md](cad/README.md).

## Build and flash

Connect the CoreS3 SE and U216 to Port A.
Run `scripts/pio.sh device list` to find the USB serial port.
Run `scripts/pio.sh test -e cores3_test -f test_boot` to verify Unity output over USB CDC.
Run `scripts/build.sh` to generate the local credential header and build the firmware.
Run `scripts/build.sh run -e cores3 -t upload --upload-port /dev/cu.usbmodem2101` to flash the connected device.
Replace the sample port with the path shown by `device list` when it differs.

`scripts/build.sh` resolves the three references in `.env.tpl` through `op run` for that command only.
It writes `.pio-local/build_secrets.hpp` with mode 0600; Git ignores that directory.
Keep the generated header private and use `scripts/build.sh` for production builds.

`scripts/pio.sh` pins PlatformIO 6.2.0 and uses `/private/tmp/spool-tap-pio-home`, the toolchain from the device spike.
It also uses the spike's uv cache under `/private/tmp`.
macOS may clear `/private/tmp` on reboot, which requires downloading about 2.4 GB of toolchain packages again.
Set `PLATFORMIO_CORE_DIR`, `UV_CACHE_DIR`, or `UV_TOOL_DIR` before invoking the script to choose durable locations.

The `cores3` environment uses 16 MB dual-slot partitions and DIO flash mode.
QIO caused a watchdog boot loop during the spike.
The `rollback_probe` environment deliberately reboots before update validation; upload it only for a controlled rollback test.

## Use

Link a physical spool's tag in Spoolman's Tags → Add tag dialog.
Tap the tag on the U216, check the M5 preview and current assignment, then press Load and confirm to map that spool in FilaBridge.
Press Cancel to leave the assignment unchanged.
To unload, press Unload on Home and confirm the named spool; the tag is not needed.
After either confirmed action, Home shows the refreshed assignment.

## Device web pages

Find the device's LAN IP on its Status screen.
Open `http://<device-ip>/settings` on the same LAN.
Sign in with username `spool-tap` and the OTA password from the `spool-tap/device/password` 1Password field.
Edit the Spoolman and FilaBridge HTTPS origins, printer name, toolhead ID, and scan reader ID there.
Save the form to persist all five values together and refresh the mapping without a reboot.
Open `http://<device-ip>/logs` with the same sign-in to see recent safe events.
Open `http://<device-ip>/update` with the same sign-in to upload a complete application `firmware.bin`.
Keep the CoreS3 powered until the screen reports the result and the device reboots.
For a terminal upload, run `curl --digest -u spool-tap -F 'firmware=@.pio/build/cores3/firmware.bin' http://<device-ip>/update` and enter the password when prompted.
The Status screen shows the running firmware version after reboot.

The settings page shows only those five non-secret values.
Wi-Fi credentials and the OTA password still come from `.env.tpl` at build time; changing them requires a rebuild and upload.
Service hosts must present Let's Encrypt certificates that match their configured hostnames.
The device serves these pages over LAN HTTP with Digest authentication; keep the LAN trusted.
