#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// Color palette
#define BUDDY_COLOR_CAT   0xC2A6  // Warm Claude Amber
#define BUDDY_COLOR_DIM   0x632C  // Dim gray/brown
#define BUDDY_COLOR_WHITE 0xFFFF
#define BUDDY_COLOR_YEL   0xFFE0
#define BUDDY_COLOR_CYAN  0x07FF
#define BUDDY_COLOR_GREEN 0x07E0
#define BUDDY_COLOR_MAG   0xF81F

class BuddyCat {
public:
  void drawSleep(Arduino_GFX *gfx, uint32_t t) {
    static const char* const LOAF[5]    = { "            ", "            ", "   .-..-.   ", "  ( -.- )   ", "  `------`~ " };
    static const char* const BREATHE[5] = { "            ", "            ", "   .-..-.   ", "  ( -.- )_  ", " `~------'~ " };
    static const char* const CURL[5]    = { "            ", "            ", "   .-/\\.    ", "  (  ..  )) ", "  `~~~~~~`  " };
    static const char* const CURL_TW[5] = { "            ", "            ", "   .-/\\.    ", "  (  ..  )) ", "  `~~~~~~`~ " };
    static const char* const PURR[5]    = { "            ", "            ", "   .-..-.   ", "  ( u.u )   ", " `~------'~ " };
    static const char* const DREAM[5]   = { "            ", "            ", "   .-..-.   ", "  ( o.o )   ", "  `------`  " };

    const char* const* P[6] = { LOAF, BREATHE, LOAF, PURR, CURL, CURL_TW };
    static const uint8_t SEQ[] = {
      0,1,0,1,0,1, 3,3,0,1, 4,5,4,5,4,5, 2,2, 0,1,0,1, 5,5,4,4
    };
    uint8_t beat = (t / 4) % sizeof(SEQ);
    drawSprite(gfx, P[SEQ[beat]], 5, 0, BUDDY_COLOR_CAT);

    // Drifting Zzz particles
    int p1 = (t)     % 10;
    int p2 = (t + 4) % 10;
    int p3 = (t + 7) % 10;
    gfx->setTextColor(BUDDY_COLOR_DIM);
    gfx->setCursor(54 + p1, 26 - p1 * 2);
    gfx->print("z");
    gfx->setTextColor(BUDDY_COLOR_WHITE);
    gfx->setCursor(60 + p2, 20 - p2 * 2);
    gfx->print("Z");
    gfx->setTextColor(BUDDY_COLOR_CYAN);
    gfx->setCursor(48 + p3 / 2, 16 - p3);
    gfx->print("z");
  }

  void drawIdle(Arduino_GFX *gfx, uint32_t t) {
    static const char* const REST[5]    = { "            ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )  ", "  (\")_(\")   " };
    static const char* const LOOK_L[5]  = { "            ", "   /\\_/\\    ", "  (o    o ) ", "  (  w   )  ", "  (\")_(\")   " };
    static const char* const LOOK_R[5]  = { "            ", "   /\\_/\\    ", "  ( o    o) ", "  (  w   )  ", "  (\")_(\")   " };
    static const char* const BLINK[5]   = { "            ", "   /\\_/\\    ", "  ( -   - ) ", "  (  w   )  ", "  (\")_(\")   " };
    static const char* const SLOW_BL[5] = { "            ", "   /\\-/\\    ", "  ( _   _ ) ", "  (  w   )  ", "  (\")_(\")   " };
    static const char* const EAR_L[5]   = { "            ", "   <\\_/\\    ", "  ( o   o ) ", "  (  w   )  ", "  (\")_(\")   " };
    static const char* const EAR_R[5]   = { "            ", "   /\\_/>    ", "  ( o   o ) ", "  (  w   )  ", "  (\")_(\")   " };
    static const char* const TAIL_L[5]  = { "            ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )  ", "  (\")_(\")~  " };
    static const char* const TAIL_R[5]  = { "            ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )  ", " ~(\")_(\")   " };
    static const char* const GROOM[5]   = { "            ", "   /\\_/\\    ", "  ( ^   ^ ) ", "  (  P   )  ", "  (\")_(\")   " };

    const char* const* P[10] = { REST, LOOK_L, LOOK_R, BLINK, SLOW_BL, EAR_L, EAR_R, TAIL_L, TAIL_R, GROOM };
    static const uint8_t SEQ[] = {
      0,0,0,3,0,1,0,2,0, 7,8,7,8,7, 0,5,0,6,0, 4,4,0, 9,9,9,0, 0,3,0, 8,7,8,7, 0,0,4,0
    };
    uint8_t beat = (t / 4) % sizeof(SEQ);
    drawSprite(gfx, P[SEQ[beat]], 5, 0, BUDDY_COLOR_CAT);
  }

  void drawBusy(Arduino_GFX *gfx, uint32_t t) {
    static const char* const PAW_UP[5]  = { "      .     ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )/ ", "  (\")_(\")   " };
    static const char* const PAW_TAP[5] = { "    .       ", "   /\\_/\\    ", "  ( o   o ) ", "  (  w   )_ ", "  (\")_(\")   " };
    static const char* const STARE[5]   = { "            ", "   /\\_/\\    ", "  ( O   O ) ", "  (  w   )  ", "  (\")_(\")   " };
    static const char* const NUDGE[5]   = { "    o       ", "   /\\_/\\    ", "  ( o   o ) ", "  ( -w   )  ", "  (\")_(\")   " };
    static const char* const SHOVE[5]   = { "  o         ", "   /\\_/\\    ", "  ( o   o ) ", "  (-w    )  ", "  (\")_(\")   " };
    static const char* const SMUG[5]    = { "            ", "   /\\_/\\    ", "  ( -   - ) ", "  (  w   )  ", "  (\")_(\")   " };

    const char* const* P[6] = { PAW_UP, PAW_TAP, STARE, NUDGE, SHOVE, SMUG };
    static const uint8_t SEQ[] = {
      2,2,2, 0,1,0,1, 3,4,3,4, 5,5, 2,2, 0,1,0,1, 5,2
    };
    uint8_t beat = (t / 3) % sizeof(SEQ);
    drawSprite(gfx, P[SEQ[beat]], 5, 0, BUDDY_COLOR_CAT);

    // Working dots
    static const char* const DOTS[] = { ".  ", ".. ", "...", " ..", "  .", "   " };
    gfx->setTextColor(BUDDY_COLOR_YEL);
    gfx->setCursor(56, 44);
    gfx->print(DOTS[t % 6]);
  }

  void drawCelebrate(Arduino_GFX *gfx, uint32_t t) {
    static const char* const CROUCH[5]  = { "            ", "   /\\_/\\    ", "  ( ^   ^ ) ", "  (  W   )  ", " /(\")_(\")\\  " };
    static const char* const JUMP[5]    = { "  \\^   ^/   ", "    /\\_/\\   ", "  ( ^   ^ ) ", "  (  W   )  ", "  (\")_(\")   " };
    static const char* const PEAK[5]    = { "  \\^   ^/   ", "    /\\_/\\   ", "  ( * * * ) ", "  (  W   )  ", "  (\")_(\")~  " };
    static const char* const SPIN_L[5]  = { "            ", "   /\\_/\\    ", "  ( <   < ) ", "  (  W   ) /", " ~(\")_(\")   " };
    static const char* const SPIN_R[5]  = { "            ", "   /\\_/\\    ", "  ( >   > ) ", " \\(  W   )  ", "  (\")_(\")~  " };
    static const char* const POSE[5]    = { "    \\o/     ", "   /\\_/\\    ", "  ( ^   ^ ) ", " /(  W   )\\ ", "  (\")_(\")   " };

    const char* const* P[6] = { CROUCH, JUMP, PEAK, SPIN_L, SPIN_R, POSE };
    static const uint8_t SEQ[] = { 0,1,2,1,0, 3,4,3,4, 0,1,2,1,0, 5,5 };
    static const int8_t Y_SHIFT[] = { 0,-2,-4,-2,0, 0,0,0,0, 0,-2,-4,-2,0, 0,0 };
    uint8_t beat = (t / 2) % sizeof(SEQ);
    drawSprite(gfx, P[SEQ[beat]], 5, Y_SHIFT[beat], BUDDY_COLOR_CAT);

    // Confetti rain
    static const uint16_t cols[] = { BUDDY_COLOR_YEL, BUDDY_COLOR_MAG, BUDDY_COLOR_CYAN, BUDDY_COLOR_WHITE, BUDDY_COLOR_GREEN };
    for (int i = 0; i < 4; i++) {
      int phase = (t * 2 + i * 9) % 20;
      int x = 6 + i * 16;
      int y = 6 + phase;
      if (y < 65) {
        gfx->setTextColor(cols[i % 5]);
        gfx->setCursor(x, y);
        gfx->print((i + (int)(t/2)) & 1 ? "*" : ".");
      }
    }
  }

private:
  void drawSprite(Arduino_GFX *gfx, const char* const* lines, uint8_t nLines, int yOffset, uint16_t color) {
    gfx->setTextSize(1);
    const int xBase = 2;
    const int yBase = 18;
    for (uint8_t i = 0; i < nLines; i++) {
      gfx->setTextColor(color, 0x0000);
      gfx->setCursor(xBase, yBase + yOffset + i * 8);
      gfx->print(lines[i]);
    }
  }
};

extern BuddyCat catBuddy;
