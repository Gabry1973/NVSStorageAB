// Minimal Arduino-ESP32 Preferences over the fake NVS (legacy header only).
#pragma once
#include "nvs.h"
class Preferences {
public:
    bool begin(const char* ns, bool ro) { _ok = nvs_open(ns, ro ? NVS_READONLY : NVS_READWRITE, &_h) == ESP_OK; return _ok; }
    void end() { if (_ok) nvs_close(_h); _ok = false; }
    size_t getBytesLength(const char* k) { size_t l = 0; return nvs_get_blob(_h, k, nullptr, &l) == ESP_OK ? l : 0; }
    size_t getBytes(const char* k, void* b, size_t m) { size_t l = m; return nvs_get_blob(_h, k, b, &l) == ESP_OK ? l : 0; }
    size_t putBytes(const char* k, const void* b, size_t l) { return nvs_set_blob(_h, k, b, l) == ESP_OK ? l : 0; }
    bool isKey(const char* k) { size_t l = 0; return nvs_get_blob(_h, k, nullptr, &l) == ESP_OK; }
    bool remove(const char* k) { return nvs_erase_key(_h, k) == ESP_OK; }
private:
    nvs_handle_t _h = 0; bool _ok = false;
};
