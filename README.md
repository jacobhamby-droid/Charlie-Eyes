# Charlie Eyes

Desktop companion with squircle eyes for the Waveshare ESP32-S3-Touch-AMOLED-1.75.

## Get the firmware
1. Open the **Actions** tab, then the latest "Build Charlie Eyes" run for the **espnow** branch.
2. Download **charlie-eyes-firmware** and unzip it. `charlie-full.bin` is the one file to flash (address 0x0).

## Flash from the browser (Chrome or Edge)
Plug the board in (hold BOOT while plugging in if it isn't detected), open https://web.esphome.io,
click **Connect**, pick the port, then **Install** and choose `charlie-full.bin`.

## ESP-NOW (main feature on this branch)
Every Charlie broadcasts his mood about once a second. Nearby Charlies hear it and gently adopt a bit of that mood.
- Happy peer → he gets a little happier and may bounce
- Annoyed peer → he may grumble
Works with any number of Charlies on the same Wi-Fi channel (no router needed).

## Playing with Charlie
- **Poke** (tap): looks at your finger and reacts. Keep poking and he gets annoyed, then angry.
- **Pet** (rub or rest your hand): he squishes happily.
- **Gentle shake**: he gets happy and wiggles.
- **Hard shake**: dizzy eyes.
- **Drop**: scared.
- Left alone he looks around, daydreams, yawns, and eventually sleeps.
- Sounds are soft, lower-pitched, and pet-like.

## Serial (115200)
`d` demo, `n` next anim, `c` next color, `o` rotate, `m` mirror, `s` sleep/wake, `f` fps, `v` volume, `A`-`Z` sound test.

Menus are removed on this branch so the face stays the focus.
