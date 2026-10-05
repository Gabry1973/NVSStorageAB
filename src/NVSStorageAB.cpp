#include "NVSStorageAB.h"

#include <nvs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

using Result = NVSStorageABCore::Result;

constexpr uint32_t MAGIC = 0x4E565341UL; // "NVSA"

// On-flash header, followed by the raw T bytes. Its layout, MAGIC, the key
// names and the CRC32 algorithm are the storage format: changing any of them
// makes data written by previous firmware unreadable.
struct StorageHeader {
    uint32_t magic;
    uint16_t headerSize;
    uint16_t version;
    uint32_t dataSize;
    uint32_t sequence;
    uint32_t crc32;
};
static_assert(sizeof(StorageHeader) == 20,
              "StorageHeader layout is part of the on-flash format");

enum class SlotState : uint8_t {
    MISSING,    // key not present
    INVALID,    // present but header/size/CRC wrong: safe to overwrite
    UNREADABLE, // could not be read (NVS/heap error): contents unknown
    VALID
};

struct SlotInfo {
    SlotState state = SlotState::MISSING;
    Result error = Result::OK; // why state is UNREADABLE or INVALID
    uint16_t version = 0;
    uint32_t sequence = 0;
    uint32_t dataSize = 0;
    uint32_t crc = 0;
    bool sameData = false;     // payload equals the `compare` buffer
};

// Closes the NVS handle on every return path.
class NvsHandle {
public:
    NvsHandle() = default;
    NvsHandle(const NvsHandle&) = delete;
    NvsHandle& operator=(const NvsHandle&) = delete;
    ~NvsHandle() {
        if (_open) nvs_close(_handle);
    }

    esp_err_t open(const char* nameSpace, nvs_open_mode_t mode) {
        const esp_err_t err = nvs_open(nameSpace, mode, &_handle);
        _open = (err == ESP_OK);
        return err;
    }

    nvs_handle_t get() const { return _handle; }

private:
    nvs_handle_t _handle = 0;
    bool _open = false;
};

uint32_t calculateCRC32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

// Wrap-around safe: true if sequence a was written after b.
bool isSequenceNewer(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) > 0;
}

const SlotInfo* selectNewest(const SlotInfo& a, const SlotInfo& b) {
    const bool aValid = a.state == SlotState::VALID;
    const bool bValid = b.state == SlotState::VALID;
    if (aValid && bValid) return isSequenceNewer(a.sequence, b.sequence) ? &a : &b;
    if (aValid) return &a;
    if (bValid) return &b;
    return nullptr;
}

// Reads and validates one slot. When the slot is valid, optionally copies
// min(stored, size) payload bytes into `dst` and/or compares the payload
// with the `size` bytes at `compare`.
void readSlot(nvs_handle_t handle, const char* key, size_t size,
              SlotInfo& out, void* dst, const void* compare) {
    out = SlotInfo{};

    size_t blobSize = 0;
    esp_err_t err = nvs_get_blob(handle, key, nullptr, &blobSize);
    if (err == ESP_ERR_NVS_NOT_FOUND) return;
    if (err != ESP_OK) {
        out.state = SlotState::UNREADABLE;
        out.error = Result::READ_ERROR;
        return;
    }
    if (blobSize < sizeof(StorageHeader)) {
        out.state = SlotState::INVALID;
        out.error = Result::INVALID_HEADER;
        return;
    }

    uint8_t* buffer = static_cast<uint8_t*>(malloc(blobSize));
    if (!buffer) {
        out.state = SlotState::UNREADABLE;
        out.error = Result::OUT_OF_MEMORY;
        return;
    }

    size_t got = blobSize;
    err = nvs_get_blob(handle, key, buffer, &got);
    if (err != ESP_OK || got != blobSize) {
        free(buffer);
        out.state = SlotState::UNREADABLE;
        out.error = Result::READ_ERROR;
        return;
    }

    StorageHeader h;
    memcpy(&h, buffer, sizeof(h));
    const uint8_t* stored = buffer + sizeof(StorageHeader);

    out.state = SlotState::INVALID;
    if (h.magic != MAGIC || h.headerSize != sizeof(StorageHeader)) {
        out.error = Result::INVALID_HEADER;
    } else if (h.dataSize == 0 || sizeof(StorageHeader) + h.dataSize != blobSize) {
        out.error = Result::INVALID_SIZE;
    } else if (calculateCRC32(stored, h.dataSize) != h.crc32) {
        out.error = Result::CRC_ERROR;
    } else {
        out.state = SlotState::VALID;
        out.error = Result::OK;
        out.version = h.version;
        out.sequence = h.sequence;
        out.dataSize = h.dataSize;
        out.crc = h.crc32;
        if (dst) memcpy(dst, stored, h.dataSize < size ? h.dataSize : size);
        if (compare) {
            out.sameData = h.dataSize == size && memcmp(stored, compare, size) == 0;
        }
    }
    free(buffer);
}

Result writeSlot(nvs_handle_t handle, const char* key, const void* data,
                 size_t size, uint16_t version, uint32_t sequence, uint32_t crc) {
    StorageHeader h;
    h.magic = MAGIC;
    h.headerSize = sizeof(StorageHeader);
    h.version = version;
    h.dataSize = static_cast<uint32_t>(size);
    h.sequence = sequence;
    h.crc32 = crc;

    const size_t blobSize = sizeof(StorageHeader) + size;
    uint8_t* buffer = static_cast<uint8_t*>(malloc(blobSize));
    if (!buffer) return Result::OUT_OF_MEMORY;
    memcpy(buffer, &h, sizeof(h));
    memcpy(buffer + sizeof(h), data, size);

    esp_err_t err = nvs_set_blob(handle, key, buffer, blobSize);
    free(buffer);
    if (err == ESP_OK) err = nvs_commit(handle);
    return err == ESP_OK ? Result::OK : Result::WRITE_ERROR;
}

} // namespace

NVSStorageABCore::NVSStorageABCore(const char* nameSpace,
                                   const char* baseKey,
                                   uint16_t currentVersion,
                                   size_t dataSize)
    : _namespace(nameSpace),
      _baseKey(baseKey),
      _currentVersion(currentVersion),
      _dataSize(dataSize) {}

NVSStorageABCore::Result NVSStorageABCore::loadRaw(void* data, bool upgrade) {
    setDefaults(data);
    if (!keysValid()) return Result::INVALID_ARGUMENT;

    char keyA[KEY_SIZE], keyB[KEY_SIZE];
    buildKey('A', keyA);
    buildKey('B', keyB);

    bool foundValid = false;
    {
        NvsHandle nvs;
        SlotInfo a, b;
        const esp_err_t err = nvs.open(_namespace, NVS_READONLY);
        if (err == ESP_OK) {
            readSlot(nvs.get(), keyA, _dataSize, a, nullptr, nullptr);
            readSlot(nvs.get(), keyB, _dataSize, b, nullptr, nullptr);
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            // NOT_FOUND only means the namespace was never written.
            return Result::NVS_OPEN_ERROR;
        }

        const SlotInfo* selected = selectNewest(a, b);
        if (!selected) {
            // A slot that could not be read may still hold good data:
            // report the error instead of replacing it with defaults.
            if (a.state == SlotState::UNREADABLE) return a.error;
            if (b.state == SlotState::UNREADABLE) return b.error;
            if (!upgrade) return Result::DEFAULTS_LOADED;
        } else {
            if (selected->version > _currentVersion) return Result::FUTURE_VERSION;

            SlotInfo loaded;
            readSlot(nvs.get(), selected == &a ? keyA : keyB, _dataSize,
                     loaded, data, nullptr);
            if (loaded.state != SlotState::VALID || loaded.sequence != selected->sequence) {
                setDefaults(data);
                return loaded.state == SlotState::UNREADABLE ? loaded.error : Result::READ_ERROR;
            }
            foundValid = true;
            if (loaded.version < _currentVersion) onMigrate(loaded.version, data);

            const bool current = loaded.version == _currentVersion &&
                                 loaded.dataSize == _dataSize;
            if (!upgrade || current) return Result::OK;
        }
    }

    // Upgrade path: persist defaults (nothing valid found) or the migrated data.
    const Result s = saveRaw(data);
    if (!isSuccess(s)) return s;
    return foundValid ? Result::OK : Result::DEFAULTS_LOADED;
}

NVSStorageABCore::Result NVSStorageABCore::saveRaw(const void* data) {
    if (!keysValid()) return Result::INVALID_ARGUMENT;

    char keyA[KEY_SIZE], keyB[KEY_SIZE];
    buildKey('A', keyA);
    buildKey('B', keyB);

    NvsHandle nvs;
    if (nvs.open(_namespace, NVS_READWRITE) != ESP_OK) return Result::NVS_OPEN_ERROR;

    SlotInfo a, b;
    readSlot(nvs.get(), keyA, _dataSize, a, nullptr, data);
    readSlot(nvs.get(), keyB, _dataSize, b, nullptr, data);

    // An unreadable slot may be the newest copy: overwriting the other one
    // with a lower sequence would make the next load pick stale data.
    if (a.state == SlotState::UNREADABLE) return a.error;
    if (b.state == SlotState::UNREADABLE) return b.error;

    const uint32_t crc = calculateCRC32(static_cast<const uint8_t*>(data), _dataSize);

    const SlotInfo* newest = selectNewest(a, b);
    if (newest &&
        newest->version == _currentVersion &&
        newest->dataSize == _dataSize &&
        newest->crc == crc &&
        newest->sameData) {
        return Result::UNCHANGED;
    }

    // Always overwrite the slot that does not hold the newest valid copy.
    const char* target = keyA;
    uint32_t sequence = 1;
    if (newest == &a) {
        target = keyB;
        sequence = a.sequence + 1U;
    } else if (newest == &b) {
        target = keyA;
        sequence = b.sequence + 1U;
    }

    const Result wr = writeSlot(nvs.get(), target, data, _dataSize,
                                _currentVersion, sequence, crc);
    if (wr != Result::OK) return wr;

    SlotInfo verify;
    readSlot(nvs.get(), target, _dataSize, verify, nullptr, data);
    if (verify.state != SlotState::VALID ||
        verify.sequence != sequence ||
        verify.version != _currentVersion ||
        verify.dataSize != _dataSize ||
        verify.crc != crc ||
        !verify.sameData) {
        return Result::VERIFY_ERROR;
    }
    return Result::OK;
}

bool NVSStorageABCore::erase() {
    if (!keysValid()) return false;

    NvsHandle nvs;
    const esp_err_t err = nvs.open(_namespace, NVS_READWRITE);
    if (err != ESP_OK) return false;

    char key[KEY_SIZE];
    buildKey('A', key);
    const esp_err_t errA = nvs_erase_key(nvs.get(), key);
    buildKey('B', key);
    const esp_err_t errB = nvs_erase_key(nvs.get(), key);

    const bool okA = errA == ESP_OK || errA == ESP_ERR_NVS_NOT_FOUND;
    const bool okB = errB == ESP_OK || errB == ESP_ERR_NVS_NOT_FOUND;
    return okA && okB && nvs_commit(nvs.get()) == ESP_OK;
}

bool NVSStorageABCore::isSuccess(Result r) {
    return r == Result::OK || r == Result::UNCHANGED || r == Result::DEFAULTS_LOADED;
}

const char* NVSStorageABCore::resultToString(Result r) {
    switch (r) {
        case Result::OK: return "OK";
        case Result::UNCHANGED: return "UNCHANGED";
        case Result::DEFAULTS_LOADED: return "DEFAULTS_LOADED";
        case Result::NVS_OPEN_ERROR: return "NVS_OPEN_ERROR";
        case Result::READ_ERROR: return "READ_ERROR";
        case Result::WRITE_ERROR: return "WRITE_ERROR";
        case Result::VERIFY_ERROR: return "VERIFY_ERROR";
        case Result::INVALID_HEADER: return "INVALID_HEADER";
        case Result::INVALID_SIZE: return "INVALID_SIZE";
        case Result::CRC_ERROR: return "CRC_ERROR";
        case Result::OUT_OF_MEMORY: return "OUT_OF_MEMORY";
        case Result::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case Result::FUTURE_VERSION: return "FUTURE_VERSION";
        default: return "UNKNOWN";
    }
}

bool NVSStorageABCore::keysValid() const {
    if (!_namespace || !_baseKey) return false;
    const size_t nsLen = strlen(_namespace);
    const size_t baseLen = strlen(_baseKey);
    // NVS names are max 15 chars. The "_A"/"_B" suffix adds 2.
    return nsLen > 0 && nsLen <= KEY_SIZE - 1 &&
           baseLen > 0 && baseLen <= KEY_SIZE - 3;
}

void NVSStorageABCore::buildKey(char slot, char (&out)[KEY_SIZE]) const {
    snprintf(out, sizeof(out), "%s_%c", _baseKey, slot);
}

void NVSStorageABCore::setDefaults(void* data) const {
    // Zeroing first makes padding deterministic for CRC/memcmp anti-wear.
    memset(data, 0, _dataSize);
    onDefaults(data);
}
