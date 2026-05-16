// ESP-IDF toolchain verification for Cardputer-AI.
//
// Goal: ensure ESP-IDF 5.x + mbedTLS + lwIP compile cleanly for the
// ESP32-S3 target so M1 can pull in esp_wireguard without surprise.

#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"
#include "mbedtls/version.h"
#include "lwip/init.h"

static const char* TAG = "idf-probe";

void app_main(void) {
    ESP_LOGI(TAG, "ESP-IDF probe booted on %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "mbedTLS version: %s", MBEDTLS_VERSION_STRING);
    ESP_LOGI(TAG, "lwIP version: %s", LWIP_VERSION_STRING);
}
