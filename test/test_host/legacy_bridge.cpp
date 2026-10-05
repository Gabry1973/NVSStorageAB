// Compiles the original header-only implementation (commit 95a10fc, based on
// Arduino Preferences) over the same fake NVS, to check that the current
// library reads and writes the very same on-flash format.
#include <Arduino.h>
#include <Preferences.h>
#include <stddef.h>
#include <type_traits>

#include "test_types.h"

namespace legacy {
#include "legacy/NVSStorageAB_header_only.h"
}

typedef legacy::NVSStorageAB<V2> LegacyV2;

int legacySaveV2(const char* ns, const char* key, uint16_t version, const V2& d) {
    LegacyV2 storage(ns, key, version, defV2);
    return static_cast<int>(storage.save(d));
}

int legacyLoadV2(const char* ns, const char* key, uint16_t version, V2& d) {
    LegacyV2 storage(ns, key, version, defV2);
    return static_cast<int>(storage.load(d));
}
