#include "probe_display.h"

#include <M5Unified.h>

bool probe_display(Stream& out) {
  out.println("-- Display --");

  auto cfg = M5.config();
  M5.begin(cfg);

  // M5Unified picks the board variant from build flags + runtime detection.
  // Print what it thinks we are running on so we can confirm ADV support.
  out.printf("M5Unified board id: %d\n", (int) M5.getBoard());

  if (M5.Display.width() == 0 || M5.Display.height() == 0) {
    out.println("display: NOT INITIALIZED (M5Unified reported 0x0)");
    out.println("display: M5Unified may not yet support Cardputer-ADV.");
    out.println("display: see fallback notes in plan Task 3 step 5");
    return false;
  }

  out.printf("display: %d x %d, color depth %d\n",
             M5.Display.width(), M5.Display.height(), M5.Display.getColorDepth());

  // Test pattern
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.fillRect(0, 0, 20, 20, TFT_RED);
  M5.Display.fillRect(M5.Display.width() - 20, 0, 20, 20, TFT_GREEN);
  M5.Display.fillRect(0, M5.Display.height() - 20, 20, 20, TFT_BLUE);
  M5.Display.fillRect(M5.Display.width() - 20, M5.Display.height() - 20,
                      20, 20, TFT_WHITE);

  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString("M0 DISPLAY OK",
                        M5.Display.width() / 2, M5.Display.height() / 2);

  out.println("display: test pattern drawn");
  out.println();
  return true;
}
