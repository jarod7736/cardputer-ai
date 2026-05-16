#pragma once
#include <Arduino.h>

// Print SoC identity, flash size, internal SRAM stats, and PSRAM presence
// + size to the given Stream. Safe to call from setup() after Serial.begin.
void probe_soc(Stream& out);
