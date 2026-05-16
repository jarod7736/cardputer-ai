#include "probe_keyboard.h"

#include <Wire.h>

static int i2c_scan(Stream& out) {
  out.println("i2c scan:");
  // Cardputer-ADV uses the default Arduino Wire instance for its internal
  // I2C bus by default. If the TCA8418 sits on Wire1 we'll find that out
  // when nothing answers on Wire.
  Wire.begin();
  int found = 0;
  for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      out.printf("  device @ 0x%02X\n", addr);
      if (addr == 0x34) {
        out.println("    ^ TCA8418 default address — keyboard controller");
      }
      ++found;
    }
  }
  if (found == 0) {
    out.println("  (no devices responded on Wire)");
  }
  return found;
}

int probe_keyboard(Stream& out, uint32_t /*duration_ms*/) {
  out.println("-- Keyboard --");

  // M0 scope: confirm the keyboard controller is on the I2C bus.
  // Reading raw key events would require either:
  //   a) building against the M5Cardputer-specific board profile (which
  //      may not yet support the ADV variant cleanly), or
  //   b) writing a direct TCA8418 driver.
  // Both are deferred to M1. For now, the I2C scan answers the only
  // question M0 cares about: "is the keyboard chip wired up?"
  int devices = i2c_scan(out);
  out.printf("keyboard: deferred typed-key capture to M1; i2c devices found = %d\n",
             devices);
  out.println();
  return 0;
}
