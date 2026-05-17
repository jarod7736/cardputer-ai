#pragma once

#include <Arduino.h>

// First-boot provisioning: if an SD bundle is present and NVS is empty,
// commit the bundle to NVS and wipe the SD files. Caller restarts the
// device on kCommitted so it re-enters main with NVS as source-of-truth.

namespace provisioning {

enum class Result : uint8_t {
  kNoBundle,            // SD absent or no bundle files
  kAlreadyProvisioned,  // SD present BUT NVS already populated → do not overwrite
  kCommitted,           // SD bundle written to NVS, SD wiped; caller should reboot
  kFailed,              // SD bundle present but invalid OR write failed
};

struct Outcome {
  Result result = Result::kNoBundle;
  String detail;        // human-readable; useful on failure / already-provisioned
};

// Single-shot orchestration. Reads SD (mounting/unmounting internally),
// validates the bundle, writes all four NVS namespaces, then wipes SD.
// Returns a structured outcome. Caller decides what to do (typically:
// kCommitted → restart, kFailed → display + halt, others → continue).
Outcome run(Stream& log);

}  // namespace provisioning
