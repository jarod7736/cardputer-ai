#include "profile_store.h"
#include <Preferences.h>

namespace profile_store {

static Preferences s_prefs;
static const char* kNs  = "cprox";
static const char* kKey = "profile_id";

void begin() {
  s_prefs.begin(kNs, /*readOnly=*/false);
}

String active_profile_id() {
  return s_prefs.getString(kKey, "");
}

void set_active_profile_id(const String& id) {
  s_prefs.putString(kKey, id);
}

}  // namespace profile_store
