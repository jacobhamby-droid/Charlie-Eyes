# Charlie Eyes

Desktop companion with expressive rounded-rectangle eyes for the Waveshare ESP32-S3-Touch-AMOLED-1.75 (466x466, CO5300).

Charlie v2 is much more alive: moods drift over time, he reacts to taps, petting (swipe back and forth), shaking, and being picked up / put down. There are synthesized sounds, eye shape presets, and a swipe-down menu.

## Get the firmware
1. Open the **Actions** tab, then the latest "Build Charlie Eyes" run (on the `charlie-v1` branch or after it builds).
2. Download the **charlie-eyes-firmware** artifact and unzip it.
3. `charlie-full.bin` is the single file to flash (at address 0x0).

## Flash from the browser (Chrome or Edge)
1. Plug the board in over USB (hold BOOT while plugging in if it isn't detected).
2. Open https://web.esphome.io, click **Connect**, pick the port, then **Install** and choose `charlie-full.bin`.

## Interactions
- **Tap** the screen: happy reaction (or angry if you tap too many times in a row).
- **Pet** by swiping back and forth: love mood + purring sounds.
- **Shake** the device: dizzy or angry.
- **Pick up / put down**: surprise then relief.
- **Swipe down from the top edge**: open the menu to change eye presets or mute.
- He gets sleepy if ignored, and can fall asleep (dim screen). Interact to wake him.

Serial monitor (115200):
- `n` neutral, `h` happy, `s` sleepy, `a` angry, `d` sad, `u` surprised, `l` love, `z` sleep/wake, `m` menu, `t` test next sound.

Settings (preset + mute) are saved and restored after power-off.
