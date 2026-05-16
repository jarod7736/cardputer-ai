#pragma once
#include <Arduino.h>

// Initialize the Cardputer-ADV LCD via M5Unified and draw a known test
// pattern: solid red/green/blue/white corners and the text "M0 DISPLAY OK"
// centered. Logs progress to `out`.
//
// Returns true if M5Unified reports the display is present and the test
// pattern was issued without error.
bool probe_display(Stream& out);
