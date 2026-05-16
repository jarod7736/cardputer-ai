#include <Arduino.h>
#include "probe_soc.h"
#include "probe_display.h"
#include "probe_keyboard.h"
#include "probe_sd.h"

static void run_all_probes() {
  Serial.println();
  Serial.println("==== Cardputer-AI M0 hardware probe ====");
  probe_soc(Serial);
  probe_display(Serial);
  probe_keyboard(Serial, 5000);  // shorter window since we re-run on demand
  probe_sd(Serial);
  Serial.println("==== probes complete — press any key to re-run ====");
}

void setup() {
  Serial.begin(115200);
  // Wait for the host monitor to open the USB-CDC endpoint. With native
  // USB on ESP32-S3 this is needed so we don't print into a void after
  // a reset that disconnects+re-enumerates the port.
  uint32_t deadline = millis() + 10000;
  while (!Serial && millis() < deadline) {
    delay(50);
  }
  delay(500);  // host buffer settle

  run_all_probes();
}

void loop() {
  // Re-run probes on any input from the monitor, so a missed boot is
  // never fatal — just press a key in pio device monitor.
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    run_all_probes();
  }
  delay(50);
}
