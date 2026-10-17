#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// 16-bit 565 Color definitions
#define EYE_COLOR_GREEN  0x07E0  // Bright Robot Green
#define EYE_COLOR_YELLOW 0xFFE0  // Bright Yellow
#define EYE_COLOR_PURPLE 0xF81F  // Vivid Purple / Magenta
#define EYE_COLOR_BLUE   0x3C1F  // Soft Calm Blue
#define EYE_COLOR_WHITE  0xFFFF
#define EYE_COLOR_CYAN   0x07FF
#define EYE_COLOR_BLACK  0x0000

class RobotEyes {
public:
  // 1. 待命 (IDLE): 绿色圆眼，黑瞳孔，眨眼，左右看
  void drawIdle(Arduino_Canvas *gfx, uint32_t tick) {
    // 左右张望偏移计算 (平缓张望)
    int phase = (tick / 20) % 8;
    int dx = 0;
    int dy = 0;
    if (phase == 2) { dx = -6; }       // 看左
    else if (phase == 3) { dx = -7; }  // 维持看左
    else if (phase == 5) { dx = 6; }   // 看右
    else if (phase == 6) { dx = 7; }   // 维持看右
    else if (phase == 7) { dy = -2; }  // 稍稍往上看

    // 眨眼动画周期 (每隔约 60 tick 眨眼一次，持续 6 tick)
    int blinkPhase = tick % 65;
    int eyeH = 54;
    if (blinkPhase >= 60) {
      int step = blinkPhase - 60;
      if (step == 0 || step == 4) eyeH = 34;
      else if (step == 1 || step == 3) eyeH = 16;
      else if (step == 2) eyeH = 4; // 闭眼成细缝
    }

    int eyeY = 40 - eyeH / 2;

    // 左眼与右眼 (大号圆角胶囊眼)
    gfx->fillRoundRect(20, eyeY, 50, eyeH, 18, EYE_COLOR_GREEN);
    gfx->fillRoundRect(90, eyeY, 50, eyeH, 18, EYE_COLOR_GREEN);

    // 只有睁眼足够大时才绘制黑色瞳孔与高光
    if (eyeH > 20) {
      int pupilH = (eyeH * 28) / 54;
      int pupilW = 20;
      int pupilY = 40 - pupilH / 2 + dy;

      // 左眼瞳孔
      gfx->fillRoundRect(20 + 15 + dx, pupilY, pupilW, pupilH, 9, EYE_COLOR_BLACK);
      // 右眼瞳孔
      gfx->fillRoundRect(90 + 15 + dx, pupilY, pupilW, pupilH, 9, EYE_COLOR_BLACK);

      // 眼神高光小圆点 (右上角灵动反光)
      gfx->fillCircle(20 + 15 + dx + 13, pupilY + 7, 2, EYE_COLOR_WHITE);
      gfx->fillCircle(90 + 15 + dx + 13, pupilY + 7, 2, EYE_COLOR_WHITE);
    }
  }

  // 2. 干活 (BUSY): 黄色，上沿压低专注，轻微抖动，右上角大号数字显示「已完成/总数」
  void drawBusy(Arduino_Canvas *gfx, uint32_t tick, const String& progress) {
    // 高频轻微抖动
    int jx = (tick & 1) ? 1 : -1;
    int jy = ((tick >> 1) & 1) ? 1 : 0;

    int lx = 20 + jx;
    int rx = 90 + jx;
    int y = 14 + jy;
    int w = 50;
    int h = 52;

    // 绘制黄色眼睛底
    gfx->fillRoundRect(lx, y, w, h, 16, EYE_COLOR_YELLOW);
    gfx->fillRoundRect(rx, y, w, h, 16, EYE_COLOR_YELLOW);

    // 黑色专注瞳孔
    gfx->fillRoundRect(lx + 15, y + 14, 20, 26, 8, EYE_COLOR_BLACK);
    gfx->fillRoundRect(rx + 15, y + 14, 20, 26, 8, EYE_COLOR_BLACK);

    // 眼神聚精会神小高光
    gfx->fillCircle(lx + 27, y + 21, 2, EYE_COLOR_WHITE);
    gfx->fillCircle(rx + 27, y + 21, 2, EYE_COLOR_WHITE);

    // 专注压低上沿 (上眼睑斜切，眉头下压形成认真专注神态)
    // 左眼：向内侧下压 (右上倾斜切掉三角)
    gfx->fillTriangle(lx, y - 2, lx + w + 2, y - 2, lx + w + 2, y + 18, EYE_COLOR_BLACK);
    // 右眼：向内侧下压 (左上倾斜切掉三角)
    gfx->fillTriangle(rx - 2, y - 2, rx + w + 2, y - 2, rx - 2, y + 18, EYE_COLOR_BLACK);

    // 右上角大号文字显示「已完成/总数」如 3/5 (屏幕唯一文字，若无清单则不显示)
    if (progress.length() > 0 && progress != "null") {
      int size = (progress.length() <= 4) ? 2 : 1;
      gfx->setTextSize(size);
      gfx->setTextColor(EYE_COLOR_WHITE);
      int charW = size * 6;
      int textW = progress.length() * charW;
      int textX = 160 - textW - 3;
      gfx->setCursor(textX, 3);
      gfx->print(progress);
    }
  }

  // 3. 完成 (DONE): 紫色眯眼笑（两道向上弯的弧线），旁边小星星闪
  void drawCelebrate(Arduino_Canvas *gfx, uint32_t tick) {
    // 绘制左右两道向上弯的圆弧笑眼 (⌒ ⌒)
    drawUpwardArc(gfx, 45, 46, 22, 14, 5, EYE_COLOR_PURPLE);
    drawUpwardArc(gfx, 115, 46, 22, 14, 5, EYE_COLOR_PURPLE);

    // 闪烁的小星星 (4角星)
    static const int starPos[4][2] = {
      { 14, 18 }, { 80, 16 }, { 146, 20 }, { 80, 56 }
    };
    uint16_t starColors[4] = { EYE_COLOR_WHITE, EYE_COLOR_CYAN, EYE_COLOR_YELLOW, EYE_COLOR_PURPLE };

    for (int i = 0; i < 4; i++) {
      int phase = (tick + i * 2) % 6;
      int r = (phase <= 2) ? 4 : 2;
      drawStar(gfx, starPos[i][0], starPos[i][1], r, starColors[(i + tick / 3) % 4]);
    }
  }

  // 4. 睡觉 (SLEEP): 暗蓝色闭眼（两道向下弯的弧线），旁边飘 z 和 Z
  void drawSleep(Arduino_Canvas *gfx, uint32_t tick) {
    // 绘制左右两道向下弯的平缓闭眼弧线 (⌣ ⌣)
    drawDownwardArc(gfx, 45, 36, 22, 12, 4, EYE_COLOR_BLUE);
    drawDownwardArc(gfx, 115, 36, 22, 12, 4, EYE_COLOR_BLUE);

    // 右侧飘动的 z 和 Z 粒子
    int p1 = (tick) % 18;
    int p2 = (tick + 6) % 18;
    int p3 = (tick + 12) % 18;

    gfx->setTextSize(1);
    gfx->setTextColor(0x52AA); // 暗淡灰青
    gfx->setCursor(132 + p1 / 2, 46 - p1 * 2);
    gfx->print("z");

    gfx->setTextSize(2);
    gfx->setTextColor(EYE_COLOR_WHITE); // 亮白大 Z
    gfx->setCursor(138 + p2 / 2, 42 - p2 * 2);
    gfx->print("Z");

    gfx->setTextSize(1);
    gfx->setTextColor(EYE_COLOR_CYAN); // 冰蓝小 z
    gfx->setCursor(144 + p3 / 3, 38 - p3 * 2);
    gfx->print("z");
  }

private:
  // 绘制向上拱起弧线 (⌒ ⌒，笑眼)
  void drawUpwardArc(Arduino_Canvas *gfx, int cx, int cy, int rx, int ry, int thickness, uint16_t color) {
    for (int x = -rx; x <= rx; x++) {
      // 抛物线方程 y = cy - ry * (1 - (x/rx)^2)
      float factor = 1.0f - ((float)(x * x) / (float)(rx * rx));
      if (factor < 0) factor = 0;
      int yTop = cy - (int)(ry * factor);
      for (int t = 0; t < thickness; t++) {
        gfx->drawPixel(cx + x, yTop + t, color);
      }
    }
  }

  // 绘制向下弯弧线 (⌣ ⌣，睡眼)
  void drawDownwardArc(Arduino_Canvas *gfx, int cx, int cy, int rx, int ry, int thickness, uint16_t color) {
    for (int x = -rx; x <= rx; x++) {
      // 抛物线方程 y = cy + ry * (1 - (x/rx)^2)
      float factor = 1.0f - ((float)(x * x) / (float)(rx * rx));
      if (factor < 0) factor = 0;
      int yBot = cy + (int)(ry * factor);
      for (int t = 0; t < thickness; t++) {
        gfx->drawPixel(cx + x, yBot - t, color);
      }
    }
  }

  // 绘制四角闪烁星星
  void drawStar(Arduino_Canvas *gfx, int x, int y, int r, uint16_t color) {
    gfx->drawLine(x - r, y, x + r, y, color);
    gfx->drawLine(x, y - r, x, y + r, color);
    gfx->drawPixel(x, y, EYE_COLOR_WHITE);
  }
};

extern RobotEyes robotEyes;
