#include <Arduino.h>
#include "probe_soc.h"
#include "probe_display.h"
#include "probe_keyboard.h"
#include "probe_sd.h"

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("==== Cardputer-AI M0 hardware probe ====");

  probe_soc(Serial);
  probe_display(Serial);
  probe_keyboard(Serial, 15000);  // 15-second interactive window
  probe_sd(Serial);

  Serial.println("==== probes complete ====");
}

void loop() {
  delay(1000);
}
