#include <Arduino.h>
#include <NVSStorageAB.h>

// Current firmware structure (V2). New fields are ALWAYS appended at the end.
struct Settings {
  uint8_t controllerID;  // existed in V1
  uint32_t flags;        // existed in V1

  // Added in V2:
  uint16_t timeout;
  uint8_t brightness;
};

void defaults(Settings &s) {
  s.controllerID = 1;
  s.flags = 0;
  s.timeout = 1000;     // used automatically if loading an older, shorter V1
  s.brightness = 80;    // used automatically if loading an older, shorter V1
}

void migrate(uint16_t oldVersion, Settings &s) {
  if (oldVersion < 2) {
    // Optional semantic conversion goes here.
    // Appended fields already retain their defaults automatically.
    // Example: s.flags |= SOME_NEW_FLAG;
  }
}

Settings settings;
NVSStorageAB<Settings> storage("LCORx", "set", 2, defaults, migrate);

void setup() {
  Serial.begin(115200);
  delay(500);

  auto r = storage.loadAndUpgrade(settings);
  Serial.printf("Load/upgrade: %s\n", NVSStorageAB<Settings>::resultToString(r));
  Serial.printf("timeout=%u brightness=%u\n", settings.timeout, settings.brightness);
}

void loop() {}
