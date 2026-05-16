#include "probe_soc.h"

#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <soc/rtc.h>

static const char* chip_model_name(esp_chip_model_t m) {
  switch (m) {
    case CHIP_ESP32:    return "ESP32";
    case CHIP_ESP32S2:  return "ESP32-S2";
    case CHIP_ESP32S3:  return "ESP32-S3";
    case CHIP_ESP32C3:  return "ESP32-C3";
    default:            return "(unknown)";
  }
}

void probe_soc(Stream& out) {
  out.println();
  out.println("-- SoC --");

  esp_chip_info_t info{};
  esp_chip_info(&info);
  out.printf("model:        %s\n", chip_model_name(info.model));
  out.printf("revision:     %u\n", info.revision);
  out.printf("cores:        %u\n", info.cores);
  out.printf("features:     %s%s%s%s%s\n",
             (info.features & CHIP_FEATURE_WIFI_BGN)    ? "WIFI " : "",
             (info.features & CHIP_FEATURE_BT)          ? "BT "   : "",
             (info.features & CHIP_FEATURE_BLE)         ? "BLE "  : "",
             (info.features & CHIP_FEATURE_EMB_FLASH)   ? "EMB-FLASH " : "",
             (info.features & CHIP_FEATURE_EMB_PSRAM)   ? "EMB-PSRAM " : "");

  out.printf("flash size:   %u bytes\n", (unsigned) ESP.getFlashChipSize());
  out.printf("crystal:      %u MHz\n", (unsigned) rtc_clk_xtal_freq_get());

  out.println();
  out.println("-- Memory --");
  out.printf("internal SRAM total: %u bytes\n",
             (unsigned) heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
  out.printf("internal SRAM free:  %u bytes\n",
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  out.printf("largest internal blk: %u bytes\n",
             (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

  // Use Arduino ESP class + heap_caps. ESP.getPsramSize() returns 0 if
  // PSRAM is not present OR the build was not configured for SPIRAM.
  size_t psram_total = ESP.getPsramSize();
  if (psram_total > 0) {
    out.printf("PSRAM:         PRESENT, %u bytes total\n", (unsigned) psram_total);
    out.printf("PSRAM free:    %u bytes\n", (unsigned) ESP.getFreePsram());
    out.printf("largest PSRAM blk: %u bytes\n",
               (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  } else {
    out.println("PSRAM:         not detected by ESP.getPsramSize()");
    out.println("               (this may mean: no PSRAM hardware, OR build flags");
    out.println("                missing BOARD_HAS_PSRAM / wrong memory_type).");
  }

  out.println();
}
