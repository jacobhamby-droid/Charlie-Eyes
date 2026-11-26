# Charlie Eyes

Desktop companion with squircle eyes for the Waveshare ESP32-S3-Touch-AMOLED-1.75.

## Get the firmware
1. Open the **Actions** tab, then the latest "Build Charlie Eyes" run for the branch.
2. Download **charlie-eyes-firmware** and unzip it. `charlie-full.bin` is the one file to flash (address 0x0).

## Flash from the browser (Chrome or Edge)
Plug the board in (hold BOOT while plugging in if it isn't detected), open https://web.esphome.io,
click **Connect**, pick the port, then **Install** and choose `charlie-full.bin`.

## Playing with Charlie
- **Poke** (tap): he looks at your finger and reacts. Keep poking and he gets annoyed, then angry.
- **Pet** (rub back and forth, or rest your hand on him): he squishes happily and floats hearts.
- **Shake**: he gets dizzy and a little grumpy. **Drop** him and he's scared.
- **Swipe down from the top**: eye color menu. Tap a color to pick it, swipe up (or tap the middle) to close.
  His color is remembered after power off.
- **Rotate** the device any way: his face stays upright.
- Left alone he looks around, daydreams, yawns, wiggles, winks and eventually falls asleep. Any touch wakes him.
- His mood comes from how you treat him: petting makes him happy and affectionate, being ignored makes him
  bored and sad, being poked or shaken makes him grumpy.

### If his "up" is wrong
Open the menu, then press and hold the middle for about 1.2 seconds. Each hold turns his "up" by 90 degrees.

## Serial monitor (115200, optional)
`d` demo every animation, `n` next animation, `c` next color, `o` rotate up, `m` mirror rotation,
`s` sleep/wake, `f` show frame rate.

## Development
`src/charlie_core.h` has no hardware code. `test/sim.cpp` runs it on a PC and dumps frames:
`g++ -O2 -std=c++17 -o sim test/sim.cpp && ./sim anims out/an` then `python3 test/view.py sheet.png out/an_*.rgb565`.
