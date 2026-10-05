#include "nvs.h"
#include <string.h>
namespace fakenvs {
std::map<std::string, Ns> store;
std::map<nvs_handle_t, std::pair<std::string, bool>> handles;
std::set<std::string> failReadKeys;
bool failWrite = false;
int writes = 0, opens = 0, openHandles = 0;
static nvs_handle_t next = 1;
void reset() { store.clear(); handles.clear(); failReadKeys.clear(); failWrite = false; writes = opens = openHandles = 0; }
}
using namespace fakenvs;
esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* out) {
    ++opens;
    if (!store.count(ns)) {
        if (mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
        store[ns];
    }
    *out = next++;
    handles[*out] = std::make_pair(std::string(ns), mode == NVS_READWRITE);
    ++openHandles;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { if (handles.erase(h)) --openHandles; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len) {
    if (!handles.count(h)) return ESP_FAIL;
    if (failReadKeys.count(key)) return ESP_FAIL;
    Ns& ns = store[handles[h].first];
    if (!ns.count(key)) return ESP_ERR_NVS_NOT_FOUND;
    std::vector<uint8_t>& v = ns[key];
    if (!out) { *len = v.size(); return ESP_OK; }
    if (*len < v.size()) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out, v.data(), v.size()); *len = v.size();
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* v, size_t len) {
    if (!handles.count(h)) return ESP_FAIL;
    if (!handles[h].second) return ESP_ERR_NVS_READ_ONLY;
    if (failWrite) return ESP_FAIL;
    ++writes;
    const uint8_t* p = static_cast<const uint8_t*>(v);
    store[handles[h].first][key] = std::vector<uint8_t>(p, p + len);
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char* key) {
    if (!handles.count(h) || !handles[h].second) return ESP_FAIL;
    return store[handles[h].first].erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_commit(nvs_handle_t h) { return handles.count(h) ? ESP_OK : ESP_FAIL; }
