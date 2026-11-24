# Nochi: a pocket desk robot with an OLED face

A tiny desk companion built on an ESP32-C3 SuperMini and a 0.96" 128×64 OLED. It sits on your desk and plays a different animated expression every few seconds, and it reacts when you touch it: a tap, double tap, triple tap or long press each trigger their own reaction.

Nochi is a hobby build **inspired by [Dasai Mochi](https://dasai.com.au/)**, the small animated desk robot. Dasai sells a finished product; this project rebuilds the idea from scratch with off-the-shelf parts: my own hardware build, firmware and touch interaction.



https://github.com/user-attachments/assets/8d98697c-e84a-486f-bcea-aad763c41262



## Features

- **32 animated expressions** played in random order, never the same one twice in a row, with a short idle pause between them.
- **Touch reactions** on a single capacitive touch pad, decoded in software:
  - single tap, double tap, triple tap and long press (≥ 700 ms), each mapped to its own animation;
  - taps that come less than 300 ms apart count as one combo.
- **Boot intro** made of pre-rendered 1-bit frames.
- **GIF playback straight from flash**: emotes are stored as GIF files inside C arrays and decoded frame by frame on the ESP32, so 32+ animations fit in the 4 MB flash of a €3 board.
- **A converter script** that turns any GIF into a new emote (see [Make your own emotes](#make-your-own-emotes)).

<p align="center"><img width="360" alt="Nochi desk robot showing an animated face on its OLED" src="https://github.com/user-attachments/assets/049bec0c-339c-4714-beec-1ecac35ed87c" /></p>


## How it works

```
 flash (PROGMEM)            ESP32-C3                                  OLED 128×64
 ┌──────────────┐   ┌──────────────────────────────────────┐   ┌──────────────┐
 │ _1 … _42     │──►│ AnimatedGIF decoder ─► GIFDraw()     │   │              │
 │ (GIF bytes)  │   │   palette → 1-bit threshold          │──►│  SSD1306     │
 │ boot frames  │──►│   pixels → 1 KB frame buffer         │I2C│  (800 kHz)   │
 └──────────────┘   │                                      │   └──────────────┘
 touch pad ────────►│ gesture state machine (10 ms poll)   │
                    │ tap / double / triple / long press   │
                    └──────────────────────────────────────┘
```

- **Rendering:** each emote is a complete GIF stored as a byte array. The [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) library decodes it one line at a time. `GIFDraw()` turns each colour into black or white by brightness, writes the pixels into a 1 KB buffer laid out like the SSD1306's memory (8 vertical pixels per byte), and pushes the finished frame over I2C at 800 kHz.
- **Boot intro:** these frames are already 1-bit bitmaps (`frames[n][1024]`) with their own delay table, so they are drawn directly without a GIF decoder.
- **Touch input:** while waiting between emotes, the firmware polls the touch pin every 10 ms and runs a small state machine. A rising edge starts a press; holding past 700 ms is a long press; each short release adds to a tap count; and 300 ms of silence after the last tap decides the combo. A gesture ends the idle wait at once, so the reaction feels immediate.

## Hardware

| Part | Notes |
|---|---|
| ESP32-C3 SuperMini | Any ESP32 works; change the pins in the sketch |
| 0.96" 128×64 OLED, SSD1306, I2C | White or blue |
| Capacitive touch module (e.g. TTP223) | Output HIGH when touched |
| Case | 3D-printed |

### Wiring

| Module | Pin | ESP32-C3 SuperMini |
|---|---|---|
| OLED | SDA | GPIO 21 |
| OLED | SCL | GPIO 20 |
| OLED | VCC / GND | 3V3 / GND |
| Touch | OUT | GPIO 1 |
| Touch | VCC / GND | 3V3 / GND |

If your touch module is active-LOW, swap `HIGH`/`LOW` in the touch block of `loop()`.

## Build and flash

1. Install the **esp32** board package by Espressif in the Arduino IDE and select **ESP32C3 Dev Module**. Turn on **USB CDC On Boot** if you want serial output over USB.
2. Install these libraries from the Library Manager, all by Larry Bank: **OneBitDisplay**, **BitBang_I2C**, **AnimatedGIF**.
3. Open `firmware/nochi_robot/nochi_robot.ino` and upload.
4. If the sketch is too big for the default partition, choose **Tools → Partition Scheme → Huge APP**.

## Make your own emotes

Every emote is a normal GIF stored in a header file as a byte array called `_<number>`. To add one:

1. **Get a clip.** Use any GIF, or cut one from a video with ffmpeg:
   ```bash
   ffmpeg -ss 00:00:03 -to 00:00:06 -i clip.mp4 \
     -vf "fps=15,scale=128:64:force_original_aspect_ratio=decrease" -y emote.gif
   ```
   High-contrast shapes, such as simple eyes and faces on a black background, look best on a 1-bit display.
2. **Convert it.** Install Pillow (`pip install pillow`) and run:
   ```bash
   python3 tools/gif_to_header.py emote.gif 43
   ```
   This scales the GIF to 128×64, makes it pure black and white, and writes `firmware/nochi_robot/src/emotes/43.h`. If too much or too little lights up, use `--threshold 0-255` to adjust; use `--invert` to swap black and white.
3. **Register it.** Add `#include "43.h"` to `src/emotes/animation.h`, then add a case to the `switch` in `loop()`:
   ```cpp
   case 33: playWrapper((uint8_t*)_43, sizeof(_43)); break;
   ```
   and raise `NUMBEROFANIMATION` to 33.
4. **Use it as a touch reaction (optional).** Swap one of the `_21` to `_24` calls in the "Handle press types" block for your array.

## Project structure

```
firmware/nochi_robot/
  nochi_robot.ino         display driver, GIF rendering, touch gestures, emote scheduler
  src/emotes/             emote GIFs as C arrays (_1 … _42) + animation.h index
  src/intros/             boot intro frames (1-bit bitmaps)
tools/
  gif_to_header.py        GIF → emote header converter
```

## Credits

- **Inspiration:** [Dasai Mochi](https://dasai.com.au/). Nochi is an independent, non-commercial fan build and is not affiliated with Dasai.
- **Emote animations (`src/emotes/`):** extracted from Dasai Mochi's emote video and shared by [huykhoong/esp32_dasai_mochi_clone_and_how_to](https://github.com/huykhoong/esp32_dasai_mochi_clone_and_how_to). All rights to these animations belong to their original creators. They are included here for personal, non-commercial use only. To replace them with your own, see [Make your own emotes](#make-your-own-emotes).
- **Libraries:** [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF), [OneBitDisplay](https://github.com/bitbank2/OneBitDisplay) and [BitBang_I2C](https://github.com/bitbank2/BitBang_I2C) by Larry Bank. The GIF-to-OLED drawing code follows the examples that come with AnimatedGIF.

## License

The firmware and tools written for this project are MIT-licensed (see [LICENSE](LICENSE)). The MIT license does **not** cover the emote animations in `src/emotes/`, which belong to their original creators.
