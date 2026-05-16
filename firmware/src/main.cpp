#include <Arduino.h>
#include "probe_soc.h"

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("==== Cardputer-AI M0 hardware probe ====");

  probe_soc(Serial);

  Serial.println("==== probes complete (more to come) ====");
}

void loop() {
  delay(1000);
}
