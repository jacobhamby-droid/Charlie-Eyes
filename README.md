# Charlie Eyes

Alive desktop companion with soft cyan rounded-rectangle eyes for the Waveshare ESP32-S3-Touch-AMOLED-1.75.

## Features
- Full-screen rendering (no green bars, eyes never cut off)
- Soft cyan eyes with gentle shading
- Lots of living animations: varied looking, double-blinks, breathing, reactions
- Mood system that drifts and reacts to touch / petting / shake / pick-up
- Brightness control and soft alarm

## Get the firmware
1. Open the **Actions** tab → latest successful "Build Charlie Eyes" run.
2. Download the **charlie-eyes-firmware** artifact.
3. Flash `charlie-full.bin` (at 0x0) via https://web.esphome.io or esptool.

## Interactions
- Tap the screen
- Pet by swiping back and forth
- Shake or pick him up

## Serial (115200)
- `n h s g d u l z` — force moods / sleep
- `+` / `-` — brightness up/down
- `b120` — set brightness to 120 (saved)
- `a30` — set soft alarm in 30 minutes
- `off` — cancel alarm

Tap the screen to silence a ringing alarm.
