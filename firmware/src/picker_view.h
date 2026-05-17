#pragma once
#include <Arduino.h>
#include <vector>
#include "proxy_api.h"

namespace picker_view {

// Render the picker over the existing chat_view canvas. Returns true if
// the user selected a profile; selected_out is set to its id. Returns
// false if the user cancelled (Esc or 30s of inactivity).
bool run(const std::vector<proxy_api::Profile>& profiles,
         const String& current_id,
         String& selected_out);

}  // namespace picker_view
