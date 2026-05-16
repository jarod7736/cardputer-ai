// Cardputer-AI M0 hardware probe — entry point.
//
// This file intentionally does very little; each probe lives in its own
// module so we can add/remove them independently. Probes are called in
// order in setup(), then loop() idles. We are not building an app yet.

#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  // Give USB-CDC time to enumerate on the host before we print.
  delay(1500);
  Serial.println();
  Serial.println("==== Cardputer-AI M0 hardware probe ====");
  Serial.println("(skeleton — no probes wired in yet)");
}

void loop() {
  delay(1000);
}
