#pragma once
#include <Arduino.h>

namespace profile_store {

void   begin();
String active_profile_id();                       // empty if never set
void   set_active_profile_id(const String& id);

}  // namespace profile_store
