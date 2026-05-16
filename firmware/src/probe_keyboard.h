#pragma once
#include <Arduino.h>

// Initialize the Cardputer-ADV keyboard and run an interactive probe for
// `duration_ms` milliseconds. Prints each detected keypress to `out`. Also
// performs a one-shot I²C bus scan and logs every responding address.
//
// Returns the number of keypresses captured.
int probe_keyboard(Stream& out, uint32_t duration_ms);
