// In-memory fake of the ESP-IDF NVS API, for host tests only.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <map>
#include <set>
#include <string>
#include <vector>

typedef int esp_err_t;
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
#define ESP_ERR_NVS_READ_ONLY 0x1107

namespace fakenvs {
typedef std::map<std::string, std::vector<uint8_t>> Ns;
extern std::map<std::string, Ns> store;
extern std::map<nvs_handle_t, std::pair<std::string, bool>> handles;
extern std::set<std::string> failReadKeys;   // nvs_get_blob fails for these keys
extern bool failWrite;
extern int writes, opens, openHandles;
void reset();
}

esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* out);
void nvs_close(nvs_handle_t h);
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len);
esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* v, size_t len);
esp_err_t nvs_erase_key(nvs_handle_t h, const char* key);
esp_err_t nvs_commit(nvs_handle_t h);
