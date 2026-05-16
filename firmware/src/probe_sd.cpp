#include "probe_sd.h"

#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

static constexpr const char* kCanaryPath = "/m0_canary.txt";
static constexpr const char* kCanaryBody = "cardputer-ai m0 probe canary";

bool probe_sd(Stream& out) {
  out.println("-- microSD --");

  // M5Unified exposes the SD SPI pins for each board. Use those rather
  // than guessing — if the API doesn't expose them, fall back to the
  // M5Stack-published pin map for Cardputer-ADV (confirm in M1).
  SPIClass spi(HSPI);
  int sck  = M5.getPin(m5::pin_name_t::sd_spi_sclk);
  int miso = M5.getPin(m5::pin_name_t::sd_spi_miso);
  int mosi = M5.getPin(m5::pin_name_t::sd_spi_mosi);
  int cs   = M5.getPin(m5::pin_name_t::sd_spi_ss);
  out.printf("sd pins: sck=%d miso=%d mosi=%d cs=%d\n", sck, miso, mosi, cs);

  if (sck < 0 || miso < 0 || mosi < 0 || cs < 0) {
    out.println("sd: M5Unified did not expose SD pins for this board.");
    out.println("sd: cannot mount in M0 — defer to M1 with explicit pin map.");
    return false;
  }

  spi.begin(sck, miso, mosi, cs);
  if (!SD.begin(cs, spi, 20000000)) {
    out.println("sd: mount FAILED");
    return false;
  }

  uint64_t size_mb = SD.cardSize() / (1024ULL * 1024ULL);
  out.printf("sd: mounted, card size = %llu MB\n", (unsigned long long) size_mb);

  out.println("sd: root listing:");
  File root = SD.open("/");
  if (root) {
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      out.printf("  %s  (%lu bytes)\n", f.name(), (unsigned long) f.size());
    }
    root.close();
  }

  // Write canary
  File w = SD.open(kCanaryPath, FILE_WRITE);
  if (!w) { out.println("sd: open for write FAILED"); SD.end(); return false; }
  w.print(kCanaryBody);
  w.close();

  // Read it back
  File r = SD.open(kCanaryPath, FILE_READ);
  if (!r) { out.println("sd: open for read FAILED"); SD.end(); return false; }
  String got = r.readString();
  r.close();

  bool ok = (got == kCanaryBody);
  out.printf("sd: canary round-trip %s (\"%s\")\n",
             ok ? "OK" : "MISMATCH", got.c_str());

  // Clean up canary so we don't litter the card
  SD.remove(kCanaryPath);

  SD.end();
  out.println();
  return ok;
}
