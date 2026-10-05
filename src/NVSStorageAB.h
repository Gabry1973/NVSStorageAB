#pragma once

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

// Non-template part of the library: NVS access, A/B slot selection, CRC32,
// write verification and anti-wear. It works on raw bytes and is implemented
// in NVSStorageAB.cpp, so it is compiled only once regardless of how many
// NVSStorageAB<T> types a sketch instantiates.
class NVSStorageABCore {
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

    // Removes both A/B slots. Returns true if nothing is left in NVS.
    bool erase();

    uint16_t currentVersion() const { return _currentVersion; }

    static bool isSuccess(Result r);
    static const char* resultToString(Result r);

protected:
    NVSStorageABCore(const char* nameSpace,
                     const char* baseKey,
                     uint16_t currentVersion,
                     size_t dataSize);
    ~NVSStorageABCore() = default;

    Result loadRaw(void* data, bool upgrade);
    Result saveRaw(const void* data);

    // Called with a zeroed buffer of dataSize bytes.
    virtual void onDefaults(void* data) const = 0;
    // Called only when oldVersion < currentVersion.
    virtual void onMigrate(uint16_t oldVersion, void* data) const = 0;

private:
    static constexpr size_t KEY_SIZE = 16; // NVS key/namespace: 15 chars + NUL

    bool keysValid() const;
    void buildKey(char slot, char (&out)[KEY_SIZE]) const;
    void setDefaults(void* data) const;

    const char* _namespace;
    const char* _baseKey;
    uint16_t _currentVersion;
    size_t _dataSize;
};

template <typename T>
class NVSStorageAB : public NVSStorageABCore {
public:
    using DefaultsFunction = void (*)(T&);
    using MigrationFunction = void (*)(uint16_t oldVersion, T& data);

    NVSStorageAB(const char* nameSpace,
                 const char* baseKey,
                 uint16_t currentVersion,
                 DefaultsFunction defaults,
                 MigrationFunction migration = nullptr)
        : NVSStorageABCore(nameSpace, baseKey, currentVersion, sizeof(T)),
          _defaults(defaults),
          _migration(migration) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "NVSStorageAB<T>: T must be trivially copyable");
        static_assert(std::is_standard_layout<T>::value,
                      "NVSStorageAB<T>: T must have standard layout");
    }

    // Loads the newest valid A/B slot. If neither slot is valid, defaults are
    // loaded into RAM and DEFAULTS_LOADED is returned. No flash write occurs.
    Result load(T& data) { return loadRaw(&data, false); }

    // Same as load(), but if an older version is loaded it is migrated and
    // immediately persisted in the current format. If no valid slot exists,
    // defaults are created and persisted automatically.
    Result loadAndUpgrade(T& data) { return loadRaw(&data, true); }

    // Anti-wear save. If the newest valid slot already contains exactly the
    // same current-version data, no NVS write is performed and UNCHANGED is
    // returned. Otherwise the older/invalid slot is written and verified.
    Result save(const T& data) { return saveRaw(&data); }

protected:
    void onDefaults(void* data) const override {
        if (_defaults) _defaults(*static_cast<T*>(data));
    }

    void onMigrate(uint16_t oldVersion, void* data) const override {
        if (_migration) _migration(oldVersion, *static_cast<T*>(data));
    }

private:
    DefaultsFunction _defaults;
    MigrationFunction _migration;
};
