#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <type_traits>

template <typename T>
class NVSStorageAB {
public:
    enum class Result : uint8_t {
        OK,
        UNCHANGED,
        DEFAULTS_LOADED,
        NVS_OPEN_ERROR,
        READ_ERROR,
        WRITE_ERROR,
        VERIFY_ERROR,
        INVALID_HEADER,
        INVALID_SIZE,
        CRC_ERROR,
        OUT_OF_MEMORY,
        INVALID_ARGUMENT,
        FUTURE_VERSION
    };

    using DefaultsFunction = void (*)(T&);
    using MigrationFunction = void (*)(uint16_t oldVersion, T& data);

    NVSStorageAB(const char* nameSpace,
                 const char* baseKey,
                 uint16_t currentVersion,
                 DefaultsFunction defaults,
                 MigrationFunction migration = nullptr)
        : _namespace(nameSpace),
          _baseKey(baseKey),
          _currentVersion(currentVersion),
          _defaults(defaults),
          _migration(migration) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "NVSStorageAB<T>: T must be trivially copyable");
        static_assert(std::is_standard_layout<T>::value,
                      "NVSStorageAB<T>: T must have standard layout");
    }

    // Loads the newest valid A/B slot. If neither slot is valid, defaults are
    // loaded into RAM and DEFAULTS_LOADED is returned. No flash write occurs.
    Result load(T& data) {
        SlotInfo a, b;
        const Result ra = readSlot('A', a);
        const Result rb = readSlot('B', b);
        (void)ra; (void)rb;

        SlotInfo* selected = selectNewest(a, b);
        if (!selected) {
            setDefaults(data);
            return Result::DEFAULTS_LOADED;
        }
        if (selected->version > _currentVersion) {
            setDefaults(data);
            return Result::FUTURE_VERSION;
        }

        data = selected->data;
        applyMigration(selected->version, data);
        return Result::OK;
    }

    // Same as load(), but if an older version is loaded it is migrated and
    // immediately persisted in the current format. If no valid slot exists,
    // defaults are created and persisted automatically.
    Result loadAndUpgrade(T& data) {
        SlotInfo a, b;
        readSlot('A', a);
        readSlot('B', b);

        SlotInfo* selected = selectNewest(a, b);
        if (!selected) {
            setDefaults(data);
            Result s = save(data);
            return isSuccess(s) ? Result::DEFAULTS_LOADED : s;
        }
        if (selected->version > _currentVersion) {
            setDefaults(data);
            return Result::FUTURE_VERSION;
        }

        data = selected->data;
        const uint16_t oldVersion = selected->version;
        applyMigration(oldVersion, data);

        if (oldVersion < _currentVersion || selected->dataSize != sizeof(T)) {
            Result s = save(data);
            if (!isSuccess(s)) return s;
        }
        return Result::OK;
    }

    // Anti-wear save. If the newest valid slot already contains exactly the
    // same current-version data, no NVS write is performed and UNCHANGED is
    // returned. Otherwise the older/invalid slot is written and verified.
    Result save(const T& data) {
        SlotInfo a, b;
        readSlot('A', a);
        readSlot('B', b);
        SlotInfo* newest = selectNewest(a, b);

        const uint32_t currentCrc = calculateCRC32(
            reinterpret_cast<const uint8_t*>(&data), sizeof(T));

        if (newest &&
            newest->version == _currentVersion &&
            newest->dataSize == sizeof(T) &&
            newest->storedCRC == currentCrc &&
            memcmp(&newest->data, &data, sizeof(T)) == 0) {
            return Result::UNCHANGED;
        }

        char target = 'A';
        uint32_t sequence = 1;

        if (a.valid && b.valid) {
            if (isSequenceNewer(a.sequence, b.sequence)) {
                target = 'B';
                sequence = a.sequence + 1U;
            } else {
                target = 'A';
                sequence = b.sequence + 1U;
            }
        } else if (a.valid) {
            target = 'B';
            sequence = a.sequence + 1U;
        } else if (b.valid) {
            target = 'A';
            sequence = b.sequence + 1U;
        }

        Result wr = writeSlot(target, data, sequence, currentCrc);
        if (wr != Result::OK) return wr;

        SlotInfo verify;
        Result vr = readSlot(target, verify);
        if (vr != Result::OK || !verify.valid ||
            verify.sequence != sequence ||
            verify.version != _currentVersion ||
            verify.dataSize != sizeof(T) ||
            verify.storedCRC != currentCrc ||
            memcmp(&verify.data, &data, sizeof(T)) != 0) {
            return Result::VERIFY_ERROR;
        }
        return Result::OK;
    }

    bool erase() {
        if (!keysValid()) return false;
        Preferences prefs;
        if (!prefs.begin(_namespace, false)) return false;
        char a[16], b[16];
        buildKey('A', a, sizeof(a));
        buildKey('B', b, sizeof(b));
        bool okA = !prefs.isKey(a) || prefs.remove(a);
        bool okB = !prefs.isKey(b) || prefs.remove(b);
        prefs.end();
        return okA && okB;
    }

    uint16_t currentVersion() const { return _currentVersion; }

    static bool isSuccess(Result r) {
        return r == Result::OK || r == Result::UNCHANGED || r == Result::DEFAULTS_LOADED;
    }

    static const char* resultToString(Result r) {
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

private:
    struct StorageHeader {
        uint32_t magic;
        uint16_t headerSize;
        uint16_t version;
        uint32_t dataSize;
        uint32_t sequence;
        uint32_t crc32;
    };

    struct SlotInfo {
        bool valid = false;
        uint16_t version = 0;
        uint32_t sequence = 0;
        uint32_t dataSize = 0;
        uint32_t storedCRC = 0;
        T data{};
    };

    static constexpr uint32_t MAGIC = 0x4E565341UL; // "NVSA"
    const char* _namespace;
    const char* _baseKey;
    uint16_t _currentVersion;
    DefaultsFunction _defaults;
    MigrationFunction _migration;

    bool keysValid() const {
        if (!_namespace || !_baseKey) return false;
        const size_t nsLen = strlen(_namespace);
        const size_t baseLen = strlen(_baseKey);
        // ESP32 Preferences/NVS names are max 15 chars. Suffix "_A"/"_B" adds 2.
        return nsLen > 0 && nsLen <= 15 && baseLen > 0 && baseLen <= 13;
    }

    void setDefaults(T& data) const {
        // Zeroing first makes padding deterministic for CRC/memcmp anti-wear.
        memset(&data, 0, sizeof(T));
        if (_defaults) _defaults(data);
    }

    void applyMigration(uint16_t oldVersion, T& data) const {
        if (_migration && oldVersion < _currentVersion) {
            _migration(oldVersion, data);
        }
    }

    void buildKey(char slot, char* out, size_t outSize) const {
        snprintf(out, outSize, "%s_%c", _baseKey, slot);
    }

    static bool isSequenceNewer(uint32_t a, uint32_t b) {
        return static_cast<int32_t>(a - b) > 0;
    }

    static SlotInfo* selectNewest(SlotInfo& a, SlotInfo& b) {
        if (!a.valid && !b.valid) return nullptr;
        if (a.valid && !b.valid) return &a;
        if (!a.valid && b.valid) return &b;
        return isSequenceNewer(a.sequence, b.sequence) ? &a : &b;
    }

    Result writeSlot(char slot, const T& data, uint32_t sequence, uint32_t crc) {
        if (!keysValid()) return Result::INVALID_ARGUMENT;

        StorageHeader h{};
        h.magic = MAGIC;
        h.headerSize = sizeof(StorageHeader);
        h.version = _currentVersion;
        h.dataSize = sizeof(T);
        h.sequence = sequence;
        h.crc32 = crc;

        const size_t blobSize = sizeof(StorageHeader) + sizeof(T);
        uint8_t* buffer = static_cast<uint8_t*>(malloc(blobSize));
        if (!buffer) return Result::OUT_OF_MEMORY;
        memcpy(buffer, &h, sizeof(h));
        memcpy(buffer + sizeof(h), &data, sizeof(T));

        char key[16];
        buildKey(slot, key, sizeof(key));
        Preferences prefs;
        if (!prefs.begin(_namespace, false)) {
            free(buffer);
            return Result::NVS_OPEN_ERROR;
        }
        const size_t written = prefs.putBytes(key, buffer, blobSize);
        prefs.end();
        free(buffer);
        return written == blobSize ? Result::OK : Result::WRITE_ERROR;
    }

    Result readSlot(char slot, SlotInfo& out) {
        out = SlotInfo{};
        setDefaults(out.data);
        if (!keysValid()) return Result::INVALID_ARGUMENT;

        char key[16];
        buildKey(slot, key, sizeof(key));
        Preferences prefs;
        if (!prefs.begin(_namespace, true)) return Result::NVS_OPEN_ERROR;
        const size_t blobSize = prefs.getBytesLength(key);
        if (blobSize == 0) {
            prefs.end();
            return Result::READ_ERROR;
        }
        if (blobSize < sizeof(StorageHeader)) {
            prefs.end();
            return Result::INVALID_HEADER;
        }

        uint8_t* buffer = static_cast<uint8_t*>(malloc(blobSize));
        if (!buffer) {
            prefs.end();
            return Result::OUT_OF_MEMORY;
        }
        const size_t got = prefs.getBytes(key, buffer, blobSize);
        prefs.end();
        if (got != blobSize) {
            free(buffer);
            return Result::READ_ERROR;
        }

        StorageHeader h{};
        memcpy(&h, buffer, sizeof(h));
        if (h.magic != MAGIC || h.headerSize != sizeof(StorageHeader)) {
            free(buffer);
            return Result::INVALID_HEADER;
        }
        const size_t expected = static_cast<size_t>(h.headerSize) + static_cast<size_t>(h.dataSize);
        if (expected != blobSize || h.dataSize == 0) {
            free(buffer);
            return Result::INVALID_SIZE;
        }

        const uint8_t* stored = buffer + h.headerSize;
        const uint32_t crc = calculateCRC32(stored, h.dataSize);
        if (crc != h.crc32) {
            free(buffer);
            return Result::CRC_ERROR;
        }

        const size_t copySize = h.dataSize < sizeof(T) ? h.dataSize : sizeof(T);
        memcpy(&out.data, stored, copySize);
        out.version = h.version;
        out.sequence = h.sequence;
        out.dataSize = h.dataSize;
        out.storedCRC = h.crc32;
        out.valid = true;
        free(buffer);
        return Result::OK;
    }

    static uint32_t calculateCRC32(const uint8_t* data, size_t length) {
        uint32_t crc = 0xFFFFFFFFUL;
        for (size_t i = 0; i < length; ++i) {
            crc ^= data[i];
            for (uint8_t bit = 0; bit < 8; ++bit) {
                crc = (crc & 1U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
            }
        }
        return crc ^ 0xFFFFFFFFUL;
    }
};
