# Charlie Eyes

Super simple firmware for the **Waveshare ESP32-S3-Touch-AMOLED-1.75**.

**Goal:** Turn the screen on and show two eyes. Nothing else.

## How to use

1. Wait for the GitHub Action to finish building (Actions tab).
2. Download the `charlie-eyes-firmware` artifact.
3. Unzip it.
4. Flash with ESP Web Tools / esptool:
   - `bootloader.bin` → `0x0`
   - `partitions.bin` → `0x8000`
   - `firmware.bin` → `0x10000`

After flashing you should see two white eyes on a black screen.
