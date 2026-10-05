#include <Arduino.h>
#include <NVSStorageAB.h>

// Adapt these to the real hardware.
constexpr uint8_t POWER_FAIL_PIN = 4;
constexpr uint8_t POWER_FAIL_ACTIVE = LOW;

struct LCORxData {
  uint32_t totalRides;
  uint32_t totalHits;
  uint32_t totalPowerFails;
};

void defaultData(LCORxData &d) {
  d.totalRides = 0;
  d.totalHits = 0;
  d.totalPowerFails = 0;
}

LCORxData data;
NVSStorageAB<LCORxData> dataStorage("LCORx", "data", 1, defaultData);

volatile bool powerFailIRQ = false;
bool powerFailHandled = false;

// Declared once with IRAM_ATTR and defined without it: the .ino-to-.cpp
// conversion (PlatformIO) would otherwise emit a second IRAM_ATTR prototype
// with a different section name and trigger a -Wattributes warning.
void IRAM_ATTR onPowerFail();

void onPowerFail() {
  // Never write NVS from an ISR.
  powerFailIRQ = true;
}

void saveOnPowerFail() {
  ++data.totalPowerFails;
  auto r = dataStorage.save(data);
  Serial.printf("Power-fail save: %s\n",
                NVSStorageAB<LCORxData>::resultToString(r));
}

void setup() {
  Serial.begin(115200);
  pinMode(POWER_FAIL_PIN, INPUT_PULLUP);

  auto r = dataStorage.loadAndUpgrade(data);
  Serial.printf("Load: %s\n", NVSStorageAB<LCORxData>::resultToString(r));

  attachInterrupt(digitalPinToInterrupt(POWER_FAIL_PIN), onPowerFail, CHANGE);
}

void loop() {
  // Example runtime changes kept only in RAM.
  // data.totalHits++;
  // data.totalRides++;

  if (powerFailIRQ) {
    powerFailIRQ = false;

    if (digitalRead(POWER_FAIL_PIN) == POWER_FAIL_ACTIVE && !powerFailHandled) {
      powerFailHandled = true;
      saveOnPowerFail();       // Save immediately while hold-up energy is high.
    }
  }

  // If main power returned, allow a future independent power-fail event.
  if (powerFailHandled && digitalRead(POWER_FAIL_PIN) != POWER_FAIL_ACTIVE) {
    powerFailHandled = false;
  }
}
