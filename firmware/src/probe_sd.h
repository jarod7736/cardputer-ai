#pragma once
#include <Arduino.h>

// Mount the microSD card via M5Unified's SD pin map, list the root
// directory, then write and read back a small canary file. Prints
// everything to `out`. Returns true on full round-trip success.
bool probe_sd(Stream& out);
