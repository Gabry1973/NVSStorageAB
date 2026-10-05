#pragma once

#include <stdint.h>

// V2 = V1 + fields appended at the end.
struct V1 { uint8_t id; uint32_t flags; };
struct V2 { uint8_t id; uint32_t flags; uint16_t timeout; uint8_t brightness; };

void defV1(V1& s);
void defV2(V2& s);

// Original header-only implementation (legacy/), see legacy_bridge.cpp.
// Return the legacy Result as int (0 == OK).
int legacySaveV2(const char* ns, const char* key, uint16_t version, const V2& d);
int legacyLoadV2(const char* ns, const char* key, uint16_t version, V2& d);
