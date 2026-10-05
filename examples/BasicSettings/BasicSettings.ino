#include <Arduino.h>
#include <NVSStorageAB.h>

struct LCORxSettings {
  uint64_t ChipID;
  uint8_t  ControllerID;
  char     companyID[8];
  uint32_t flags;
  uint16_t timeout;
  uint8_t  brightness;
};

void defaultSettings(LCORxSettings &s) {
  // NVSStorageAB has already zeroed the whole struct, including padding.
  s.ChipID = 0;
  s.ControllerID = 1;
  strlcpy(s.companyID, "DEFAULT", sizeof(s.companyID));
  s.flags = 0;
  s.timeout = 1000;
  s.brightness = 80;
}

LCORxSettings settings;
NVSStorageAB<LCORxSettings> settingsStorage("LCORx", "set", 1, defaultSettings);

void setup() {
  Serial.begin(115200);
  delay(500);

  auto r = settingsStorage.loadAndUpgrade(settings);
  Serial.printf("Load: %s\n", NVSStorageAB<LCORxSettings>::resultToString(r));

  Serial.printf("ControllerID: %u\n", settings.ControllerID);
  Serial.printf("Brightness: %u\n", settings.brightness);

  // Example modification.
  settings.brightness = 90;
  r = settingsStorage.save(settings);
  Serial.printf("Save #1: %s\n", NVSStorageAB<LCORxSettings>::resultToString(r));

  // Same data: anti-wear prevents another flash write.
  r = settingsStorage.save(settings);
  Serial.printf("Save #2: %s (expected UNCHANGED)\n",
                NVSStorageAB<LCORxSettings>::resultToString(r));
}

void loop() {}
