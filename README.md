# Charlie Eyes

Alive desktop companion with Emo/Vector/Cozmo-style rounded-rectangle eyes (with shading) for the Waveshare ESP32-S3-Touch-AMOLED-1.75.

## Features
- Mood system that drifts and reacts (happy, sleepy, angry, sad, love, surprised, dizzy, asleep)
- Touch, petting (swipe back and forth), shake, and pick-up/put-down reactions + cute sounds
- Autonomous idle animations so he feels alive
- Swipe **up from the bottom** to open the menu (shape presets + color presets)
- Settings remembered after power-off

## Get the firmware
1. Open the **Actions** tab → latest successful "Build Charlie Eyes" run.
2. Download the **charlie-eyes-firmware** artifact.
3. Flash `charlie-full.bin` (at 0x0) via https://web.esphome.io or esptool.

## Interactions
- Tap the screen
- Pet by swiping back and forth
- Shake or pick him up
- Swipe up from the bottom for the menu

Serial (115200): `n h s a d u l z m` for moods/menu, etc.

Future: Wi-Fi hotspot + Lovable app for personality quiz presets.
