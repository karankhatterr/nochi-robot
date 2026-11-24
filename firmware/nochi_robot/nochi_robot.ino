#include <SPI.h>
#include <Wire.h>

#include <BitBang_I2C.h>
#include <OneBitDisplay.h>
#include <AnimatedGIF.h>

#include "src/emotes/animation.h"  // emote GIFs _1 ... _42 (see README)
#include "src/intros/devochi.h"    // boot intro: name text
#include "src/intros/still.h"      // boot intro: resting face

OBDISP obd;
AnimatedGIF gif;
static uint8_t ucOLED[4096]; // holds current frame for 128x64 OLED

// ---- Pin Mapping for ESP32-C3 SuperMini ----
#define RESET_PIN   -1
#define SDA_PIN     21    // connected to OLED SDA
#define SCL_PIN     20    // connected to OLED SCL
#define OLED_ADDR   -1
#define MY_OLED     OLED_128x64
#define USE_HW_I2C  1
#define FLIP180     0
#define INVERT      0

#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 64

// Reactions
#define LONG_PRESS_MS       700   // how long is a "long press"
#define MULTI_TAP_GAP_MS    300   // max gap between taps in a combo

// Touch sensor (digital) on GPIO 1
#define TOUCH_PIN   1

// ------------ Drawing helpers ------------

void DrawPixel(int x, int y, uint8_t ucColor) {
  uint8_t ucMask;
  int index;

  if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT)
    return;
  ucMask = 1 << (y & 7);
  index = x + ((y >> 3) << 7);
  if (ucColor)
    ucOLED[index] |= ucMask;
  else
    ucOLED[index] &= ~ucMask;
}

void GIFDraw(GIFDRAW* pDraw) {
  uint8_t* s;
  int x, y, iWidth;
  static uint8_t ucPalette[4096]; // thresholded palette

  if (pDraw->y == 0) { // first line, convert palette to 0/1
    for (x = 0; x < 256; x++) {
      uint16_t usColor = pDraw->pPalette[x];
      int gray = (usColor & 0xf800) >> 8;   // red
      gray += ((usColor & 0x7e0) >> 2);     // green*2
      gray += ((usColor & 0x1f) << 3);      // blue
      ucPalette[x] = (gray > 800) ? 1 : 0;
    }
  }

  y = pDraw->iY + pDraw->y; // current line
  iWidth = pDraw->iWidth;
  if (iWidth > DISPLAY_WIDTH)
    iWidth = DISPLAY_WIDTH;

  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2) { // restore to background color
    for (x = 0; x < iWidth; x++) {
      if (s[x] == pDraw->ucTransparent)
        s[x] = pDraw->ucBackground;
    }
    pDraw->ucHasTransparency = 0;
  }

  // Apply the new pixels to the main image
  if (pDraw->ucHasTransparency) { // if transparency used
    uint8_t c, ucTransparent = pDraw->ucTransparent;
    for (x = 0; x < iWidth; x++) {
      c = *s++;
      if (c != ucTransparent)
        DrawPixel(pDraw->iX + x, y, ucPalette[c]);
    }
  } else {
    s = pDraw->pPixels;
    for (x = 0; x < pDraw->iWidth; x++)
      DrawPixel(pDraw->iX + x, y, ucPalette[*s++]);
  }

  if (pDraw->y == pDraw->iHeight - 1) { // last line, render it
    obdDumpBuffer(&obd, ucOLED, 1, 1, 0);
  }
}

// ------------ Animation Helpers ------------
// ------------ Boot intro animations (pre-rendered 1-bit frames) ------------

// frames: [frameCount][1024] (128x64, 1bpp, row-major as in your test sketch)
void playIntroFrames(const myGIF &gif) {
    uint16_t bytesPerRow = (gif.width + 7) / 8;

    for (uint8_t frame = 0; frame < gif.frame_count; frame++) {

        memset(ucOLED, 0, sizeof(ucOLED));

        for (uint16_t y = 0; y < gif.height; y++) {
            for (uint16_t x = 0; x < gif.width; x++) {

                uint16_t byteIndex = y * bytesPerRow + (x / 8);
                uint8_t bitIndex   = 7 - (x % 8);

                uint8_t on = (gif.frames[frame][byteIndex] >> bitIndex) & 1;
                DrawPixel(x, y, on);
            }
        }

        obdDumpBuffer(&obd, ucOLED, 1, 1, 0);
        delay(gif.delays[frame]);
    }
}

void playWrapper(uint8_t* gifinput, int size) {
  if (gif.open(gifinput, size, GIFDraw)) {
    while (gif.playFrame(true, NULL)) {
      // frames streamed here
    }
    gif.close();
  }
}

#define NUMBEROFANIMATION 32
int n = NUMBEROFANIMATION;
int r;
int debugRandom = 0;
uint8_t last_animation = 0;

// ------------ Setup ------------

void setup() {
  Serial.begin(115200);

  // Touch sensor pin
  pinMode(TOUCH_PIN, INPUT_PULLUP); 
  // If your module outputs LOW when touched, use INPUT_PULLUP.
  // If it's HIGH when touched with internal pulldown, change accordingly.

  // I2C init handled by OneBitDisplay
  int rc = obdI2CInit(
      &obd,
      MY_OLED,
      OLED_ADDR,
      FLIP180,
      INVERT,
      USE_HW_I2C,
      SDA_PIN,
      SCL_PIN,
      RESET_PIN,
      800000L
  );
  Serial.print("obdI2CInit rc = ");
  Serial.println(rc);

  obdFill(&obd, 0, 1);  // clear display

  gif.begin(LITTLE_ENDIAN_PIXELS);
  // --- Boot intro ---
  playIntroFrames(devochi_gif);
  playIntroFrames(still_gif);


  // Startup animation (e.g. _31)
  // if (gif.open((uint8_t*)_1, sizeof(_1), GIFDraw)) {
  //   Serial.printf("Opened startup GIF; Canvas = %d x %d\n",
  //                 gif.getCanvasWidth(), gif.getCanvasHeight());
  //   while (gif.playFrame(true, NULL)) {
  //     // play startup frames
  //   }
  //   gif.close();
  // }
}

// ------------ Loop ------------

void loop() {
  // Random idle wait between animations (7.5 s or 15 s)
  r = random(1, 3) * 7500;
  Serial.print("Idle ms = ");
  Serial.println(r);

  // ---- Touch-aware wait with single/double/triple/long ----
  unsigned long start = millis();

  int pressType = 0;           // 0 = none, 1 = single, 2 = long, 3 = double, 4 = triple+
  int tapCount = 0;
  int lastState = digitalRead(TOUCH_PIN);
  unsigned long pressStart = 0;
  unsigned long lastReleaseTime = 0;

  while ((millis() - start < (unsigned long)r) && pressType == 0) {
    int currentState = digitalRead(TOUCH_PIN);

    // NOTE: assumes HIGH = touched, LOW = not touched.
    // If your sensor is active-LOW, swap HIGH/LOW in this block.

    // Rising edge: not touched -> touched
    if (currentState == HIGH && lastState == LOW) {
      pressStart = millis();
      // Serial.println("Touch started");
    }

    // Still held: check for long press
    if (currentState == HIGH && pressStart > 0) {
      unsigned long held = millis() - pressStart;
      if (held >= LONG_PRESS_MS) {
        pressType = 2;   // long press
        Serial.println("Long press detected");
        break;
      }
    }

    // Falling edge: touched -> released (short tap)
    if (currentState == LOW && lastState == HIGH && pressStart > 0) {
      unsigned long duration = millis() - pressStart;
      if (duration < LONG_PRESS_MS) {
        tapCount++;
        lastReleaseTime = millis();
        Serial.print("Short tap count = ");
        Serial.println(tapCount);
      }
      pressStart = 0;
    }

    // If we have at least one tap, check if the combo is finished
    if (tapCount > 0 && lastReleaseTime > 0) {
      unsigned long sinceLast = millis() - lastReleaseTime;

      if (sinceLast > MULTI_TAP_GAP_MS) {
        // No more taps in the allowed gap -> decide on combo
        if (tapCount == 1)       pressType = 1;  // single
        else if (tapCount == 2)  pressType = 3;  // double
        else                     pressType = 4;  // triple or more
        Serial.print("Tap combo decided, pressType = ");
        Serial.println(pressType);
        break;
      }
    }

    lastState = currentState;
    delay(10);  // poll every 10 ms
  }

  // If we run out of idle time but still had taps & no combo decision yet
  if (pressType == 0 && tapCount > 0) {
    if (tapCount == 1)       pressType = 1;
    else if (tapCount == 2)  pressType = 3;
    else                     pressType = 4;
    Serial.print("Idle timeout, tap combo decided, pressType = ");
    Serial.println(pressType);
  }

  // ---- Handle press types ----
  if (pressType == 1) {
    Serial.println("Single tap → Reaction A");
    // 👉 Single tap animation (e.g. heart)
    playWrapper((uint8_t*)_21, sizeof(_21));
    return;
  } else if (pressType == 3) {
    Serial.println("Double tap → Reaction B");
    // 👉 Double tap animation
    playWrapper((uint8_t*)_22, sizeof(_22));
    return;
  } else if (pressType == 4) {
    Serial.println("Triple+ tap → Reaction C");
    // 👉 Triple tap animation
    playWrapper((uint8_t*)_23, sizeof(_23));
    return;
  } else if (pressType == 2) {
    Serial.println("Long press → Reaction D");
    // 👉 Long press animation
    playWrapper((uint8_t*)_24, sizeof(_24));
    return;
  }

  // ---- If no touch gesture, continue with random animation selection ----

  if (debugRandom == 0) {
    r = random(0, n) + 1;  // 1..n
    Serial.print("Random choice: ");
    Serial.println(r);

    while (r == last_animation) {   // never play the same emote twice in a row
      r = random(0, n) + 1;
    }
    last_animation = r;
    Serial.print("Final choice: ");
    Serial.println(r);
  } else {
    static int counter = 0;
    counter++;
    if (counter > NUMBEROFANIMATION) {
      counter = 1;
    }
    r = counter;
    Serial.print("Debug sequential choice: ");
    Serial.println(r);
  }

  // ---- Play the selected random animation ----
  switch (r) {
    case 1:  playWrapper((uint8_t*)_1,  sizeof(_1));  break;
    case 2:  playWrapper((uint8_t*)_2,  sizeof(_2));  break;
    case 3:  playWrapper((uint8_t*)_3,  sizeof(_3));  break;
    case 4:  playWrapper((uint8_t*)_4,  sizeof(_4));  break;
    case 5:  playWrapper((uint8_t*)_5,  sizeof(_5));  break;
    case 6:  playWrapper((uint8_t*)_6,  sizeof(_6));  break;
    case 7:  playWrapper((uint8_t*)_40, sizeof(_40)); break;
    case 8:  playWrapper((uint8_t*)_8,  sizeof(_8));  break;
    case 9:  playWrapper((uint8_t*)_9,  sizeof(_9));  break;
    case 10: playWrapper((uint8_t*)_10, sizeof(_10)); break;
    case 11: playWrapper((uint8_t*)_36, sizeof(_36)); break;
    case 12: playWrapper((uint8_t*)_41, sizeof(_41)); break;
    case 13: playWrapper((uint8_t*)_13, sizeof(_13)); break;
    case 14: playWrapper((uint8_t*)_14, sizeof(_14)); break;
    case 15: playWrapper((uint8_t*)_34, sizeof(_34)); break;
    case 16: playWrapper((uint8_t*)_16, sizeof(_16)); break;
    case 17: playWrapper((uint8_t*)_35, sizeof(_35)); break;
    case 18: playWrapper((uint8_t*)_18, sizeof(_18)); break;
    case 19: playWrapper((uint8_t*)_19, sizeof(_19)); break;
    case 20: playWrapper((uint8_t*)_33, sizeof(_33)); break;
    case 21: playWrapper((uint8_t*)_21, sizeof(_21)); break;
    case 22: playWrapper((uint8_t*)_22, sizeof(_22)); break;
    case 23: playWrapper((uint8_t*)_23, sizeof(_23)); break;
    case 24: playWrapper((uint8_t*)_24, sizeof(_24)); break;
    case 25: playWrapper((uint8_t*)_25, sizeof(_25)); break;
    case 26: playWrapper((uint8_t*)_32, sizeof(_32)); break;
    case 27: playWrapper((uint8_t*)_37, sizeof(_37)); break;
    case 28: playWrapper((uint8_t*)_28, sizeof(_28)); break;
    case 29: playWrapper((uint8_t*)_29, sizeof(_29)); break;
    case 30: playWrapper((uint8_t*)_30, sizeof(_30)); break;
    case 31: playWrapper((uint8_t*)_42, sizeof(_42)); break;
    case 32: playWrapper((uint8_t*)_39, sizeof(_39)); break;
    default:
      break;
  }
}
