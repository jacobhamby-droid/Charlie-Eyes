# Charlie Eyes

Desktop companion with squircle eyes for the Waveshare ESP32-S3-Touch-AMOLED-1.75 (466x466, CO5300).

## Get the firmware
1. Open the **Actions** tab, then the latest "Build Charlie Eyes" run (any branch).
2. Download the **charlie-eyes-firmware** artifact and unzip it.
3. `charlie-full.bin` is the single file to flash (at address 0x0).

## Flash from the browser (Chrome or Edge)
1. Plug the board in over USB (hold BOOT while plugging in if it isn't detected).
2. Open https://web.esphome.io, click **Connect**, pick the port, then **Install** and choose `charlie-full.bin`.

## What you should see
On boot: full-screen RED, GREEN, BLUE (about 0.7 s each, a quick color check), then Charlie's eyes.

Serial monitor (115200): `0`-`6` pick a mood, `a` toggles auto mood, `b` blink, `l` look around.
